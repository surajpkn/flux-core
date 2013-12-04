/* apisrv.c - bridge unix domain API socket and zmq message broker */

#define _GNU_SOURCE
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <ctype.h>
#include <fcntl.h>
#include <zmq.h>
#include <czmq.h>
#include <json/json.h>

#include "zmsg.h"
#include "plugin.h"
#include "util.h"
#include "log.h"
#include "cmb_socket.h"

#define LISTEN_BACKLOG      5

struct _client_struct;

typedef struct {
    int listen_fd;
    struct _client_struct *clients;
    flux_t h;
} ctx_t;

typedef struct {
    int type; 
    char *topic;
} subscription_t;

typedef struct _client_struct {
    int fd;
    struct _client_struct *next;
    struct _client_struct *prev;
    ctx_t *ctx;
    zhash_t *disconnect_notify;
    zlist_t *subscriptions;
    char *uuid;
    int cfd_id;
} client_t;

static void freectx (ctx_t *ctx)
{
    free (ctx);
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "apisrv");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->h = h;
        flux_aux_set (h, "apisrv", ctx, (FluxFreeFn)freectx);
    }

    return ctx;
}

static client_t * client_create (ctx_t *ctx, int fd)
{
    client_t *c;

    c = xzmalloc (sizeof (client_t));
    c->fd = fd;
    c->uuid = uuid_generate_str ();
    c->ctx = ctx;
    if (!(c->disconnect_notify = zhash_new ()))
        oom ();
    if (!(c->subscriptions = zlist_new ()))
        oom ();
    c->prev = NULL;
    c->next = ctx->clients;
    if (c->next)
        c->next->prev = c;
    ctx->clients = c;
    return (c);
}

static subscription_t *subscription_create (flux_t h, int type, char *topic)
{
    subscription_t *sub = xzmalloc (sizeof (*sub));
    sub->type = type;
    sub->topic = xstrdup (topic);
    if (type == FLUX_MSGTYPE_EVENT) {
        (void)flux_event_subscribe (h, topic);
        //flux_log (h, LOG_DEBUG, "event subscribe %s", topic);
    } else if (type == FLUX_MSGTYPE_SNOOP) {
        /* N.B. snoop messages may have routing headers, so do not attempt
         * to subscribe by tag - just use "" to subscribe to all.
         */
        (void)flux_snoop_subscribe (h, "");
        //flux_log (h, LOG_DEBUG, "snoop subscribe %s", topic);
    }
    return sub;
}

static void subscription_destroy (flux_t h, subscription_t *sub)
{
    if (sub->type == FLUX_MSGTYPE_EVENT) {
        (void)flux_event_unsubscribe (h, sub->topic);
        //flux_log (h, LOG_DEBUG, "event unsubscribe %s", sub->topic);
    } else if (sub->type == FLUX_MSGTYPE_SNOOP) {
        (void)flux_snoop_unsubscribe (h, "");
        //flux_log (h, LOG_DEBUG, "snoop unsubscribe %s", sub->topic);
    }
    free (sub->topic);
    free (sub);
}

static subscription_t *subscription_lookup (client_t *c, int type, char *topic)
{
    subscription_t *sub;

    sub = zlist_first (c->subscriptions);
    while (sub) {
        if (sub->type == type && !strcmp (sub->topic, topic))
            return sub;
        sub = zlist_next (c->subscriptions);
    }
    return NULL;
}

static bool subscription_match (client_t *c, int type, char *topic)
{
    subscription_t *sub;

    sub = zlist_first (c->subscriptions);
    while (sub) {
        if (sub->type == type && !strncmp (sub->topic, topic,
                                           strlen (sub->topic)))
            return true;
        sub = zlist_next (c->subscriptions);
    }
    return false;
}

static int notify_srv (const char *key, void *item, void *arg)
{
    client_t *c = arg;
    zmsg_t *zmsg; 
    json_object *o;

    if (!(zmsg = zmsg_new ()))
        err_exit ("zmsg_new");
    o = util_json_object_new_object ();
    if (zmsg_pushstr (zmsg, "%s", json_object_to_json_string (o)) < 0)
        err_exit ("zmsg_pushstr");
    json_object_put (o);
    if (zmsg_pushstr (zmsg, "%s.disconnect", key) < 0)
        err_exit ("zmsg_pushstr");
    if (zmsg_pushmem (zmsg, NULL, 0) < 0) /* delimiter frame */
        err_exit ("zmsg_pushmem");
    if (zmsg_pushstr (zmsg, "%s", c->uuid) < 0)
        err_exit ("zmsg_pushmem");

    flux_request_sendmsg (c->ctx->h, &zmsg);

    return 0;
}

