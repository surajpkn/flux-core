#include <flux/core.h>
#include "src/common/libutil/log.h"

int main (int argc, char **argv)
{
    flux_t h;
    uint32_t rank;

    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");
    if (flux_get_rank (h, &rank) < 0)
        err_exit ("flux_get_rank");
    printf ("My rank is %d\n", (int)rank);
    flux_close (h);
    return (0);
}
