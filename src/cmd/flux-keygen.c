/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <getopt.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"


#define OPTIONS "hfp"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"force",      no_argument,        0, 'f'},
    {"plain",      no_argument,        0, 'p'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr,
"Usage: flux-keygen [--force] [--plain]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    flux_sec_t sec;
    bool force = false;
    bool plain = false;
    char *secdir;

    log_init ("flux-keygen");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'f': /* --force */
                force = true;
                break;
            case 'p': /* --plain */
                plain = true;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind < argc)
        usage ();
    if (!(sec = flux_sec_create ()))
        err_exit ("flux_sec_create");
    if (!(secdir = getenv ("FLUX_SEC_DIRECTORY")))
        msg_exit ("FLUX_SEC_DIRECTORY is not set");
    flux_sec_set_directory (sec, secdir);
    if (plain && flux_sec_enable (sec, FLUX_SEC_TYPE_PLAIN) < 0)
        msg_exit ("PLAIN security is not available");
    if (flux_sec_keygen (sec, force, true) < 0)
        msg_exit ("%s", flux_sec_errstr (sec));
    flux_sec_destroy (sec);

    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