static void client_destroy (ctx_t *ctx, client_t *c)
{
    subscription_t *sub;

    zhash_foreach (c->disconnect_notify, notify_srv, c);
    zhash_destroy (&c->disconnect_notify);

    while ((sub = zlist_pop (c->subscriptions)))
        subscription_destroy (ctx->h, sub);
    zlist_destroy (&c->subscriptions);

    free (c->uuid);
    close (c->fd);

    if (c->prev)
        c->prev->next = c->next;
    else
        ctx->clients = c->next;
    if (c->next)
        c->next->prev = c->prev;
    free (c);
}

static int client_read (ctx_t *ctx, client_t *c)
{
    zmsg_t *zmsg = NULL;
    char *name = NULL;
    int typemask;
    subscription_t *sub;

    zmsg = zmsg_recv_fd_typemask (c->fd, &typemask, true);
    if (!zmsg) {
        if (errno != ECONNRESET && errno != EWOULDBLOCK && errno != EPROTO)
            err ("API read");
        return -1;
    }
    if ((typemask & FLUX_MSGTYPE_EVENT)) {
        flux_event_sendmsg (ctx->h, &zmsg);
        goto done;
    }
    if (!(typemask & FLUX_MSGTYPE_REQUEST))
        goto done; /* DROP */
        
    if (cmb_msg_match_substr (zmsg, "api.snoop.subscribe.", &name)) {
        sub = subscription_create (ctx->h, FLUX_MSGTYPE_SNOOP, name);
        if (zlist_append (c->subscriptions, sub) < 0)
            oom ();
    } else if (cmb_msg_match_substr (zmsg, "api.snoop.unsubscribe.", &name)) {
        if ((sub = subscription_lookup (c, FLUX_MSGTYPE_SNOOP, name)))
            zlist_remove (c->subscriptions, sub);
    } else if (cmb_msg_match_substr (zmsg, "api.event.subscribe.", &name)) {
        sub = subscription_create (ctx->h, FLUX_MSGTYPE_EVENT, name);
        if (zlist_append (c->subscriptions, sub) < 0)
            oom ();
    } else if (cmb_msg_match_substr (zmsg, "api.event.unsubscribe.", &name)) {
        if ((sub = subscription_lookup (c, FLUX_MSGTYPE_EVENT, name)))
            zlist_remove (c->subscriptions, sub);
    } else {
        /* insert disconnect notifier before forwarding request */
        if (c->disconnect_notify) {
            char *tag = cmb_msg_tag (zmsg, true); /* first component only */
            if (!tag)
                goto done;
            if (zhash_lookup (c->disconnect_notify, tag) == NULL) {
                if (zhash_insert (c->disconnect_notify, tag, tag) < 0)
                    err_exit ("zhash_insert");
                zhash_freefn (c->disconnect_notify, tag, free);
            } else
                free (tag);
        }
        if (zmsg_pushstr (zmsg, "%s", c->uuid) < 0)
            err_exit ("zmsg_pushmem");
        flux_request_sendmsg (ctx->h, &zmsg);
    }
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    if (name)
        free (name);
    return 0;
}

static int client_cb (flux_t h, int fd, short revents, void *arg)
{
    client_t *c = arg;
    ctx_t *ctx = c->ctx;
    bool delete = false;

    if (revents & ZMQ_POLLIN) {
        while (client_read (ctx, c) != -1)
            ;
        if (errno != EWOULDBLOCK && errno != EAGAIN)
            delete = true;
    }
    if (revents & ZMQ_POLLERR)
        delete = true;

    if (delete) {
        /*  Cancel this client's fd from the reactor and destroy client
         */
        flux_fdhandler_remove (h, fd, ZMQ_POLLIN | ZMQ_POLLERR);
        client_destroy (ctx, c);
    }
    return 0;
}

static void recv_response (ctx_t *ctx, zmsg_t **zmsg)
{
    char *uuid = NULL;
    zframe_t *zf = NULL;
    client_t *c;

    if (zmsg_hopcount (*zmsg) != 1) {
        msg ("apisrv: ignoring response with bad envelope");
        return;
    }
    uuid = zmsg_popstr (*zmsg);
    assert (uuid != NULL);
    zf = zmsg_pop (*zmsg);
    assert (zf != NULL);
    assert (zframe_size (zf) == 0);

    for (c = ctx->clients; c != NULL && *zmsg != NULL; ) {

        if (!strcmp (uuid, c->uuid)) {
            if (zmsg_send_fd_typemask (c->fd, FLUX_MSGTYPE_RESPONSE, zmsg) < 0)
                zmsg_destroy (zmsg);
            break;
        }
        c = c->next;
    }
    if (*zmsg) {
        //msg ("apisrv: discarding response for unknown uuid %s", uuid);
        zmsg_destroy (zmsg);
    }
    if (zf)
        zframe_destroy (&zf);
    if (uuid)
        free (uuid);
}

