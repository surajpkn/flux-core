#include <errno.h>
#include <czmq.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>

#include "src/common/libflux/message.h"
#include "src/common/libflux/handle.h"
#include "src/common/libflux/reactor.h"
#include "src/common/libflux/dispatch.h"
#include "src/common/libflux/request.h"

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libtap/tap.h"

static int send_request (flux_t h, const char *topic)
{
    int rc = -1;
    flux_msg_t *msg = flux_request_encode (topic, NULL);
    if (!msg || flux_send (h, msg, 0) < 0) {
        fprintf (stderr, "%s: flux_send failed: %s",
                 __FUNCTION__, strerror (errno));
        goto done;
    }
    rc = 0;
done:
    flux_msg_destroy (msg);
    return rc;
}

static int multmatch_count = 0;
static void multmatch1 (flux_t h, flux_msg_handler_t *w, const flux_msg_t *msg,
                        void *arg)
{
    const char *topic;
    if (flux_msg_get_topic (msg, &topic) < 0 || strcmp (topic, "foo.baz"))
        flux_reactor_stop_error (flux_get_reactor (h));
    flux_msg_handler_stop (w);
    multmatch_count++;
}

static void multmatch2 (flux_t h, flux_msg_handler_t *w, const flux_msg_t *msg,
                        void *arg)
{
    const char *topic;
    if (flux_msg_get_topic (msg, &topic) < 0 || strcmp (topic, "foo.bar"))
        flux_reactor_stop_error (flux_get_reactor (h));
    flux_msg_handler_stop (w);
    multmatch_count++;
}

static void test_multmatch (flux_t h)
{
    flux_msg_handler_t *w1, *w2;
    struct flux_match m1 = FLUX_MATCH_ANY;
    struct flux_match m2 = FLUX_MATCH_ANY;

    m1.topic_glob = "foo.*";
    m2.topic_glob = "foo.bar";

    /* test #1: verify multiple match behaves as documented, that is,
     * a message is matched (only) by the most recently added watcher
     */
    ok ((w1 = flux_msg_handler_create (h, m1, multmatch1, NULL)) != NULL,
        "multmatch: first added handler for foo.*");
    ok ((w2 = flux_msg_handler_create (h, m2, multmatch2, NULL)) != NULL,
        "multmatch: next added handler for foo.bar");
    flux_msg_handler_start (w1);
    flux_msg_handler_start (w2);
    ok (send_request (h, "foo.bar") == 0,
        "multmatch: send foo.bar msg");
    ok (send_request (h, "foo.baz") == 0,
        "multmatch: send foo.baz msg");
    ok (flux_reactor_run (flux_get_reactor (h), 0) == 0 && multmatch_count == 2,
        "multmatch: last added watcher handled foo.bar");
    flux_msg_handler_destroy (w1);
    flux_msg_handler_destroy (w2);
}

static int msgwatcher_count = 100;
static void msgreader (flux_t h, flux_msg_handler_t *w, const flux_msg_t *msg,
                       void *arg)
{
    static int count = 0;
    count++;
    if (count == msgwatcher_count)
        flux_msg_handler_stop (w);
}

static void test_msg (flux_t h)
{
    flux_msg_handler_t *w;
    int i;

    ok ((w = flux_msg_handler_create (h, FLUX_MATCH_ANY, msgreader, NULL))
        != NULL,
        "msg: created handler for any message");
    flux_msg_handler_start (w);
    for (i = 0; i < msgwatcher_count; i++) {
        if (send_request (h, "foo") < 0)
            break;
    }
    ok (i == msgwatcher_count,
        "msg: sent %d requests", i);
    ok (flux_reactor_run (flux_get_reactor (h), 0) == 0,
        "msg: reactor ran to completion after %d requests", msgwatcher_count);
    flux_msg_handler_stop (w);
    flux_msg_handler_destroy (w);
}

static const size_t zmqwriter_msgcount = 1024;

static void zmqwriter (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    void *sock = flux_zmq_watcher_get_zsock (w);
    static int count = 0;
    if (revents & FLUX_POLLERR) {
        fprintf (stderr, "%s: FLUX_POLLERR is set\n", __FUNCTION__);
        goto error;
    }
    if (revents & FLUX_POLLOUT) {
        uint8_t blob[64];
        zmsg_t *zmsg = zmsg_new ();
        if (!zmsg || zmsg_addmem (zmsg, blob, sizeof (blob)) < 0) {
            fprintf (stderr, "%s: failed to create message: %s\n",
                     __FUNCTION__, strerror (errno));
            goto error;
        }
        if (zmsg_send (&zmsg, sock) < 0) {
            fprintf (stderr, "%s: zmsg_send: %s\n",
                     __FUNCTION__, strerror (errno));
            goto error;
        }
        count++;
        if (count == zmqwriter_msgcount)
            flux_watcher_stop (w);
    }
    return;
error:
    flux_reactor_stop_error (r);
}

