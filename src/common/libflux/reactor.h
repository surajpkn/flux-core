#ifndef _FLUX_CORE_REACTOR_H
#define _FLUX_CORE_REACTOR_H

#include <stdbool.h>

#include "handle.h"

/* Reactor
 */

typedef struct flux_reactor flux_reactor_t;

enum {
    FLUX_REACTOR_NOWAIT = 1,  /* return after all new and outstanding */
                              /*     events have been hnadled */
    FLUX_REACTOR_ONCE = 2,    /* same as above but block until at least */
                              /*     one event occurs */
};

flux_reactor_t *flux_reactor_create (void);
void flux_reactor_destroy (flux_reactor_t *r);

flux_reactor_t *flux_get_reactor (flux_t h);
void flux_set_reactor (flux_t h, flux_reactor_t *r);

int flux_reactor_run (flux_reactor_t *r, int flags);

void flux_reactor_stop (flux_reactor_t *r);
void flux_reactor_stop_error (flux_reactor_t *r);

/* Watchers
 */

typedef struct flux_watcher flux_watcher_t;

typedef void (*flux_watcher_f)(flux_reactor_t *r, flux_watcher_t *w,
                               int revents, void *arg);

void flux_watcher_start (flux_watcher_t *w);
void flux_watcher_stop (flux_watcher_t *w);
void flux_watcher_destroy (flux_watcher_t *w);

flux_watcher_t *flux_handle_watcher_create (flux_reactor_t *r,
                                            flux_t h, int events,
                                            flux_watcher_f cb, void *arg);
flux_t flux_handle_watcher_get_flux (flux_watcher_t *w);

flux_watcher_t *flux_fd_watcher_create (flux_reactor_t *r, int fd, int events,
                                        flux_watcher_f cb, void *arg);
int flux_fd_watcher_get_fd (flux_watcher_t *w);

flux_watcher_t *flux_zmq_watcher_create (flux_reactor_t *r,
                                         void *zsock, int events,
                                         flux_watcher_f cb, void *arg);
void *flux_zmq_watcher_get_zsock (flux_watcher_t *w);

flux_watcher_t *flux_timer_watcher_create (flux_reactor_t *r,
                                           double after, double repeat,
                                           flux_watcher_f cb, void *arg);

void flux_timer_watcher_reset (flux_watcher_t *w, double after, double repeat);


flux_watcher_t *flux_prepare_watcher_create (flux_reactor_t *r,
                                             flux_watcher_f cb, void *arg);

flux_watcher_t *flux_check_watcher_create (flux_reactor_t *r,
                                          flux_watcher_f cb, void *arg);

flux_watcher_t *flux_idle_watcher_create (flux_reactor_t *r,
                                          flux_watcher_f cb, void *arg);


#endif /* !_FLUX_CORE_REACTOR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
