#include <flux/core.h>
#include "src/common/libutil/log.h"

int main (int argc, char **argv)
{
    flux_t h;
    flux_msg_t *msg;
    const char *topic;

    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");
    if (flux_event_subscribe (h, "") < 0)
        err_exit ("flux_event_subscribe");
    for (;;) {
        if ((msg = flux_recv (h, FLUX_MATCH_EVENT, 0)))
            err_exit ("flux_recv");
        if (flux_msg_get_topic (msg, &topic) < 0)
            err_exit ("flux_msg_get_topic");
        printf ("Event: %s\n", topic);
        flux_msg_destroy (msg);
    }
    flux_close (h);
    return (0);
}