static void zmqreader (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    void *sock = flux_zmq_watcher_get_zsock (w);
    static int count = 0;
    if (revents & FLUX_POLLERR) {
        fprintf (stderr, "%s: FLUX_POLLERR is set\n", __FUNCTION__);
        goto error;
    }
    if (revents & FLUX_POLLIN) {
        zmsg_t *zmsg = zmsg_recv (sock);
        if (!zmsg) {
            fprintf (stderr, "%s: zmsg_recv: %s\n",
                     __FUNCTION__, strerror (errno));
            goto error;
        }
        zmsg_destroy (&zmsg);
        count++;
        if (count == zmqwriter_msgcount)
            flux_watcher_stop (w);
    }
    return;
error:
    flux_reactor_stop_error (r);
}

static void test_zmq (flux_reactor_t *reactor)
{
    zctx_t *zctx;
    void *zs[2];
    flux_watcher_t *r, *w;

    ok ((zctx = zctx_new ()) != NULL,
        "zmq: created zmq context");
    zs[0] = zsocket_new (zctx, ZMQ_PAIR);
    zs[1] = zsocket_new (zctx, ZMQ_PAIR);
    ok (zs[0] && zs[1]
        && zsocket_bind (zs[0], "inproc://test_zmq") == 0
        && zsocket_connect (zs[1], "inproc://test_zmq") == 0,
        "zmq: connected ZMQ_PAIR sockets over inproc");
    r = flux_zmq_watcher_create (reactor, zs[0], FLUX_POLLIN, zmqreader, NULL);
    w = flux_zmq_watcher_create (reactor, zs[1], FLUX_POLLOUT, zmqwriter, NULL);
    ok (r != NULL && w != NULL,
        "zmq: nonblocking reader and writer created");
    flux_watcher_start (r);
    flux_watcher_start (w);
    ok (flux_reactor_run  (reactor, 0) == 0,
        "zmq: reactor ran to completion after %d messages", zmqwriter_msgcount);
    flux_watcher_stop (r);
    flux_watcher_stop (w);
    flux_watcher_destroy (r);
    flux_watcher_destroy (w);

    zsocket_destroy (zctx, zs[0]);
    zsocket_destroy (zctx, zs[1]);
    zctx_destroy (&zctx);
}

static const size_t fdwriter_bufsize = 10*1024*1024;

static void fdwriter (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    int fd = flux_fd_watcher_get_fd (w);
    static char *buf = NULL;
    static int count = 0;
    int n;

    if (!buf)
        buf = xzmalloc (fdwriter_bufsize);
    if (revents & FLUX_POLLERR) {
        fprintf (stderr, "%s: FLUX_POLLERR is set\n", __FUNCTION__);
        goto error;
    }
    if (revents & FLUX_POLLOUT) {
        if ((n = write (fd, buf + count, fdwriter_bufsize - count)) < 0
                                && errno != EWOULDBLOCK && errno != EAGAIN) {
            fprintf (stderr, "%s: write failed: %s\n",
                     __FUNCTION__, strerror (errno));
            goto error;
        }
        if (n > 0) {
            count += n;
            if (count == fdwriter_bufsize) {
                flux_watcher_stop (w);
                free (buf);
            }
        }
    }
    return;
error:
    flux_reactor_stop_error (r);
}
static void fdreader (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    int fd = flux_fd_watcher_get_fd (w);
    static char *buf = NULL;
    static int count = 0;
    int n;

    if (!buf)
        buf = xzmalloc (fdwriter_bufsize);
    if (revents & FLUX_POLLERR) {
        fprintf (stderr, "%s: FLUX_POLLERR is set\n", __FUNCTION__);
        goto error;
    }
    if (revents & FLUX_POLLIN) {
        if ((n = read (fd, buf + count, fdwriter_bufsize - count)) < 0
                            && errno != EWOULDBLOCK && errno != EAGAIN) {
            fprintf (stderr, "%s: read failed: %s\n",
                     __FUNCTION__, strerror (errno));
            goto error;
        }
        if (n > 0) {
            count += n;
            if (count == fdwriter_bufsize) {
                flux_watcher_stop (w);
                free (buf);
            }
        }
    }
    return;
error:
    flux_reactor_stop_error (r);
}

