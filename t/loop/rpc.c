#include <czmq.h>
#include "src/common/libflux/message.h"
#include "src/common/libflux/handle.h"
#include "src/common/libflux/rpc.h"
#include "src/common/libflux/request.h"
#include "src/common/libflux/response.h"
#include "src/common/libflux/reactor.h"
#include "src/common/libflux/dispatch.h"

#include "src/common/libutil/shortjson.h"

#include "src/common/libtap/tap.h"

/* request nodeid and flags returned in response */
void rpctest_nodeid_cb (flux_t h, flux_msg_handler_t *w,
                        const flux_msg_t *msg, void *arg)
{
    int errnum = 0;
    uint32_t nodeid;
    JSON o = NULL;
    int flags;

    if (flux_request_decode (msg, NULL, NULL) < 0
            || flux_msg_get_nodeid (msg, &nodeid, &flags) < 0) {
        errnum = errno;
        goto done;
    }
    o = Jnew ();
    Jadd_int (o, "nodeid", nodeid);
    Jadd_int (o, "flags", flags);
done:
    (void)flux_respond (h, msg, errnum, Jtostr (o));
    Jput (o);
}

/* request payload echoed in response */
void rpctest_echo_cb (flux_t h, flux_msg_handler_t *w,
                      const flux_msg_t *msg, void *arg)
{
    int errnum = 0;
    const char *json_str;

    if (flux_request_decode (msg, NULL, &json_str) < 0) {
        errnum = errno;
        goto done;
    }
done:
    (void)flux_respond (h, msg, errnum, json_str);
}

/* no-payload response */
void rpctest_hello_cb (flux_t h, flux_msg_handler_t *w,
                       const flux_msg_t *msg, void *arg)
{
    int errnum = 0;

    if (flux_request_decode (msg, NULL, NULL) < 0) {
        errnum = errno;
        goto done;
    }
done:
    (void)flux_respond (h, msg, errnum, NULL);
}