static void recv_event (ctx_t *ctx, zmsg_t **zmsg)
{
    client_t *c;
    char *tag = flux_zmsg_tag (*zmsg);
    zmsg_t *cpy;


    if (tag) {
        //flux_log (ctx->h, LOG_DEBUG, "event received: %s", tag);
        for (c = ctx->clients; c != NULL; c = c->next) {
            if (subscription_match (c, FLUX_MSGTYPE_EVENT, tag)) {
                if (!(cpy = zmsg_dup (*zmsg)))
                    oom ();
                if (zmsg_send_fd_typemask (c->fd, FLUX_MSGTYPE_EVENT, &cpy) < 0)
                    zmsg_destroy (&cpy);
            }
        }
        free (tag);
    }
}

static void recv_snoop (ctx_t *ctx, zmsg_t **zmsg)
{
    client_t *c;
    char *tag = flux_zmsg_tag (*zmsg);
    zmsg_t *cpy;

    if (tag) {
        if (strcmp (tag, "log.msg") != 0 && strcmp (tag, "cmb.info") != 0)
            //flux_log (ctx->h, LOG_DEBUG, "snoop received: %s", tag);
        for (c = ctx->clients; c != NULL; c = c->next) {
            if (subscription_match (c, FLUX_MSGTYPE_SNOOP, tag)) {
                if (!(cpy = zmsg_dup (*zmsg)))
                    oom ();
                if (zmsg_send_fd_typemask (c->fd, FLUX_MSGTYPE_SNOOP, &cpy) < 0)
                    zmsg_destroy (&cpy);
            }
        }
        free (tag);
    }
}

static int apisrv_recv (flux_t h, zmsg_t **zmsg, int typemask)
{
    ctx_t *ctx = getctx (h);

    if ((typemask & FLUX_MSGTYPE_EVENT))
        recv_event (ctx, zmsg);
    else if ((typemask & FLUX_MSGTYPE_RESPONSE))
        recv_response (ctx, zmsg);
    else if ((typemask & FLUX_MSGTYPE_SNOOP))
        recv_snoop (ctx, zmsg);
    return 0;
}

static int listener_cb (flux_t h, int fd, short revents, void *arg)
{
    ctx_t *ctx = arg;
    int rc = 0;

    if (revents & ZMQ_POLLIN) {       /* listenfd */
        client_t *c;
        int cfd;

        if ((cfd = accept (fd, NULL, NULL)) < 0) {
            err ("accept");
            goto done;
        }
        c = client_create (ctx, cfd);
        if (flux_fdhandler_add (h, cfd, ZMQ_POLLIN | ZMQ_POLLERR,
                                                    client_cb, c) < 0) {
            err ("%s: flux_fdhandler_add", __FUNCTION__);
            rc = -1; /* terminate reactor */
            goto done;
        }
    }
    if (revents & ZMQ_POLLERR) {      /* listenfd - error */
        err ("apisrv: poll on listen fd");
        goto done;
    }
done:
    return rc;
}

static int listener_init (ctx_t *ctx, char *sockpath)
{
    struct sockaddr_un addr;
    int fd;

    fd = socket (AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        err ("socket");
        goto done;
    }
    if (remove (sockpath) < 0 && errno != ENOENT) {
        err ("remove %s", sockpath);
        goto error_close;
    }
    memset (&addr, 0, sizeof (struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy (addr.sun_path, sockpath, sizeof (addr.sun_path) - 1);

    if (bind (fd, (struct sockaddr *)&addr, sizeof (struct sockaddr_un)) < 0) {
        err ("bind");
        goto error_close;
    }
    if (listen (fd, LISTEN_BACKLOG) < 0) {
        err ("listen");
        goto error_close;
    }
done:
    return fd;
error_close:
    close (fd);
    return -1;
}

static int apisrv_init (flux_t h, zhash_t *args)
{
    ctx_t *ctx = getctx (h);
    char *sockpath = NULL, *dfltpath = NULL;
    int rc = -1;

    if (!args || !(sockpath = zhash_lookup (args, "sockpath"))) {
        if (asprintf (&dfltpath, CMB_API_PATH_TMPL, geteuid ()) < 0)
            oom ();
        sockpath = dfltpath;
    }
    if ((ctx->listen_fd = listener_init (ctx, sockpath)) < 0)
        goto done;
    if (flux_fdhandler_add (h, ctx->listen_fd, ZMQ_POLLIN | ZMQ_POLLERR,
                                                    listener_cb, ctx) < 0)
        err_exit ("%s: flux_fdhandler_add", __FUNCTION__);
    rc = 0;
done:
    if (dfltpath)
        free (dfltpath);
    return rc;
}

static void apisrv_fini (flux_t h)
{
    ctx_t *ctx = getctx (h);

    if (close (ctx->listen_fd) < 0)
        err_exit ("listen");
    while (ctx->clients != NULL)
        client_destroy (ctx, ctx->clients);
}

const struct plugin_ops ops = {
    .recv = apisrv_recv,
    .init = apisrv_init,
    .fini = apisrv_fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