static int set_nonblock (int fd)
{
    int flags = fcntl (fd, F_GETFL, NULL);
    if (flags < 0 || fcntl (fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        fprintf (stderr, "fcntl: %s\n", strerror (errno));
        return -1;
    }
    return 0;
}

static void test_fd (flux_reactor_t *reactor)
{
    int fd[2];
    flux_watcher_t *r, *w;

    ok (socketpair (PF_LOCAL, SOCK_STREAM, 0, fd) == 0
        && set_nonblock (fd[0]) == 0 && set_nonblock (fd[1]) == 0,
        "fd: successfully created non-blocking socketpair");
    r = flux_fd_watcher_create (reactor, fd[0], FLUX_POLLIN, fdreader, NULL);
    w = flux_fd_watcher_create (reactor, fd[1], FLUX_POLLOUT, fdwriter, NULL);
    ok (r != NULL && w != NULL,
        "fd: reader and writer created");
    flux_watcher_start (r);
    flux_watcher_start (w);
    ok (flux_reactor_run (reactor, 0) == 0,
        "fd: reactor ran to completion after %lu bytes", fdwriter_bufsize);
    flux_watcher_stop (r);
    flux_watcher_stop (w);
    flux_watcher_destroy (r);
    flux_watcher_destroy (w);
    close (fd[0]);
    close (fd[1]);
}

static int repeat_countdown = 10;
static void repeat (flux_reactor_t *r, flux_watcher_t *w,
                    int revents, void *arg)
{
    repeat_countdown--;
    if (repeat_countdown == 0)
        flux_watcher_stop (w);
}

static bool oneshot_ran = false;
static int oneshot_errno = 0;
static void oneshot (flux_reactor_t *r, flux_watcher_t *w,
                     int revents, void *arg)
{
    oneshot_ran = true;
    if (oneshot_errno != 0) {
        errno = oneshot_errno;
        flux_reactor_stop_error (r);
    }
}

static void test_timer (flux_reactor_t *reactor)
{
    flux_watcher_t *w;

    errno = 0;
    ok (!flux_timer_watcher_create (reactor, -1, 0, oneshot, NULL)
        && errno == EINVAL,
        "timer: creating negative timeout fails with EINVAL");
    ok (!flux_timer_watcher_create (reactor, 0, -1, oneshot, NULL)
        && errno == EINVAL,
        "timer: creating negative repeat fails with EINVAL");
    ok ((w = flux_timer_watcher_create (reactor, 0, 0, oneshot, NULL)) != NULL,
        "timer: creating zero timeout works");
    flux_watcher_start (w);
    ok (flux_reactor_run (reactor, 0) == 0,
        "timer: reactor ran to completion (single oneshot)");
    ok (oneshot_ran == true,
        "timer: oneshot was executed");
    oneshot_ran = false;
    ok (flux_reactor_run (reactor, 0) == 0,
        "timer: reactor ran to completion (expired oneshot)");
    ok (oneshot_ran == false,
        "timer: expired oneshot was not re-executed");

    errno = 0;
    oneshot_errno = ESRCH;
    flux_watcher_start (w);
    ok (flux_reactor_run (reactor, 0) < 0 && errno == ESRCH,
        "general: reactor stop_error worked with errno passthru");
    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    ok ((w = flux_timer_watcher_create (reactor, 0.01, 0.01, repeat, NULL))
        != NULL,
        "timer: creating 1ms timeout with 1ms repeat works");
    flux_watcher_start (w);
    ok (flux_reactor_run (reactor, 0) == 0,
        "timer: reactor ran to completion (single repeat)");
    ok (repeat_countdown == 0,
        "timer: repeat timer stopped itself after countdown");
    flux_watcher_stop (w);
    flux_watcher_destroy (w);
}

static void dummy (flux_t h, flux_msg_handler_t *w,
                   const flux_msg_t *msg, void *arg)
{
}

static void leak_msg_handler (void)
{
    flux_t h;
    flux_msg_handler_t *w;

    if (!(h = flux_open ("loop://", 0)))
        exit (1);
    if (!(w = flux_msg_handler_create (h, FLUX_MATCH_ANY, dummy, NULL)))
        exit (1);
    flux_msg_handler_start (w);
    flux_close (h);
}

static void reactor_destroy_early (void)
{
    flux_reactor_t *r;
    flux_watcher_t *w;

    if (!(r = flux_reactor_create ()))
        exit (1);
    if (!(w = flux_idle_watcher_create (r, NULL, NULL)))
        exit (1);
    flux_watcher_start (w);
    flux_reactor_destroy (r);
    flux_watcher_destroy (w);
}

static void fatal_err (const char *message, void *arg)
{
    BAIL_OUT ("fatal error: %s", message);
}

int main (int argc, char *argv[])
{
    flux_t h;
    flux_reactor_t *reactor;

    plan (4+11+3+4+3+5+2);

    (void)setenv ("FLUX_CONNECTOR_PATH", CONNECTOR_PATH, 0);
    ok ((h = flux_open ("loop://", 0)) != NULL,
        "opened loop connector");
    if (!h)
        BAIL_OUT ("can't continue without loop handle");
    flux_fatal_set (h, fatal_err, NULL);
    ok ((reactor = flux_get_reactor (h)) != NULL,
        "obtained reactor");
    if (!reactor)
        BAIL_OUT ("can't continue without reactor");

    ok (flux_reactor_run (reactor, 0) == 0,
        "general: reactor ran to completion (no watchers)");
    errno = 0;
    ok (flux_sleep_on (h, FLUX_MATCH_ANY) < 0 && errno == EINVAL,
        "general: flux_sleep_on outside coproc fails with EINVAL");

    test_timer (reactor); // 11
    test_fd (reactor); // 3
    test_zmq (reactor); // 4
    test_msg (h); // 3
    test_multmatch (h); // 5

    /* Misc
     */
    lives_ok ({ reactor_destroy_early ();},
        "destroying reactor then watcher doesn't segfault");
    lives_ok ({ leak_msg_handler ();},
        "leaking a msg_handler_t doesn't segfault");

    flux_close (h);
    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