void rpctest_begin_cb (flux_t h, flux_msg_handler_t *w,
                       const flux_msg_t *msg, void *arg)
{
    const char *json_str;
    flux_rpc_t *r;

    errno = 0;
    ok (!(r = flux_rpc (h, NULL, NULL, FLUX_NODEID_ANY, 0))
        && errno == EINVAL,
        "flux_rpc with NULL topic fails with EINVAL");

    /* working no-payload RPC */
    ok ((r = flux_rpc (h, "rpctest.hello", NULL, FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with no payload when none is expected works");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    ok (flux_rpc_get (r, NULL, NULL) == 0,
        "flux_rpc_get works");
    flux_rpc_destroy (r);

    /* cause remote EPROTO (unexpected payload) - will be picked up in _get() */
    ok ((r = flux_rpc (h, "rpctest.hello", "foo", FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with payload when none is expected works, at first");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    errno = 0;
    ok (flux_rpc_get (r, NULL, NULL) < 0
        && errno == EPROTO,
        "flux_rpc_get fails with EPROTO");
    flux_rpc_destroy (r);

    /* cause remote EPROTO (missing payload) - will be picked up in _get() */
    errno = 0;
    ok ((r = flux_rpc (h, "rpctest.echo", NULL, FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with no payload when payload is expected works, at first");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    errno = 0;
    ok (flux_rpc_get (r, NULL, NULL) < 0
        && errno == EPROTO,
        "flux_rpc_get fails with EPROTO");
    flux_rpc_destroy (r);

    /* working with-payload RPC */
    ok ((r = flux_rpc (h, "rpctest.echo", "foo", FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with payload when payload is expected works");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    json_str = NULL;
    ok (flux_rpc_get (r, NULL, &json_str) == 0
        && json_str && !strcmp (json_str, "foo"),
        "flux_rpc_get works and returned expected payload");
    flux_rpc_destroy (r);

    flux_reactor_stop (flux_get_reactor (h));
}

static void then_cb (flux_rpc_t *r, void *arg)
{
    flux_t h = arg;
    const char *json_str;

    ok (flux_rpc_check (r) == true,
        "flux_rpc_check says get won't block in then callback");
    json_str = NULL;
    ok (flux_rpc_get (r, NULL, &json_str) == 0
        && json_str && !strcmp (json_str, "xxx"),
        "flux_rpc_get works and returned expected payload in then callback");
    flux_reactor_stop (flux_get_reactor (h));
}

static flux_rpc_t *thenbug_r = NULL;
void rpctest_thenbug_cb (flux_t h, flux_msg_handler_t *w,
                         const flux_msg_t *msg, void *arg)
{
    (void)flux_rpc_check (thenbug_r);
    flux_reactor_stop (flux_get_reactor (h));
}

static void fatal_err (const char *message, void *arg)
{
    BAIL_OUT ("fatal error: %s", message);
}

static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,   "rpctest.begin",          rpctest_begin_cb},
    { FLUX_MSGTYPE_REQUEST,   "rpctest.hello",          rpctest_hello_cb},
    { FLUX_MSGTYPE_REQUEST,   "rpctest.echo",           rpctest_echo_cb},
    { FLUX_MSGTYPE_REQUEST,   "rpctest.nodeid",         rpctest_nodeid_cb},
    { FLUX_MSGTYPE_REQUEST,   "rpctest.thenbug",        rpctest_thenbug_cb},
    FLUX_MSGHANDLER_TABLE_END,
};

int main (int argc, char *argv[])
{
    flux_msg_t *msg;
    flux_t h;
    flux_reactor_t *reactor;

    plan (34);

    (void)setenv ("FLUX_CONNECTOR_PATH", CONNECTOR_PATH, 0);
    ok ((h = flux_open ("loop://", FLUX_O_COPROC)) != NULL,
        "opened loop connector");
    if (!h)
        BAIL_OUT ("can't continue without loop handle");
    flux_fatal_set (h, fatal_err, NULL);
    ok ((reactor = flux_get_reactor(h)) != NULL,
       "obtained reactor");
    if (!h)
        BAIL_OUT ("can't continue without reactor");

    ok (flux_msg_handler_addvec (h, htab, NULL) == 0,
        "registered message handlers");
    /* test continues in rpctest_begin_cb() so that rpc calls
     * can sleep while we answer them
     */
    ok ((msg = flux_request_encode ("rpctest.begin", NULL)) != NULL,
        "encoded rpctest.begin request OK");
    ok (flux_send (h, msg, 0) == 0,
        "sent rpctest.begin request");
    ok (flux_reactor_run (reactor, 0) == 0,
        "reactor completed normally");
    flux_msg_destroy (msg);

    /* test _then:  Slightly tricky.
     * Send request.  We're not in a coproc ctx here in main(), so there
     * will be no response, therefore, check will be false.  Register
     * continuation, start reactor.  Response will be received, continuation
     * will be invoked. Continuation stops the reactor.
    */
    flux_rpc_t *r;
    ok ((r = flux_rpc (h, "rpctest.echo", "xxx", FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with payload when payload is expected works");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    /* reg/unreg _then a couple times for fun */
    ok (flux_rpc_then (r, NULL, 0) == 0,
        "flux_rpc_then with NULL cb works");
    ok (flux_rpc_then (r, then_cb, h) == 0,
        "flux_rpc_then works after NULL");
    ok (flux_rpc_then (r, NULL, 0) == 0,
        "flux_rpc_then with NULL cb after non-NULL works");
    ok (flux_rpc_then (r, then_cb, h) == 0,
        "flux_rpc_then works");
    /* enough of that */
    ok (flux_reactor_run (reactor, 0) == 0,
        "reactor completed normally");
    flux_rpc_destroy (r);

    /* Test a _then corner case:
     * If _check() is called before _then(), a message may have been cached
     * in the flux_rpc_t.  rpctest_thenbug_cb creates this condition.
     * Next, _then continuation is installed, but will reactor call it?
     * This will hang if rpc implementation doesn't return a cached message
     * back to the handle in _then().  Else, continuation will stop reactor.
     */
    ok ((thenbug_r = flux_rpc (h, "rpctest.echo", "xxx",
        FLUX_NODEID_ANY, 0)) != NULL,
        "thenbug: sent echo request");
    do {
        if (!(msg = flux_request_encode ("rpctest.thenbug", NULL))
                  || flux_send (h, msg, 0) < 0
                  || flux_reactor_run (reactor, 0) < 0) {
            flux_msg_destroy (msg);
            break;
        }
        flux_msg_destroy (msg);
    } while (!flux_rpc_check (thenbug_r));
    ok (true,
        "thenbug: check says message ready");
    ok (flux_rpc_then (thenbug_r, then_cb, h) == 0,
        "thenbug: registered then - hangs on failure");
    ok (flux_reactor_run (reactor, 0) == 0,
        "reactor completed normally");
    flux_rpc_destroy (thenbug_r);

    flux_msg_handler_delvec (htab);
    flux_close (h);
    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

