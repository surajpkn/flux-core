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
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <stdbool.h>
#include <sys/param.h>
#include <glob.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/setenvf.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/optparse.h"


bool handle_internal (flux_conf_t cf, int ac, char *av[]);
void exec_subcommand (const char *searchpath, bool vopt, char *argv[]);
char *intree_confdir (void);
void setup_path (flux_conf_t cf, const char *argv0);

static void print_environment(flux_conf_t cf, const char * prefix);
void setup_broker_env (flux_conf_t cf, const char *path_override);

#define OPTIONS "+tx:hM:O:B:vc:L:P:C:FS:u:"
static const struct option longopts[] = {
    {"trace-handle",    no_argument,        0, 't'},
    {"exec-path",       required_argument,  0, 'x'},
    {"module-path",     required_argument,  0, 'M'},
    {"connector-path",  required_argument,  0, 'O'},
    {"broker-path",     required_argument,  0, 'B'},
    {"lua-path",        required_argument,  0, 'L'},
    {"python-path",     required_argument,  0, 'P'},
    {"lua-cpath",       required_argument,  0, 'C'},
    {"config",          required_argument,  0, 'c'},
    {"secdir",          required_argument,  0, 'S'},
    {"uri",             required_argument,  0, 'u'},
    {"verbose",         no_argument,        0, 'v'},
    {"file-config",     no_argument,        0, 'F'},
    {"help",            no_argument,        0, 'h'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr,
"Usage: flux [OPTIONS] COMMAND ARGS\n"
"    -x,--exec-path PATH   prepend PATH to command search path\n"
"    -M,--module-path PATH prepend PATH to module search path\n"
"    -O,--connector-path PATH   prepend PATH to connector search path\n"
"    -L,--lua-path PATH    prepend PATH to LUA_PATH\n"
"    -P,--python-path PATH prepend PATH to PYTHONPATH\n"
"    -C,--lua-cpath PATH   prepend PATH to LUA_CPATH\n"
"    -t,--trace-handle     set FLUX_HANDLE_TRACE=1 before executing COMMAND\n"
"    -B,--broker-path FILE override path to flux broker\n"
"    -c,--config DIR       set path to config directory\n"
"    -F,--file-config      force use of config file, even if FLUX_URI is set\n"
"    -S,--secdir DIR       set the directory where CURVE keys will be stored\n"
"    -u,--uri URI          override default URI to flux broker\n"
"    -v,--verbose          show FLUX_* environment and command search\n"
"\n"
"The flux-core commands are:\n"
"   help          Display manual for a sub-command\n"
"   keygen        Generate CURVE keypairs for session security\n"
"   start         Bootstrap a comms session interactively\n"
"   kvs           Access the Flux the key-value store\n"
"   module        Load/unload comms modules\n"
"   up            Show state of all broker ranks\n"
"   ping          Time round-trip RPC on the comms rank-request network\n"
"   mping         Time round-trip group RPC to the mecho comms module\n"
"   snoop         Snoop on local Flux message broker traffic\n"
"   event         Publish and subscribe to Flux events\n"
"   logger        Log a message to Flux logging system\n"
"   comms         Misc Flux comms session operations\n"
"   comms-stats   Display comms message counters, etc.\n"
"   topo          Display current comms topology using graphviz\n"
"   wreckrun      Execute a Flux lightweight job (LWJ)\n"
"   zio           Manipulate KVS streams (including LWJ stdio)\n"
"   config        Manipulate a Flux config file\n"
);
}


int main (int argc, char *argv[])
{
    int ch;
    bool vopt = false;
    char *xopt = NULL;
    char *Mopt = NULL;
    char *Popt = NULL;
    char *Oopt = NULL;
    char *Bopt = NULL;
    char *Lopt = NULL;
    char *Copt = NULL;
    bool Fopt = false;
    char *confdir = NULL;
    char *secdir = NULL;
    flux_conf_t cf;
    const char *searchpath;
    const char *argv0 = argv[0];

    log_init ("flux");

    cf = flux_conf_create ();

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'c': /* --config DIR */
                confdir = xstrdup (optarg);
                break;
            case 'S': /* --secdir DIR */
                secdir = optarg;
                break;
            case 'F': /* --file-config */
                Fopt = true;
                break;
            case 't': /* --trace-handle */
                if (setenv ("FLUX_HANDLE_TRACE", "1", 1) < 0)
                    err_exit ("setenv");
                flux_conf_environment_set (cf, "FLUX_HANDLE_TRACE", "1", "");
                break;
            case 'M': /* --module-path PATH */
                Mopt = optarg;
                break;
            case 'O': /* --connector-path PATH */
                Oopt = optarg;
                break;
            case 'x': /* --exec-path PATH */
                xopt = optarg;
                break;
            case 'B': /* --broker-path PATH */
                Bopt = optarg;
                break;
            case 'v': /* --verbose */
                vopt = true;
                break;
            case 'L': /* --lua-path PATH */
                Lopt = optarg;
                break;
            case 'P': /* --python-path PATH */
                Popt = optarg;
                break;
            case 'C': /* --lua-cpath PATH */
                Copt = optarg;
                break;
            case 'u': /* --uri URI */
                if (setenv ("FLUX_URI", optarg, 1) < 0)
                    err_exit ("setenv");
                flux_conf_environment_set(cf, "FLUX_URI", optarg, "");
                break;
            case 'h': /* --help  */
                usage ();
                exit (0);
                break;
            default:
                usage ();
                exit (1);
        }
    }
    argc -= optind;
    argv += optind;

    /* Set the config directory and pass it to sub-commands.
    */
    if (confdir || (confdir = intree_confdir ()))
        flux_conf_set_directory (cf, confdir);
    flux_conf_environment_set (cf, "FLUX_SEC_DIRECTORY",
                     secdir ? secdir : flux_conf_get_directory (cf), "");
    flux_conf_environment_unset (cf, "FLUX_CONF_USEFILE");

    /* Process config from the KVS if not a bootstrap instance, and not
     * forced to use a config file by the command line.
     * It is not an error if config is not foud in either place, we will
     * try to make do with compiled-in defaults.
     */
    if (!Fopt && getenv ("FLUX_URI")
              && !(argc > 0 && !strcmp (argv[0], "start"))) {
        flux_t h;
        flux_conf_load (cf);
        if (!(h = flux_open (NULL, 0)))     /*   esp. for in-tree */
            err_exit ("flux_open");
        if (kvs_conf_load (h, cf) < 0 && errno != ENOENT)
            err_exit ("could not load config from KVS");
        flux_close (h);
    } else {
        if (flux_conf_load (cf) == 0) {
            flux_conf_environment_set (cf, "FLUX_CONF_USEFILE", "1", "");
        } else if (errno != ENOENT || Fopt)
            err_exit ("%s", flux_conf_get_directory (cf));
    }

    /* We share a few environment variables with sub-commands, so
     * that they don't have to reprocess the config.
     */
    setup_broker_env (cf, Bopt);    /* sets FLUX_BROKER_PATH */

    /* Add PATH to flux_conf_environment and prepend path to
     *  this executable if necessary.
     */
    setup_path (cf, argv0);

    /* Add config items to environment variables */
    /* NOTE: I would prefer that this be in config, but kvs_load loads
     * everything out of band, preventing that */
    flux_conf_environment_push (cf, "FLUX_CONNECTOR_PATH", flux_conf_get(cf, "general.connector_path"));
    flux_conf_environment_push (cf, "FLUX_EXEC_PATH",      flux_conf_get(cf, "general.exec_path"));
    flux_conf_environment_push (cf, "FLUX_MODULE_PATH",    flux_conf_get(cf, "general.module_path"));
    flux_conf_environment_push (cf, "LUA_CPATH",           flux_conf_get(cf, "general.lua_cpath"));
    flux_conf_environment_push (cf, "LUA_PATH",            flux_conf_get(cf, "general.lua_path"));
    flux_conf_environment_push (cf, "PYTHONPATH",          flux_conf_get(cf, "general.python_path"));

    /* Prepend to command-line environment variables */
    flux_conf_environment_push (cf, "FLUX_CONNECTOR_PATH", Oopt);
    flux_conf_environment_push (cf, "FLUX_EXEC_PATH", xopt);
    flux_conf_environment_push (cf, "FLUX_MODULE_PATH", Mopt);
    flux_conf_environment_push (cf, "LUA_CPATH", Copt);
    flux_conf_environment_push (cf, "LUA_PATH", Lopt);
    flux_conf_environment_push (cf, "PYTHONPATH", Popt);

    if (argc == 0) {
        usage ();
        exit (1);
    }

    flux_conf_environment_apply(cf);

    if (vopt)
        print_environment (cf, "");
    if (!handle_internal (cf, argc, argv)) {
        searchpath = flux_conf_environment_get(cf, "FLUX_EXEC_PATH");
        if (vopt)
            printf ("sub-command search path: %s\n", searchpath);
        exec_subcommand (searchpath, vopt, argv);
    }

    free (confdir);
    flux_conf_destroy (cf);
    log_fini ();

    return 0;
}

/*  Strip trailing ".libs", otherwise do nothing
 */
char *strip_trailing_dot_libs (char *dir)
{
    char *p = dir + strlen (dir) - 1;
    if (   (*(p--) == 's')
        && (*(p--) == 'b')
        && (*(p--) == 'i')
        && (*(p--) == 'l')
        && (*(p--) == '.')
        && (*p == '/') )
        *p = '\0';
    return (dir);
}

/*  Return directory containing this executable.  Caller must free.
 *   (using non-portable /proc/self/exe support for now)
 *   NOTE: build tree .libs directory stripped from path if found.
 */
char *dir_self (void)
{
    static char  flux_exe_path [MAXPATHLEN];
    static char *flux_exe_dir;
    static bool exe_path_valid = false;
    if (!exe_path_valid) {
        memset (flux_exe_path, 0, MAXPATHLEN);
        if (readlink ("/proc/self/exe", flux_exe_path, MAXPATHLEN - 1) < 0)
            err_exit ("readlink (/proc/self/exe)");
        flux_exe_dir = strip_trailing_dot_libs (dirname (flux_exe_path));
        exe_path_valid = true;
    }
    return xstrdup (flux_exe_dir);
}

char *intree_confdir (void)
{
    char *confdir = NULL;
    char *selfdir = dir_self ();

    if (strcmp (selfdir, X_BINDIR) != 0){
        confdir = xasprintf ("%s/../../etc/flux", selfdir);
    }
    free (selfdir);
    return confdir;
}

/*
 * If flux command was run with relative or absolute path, then
 *  prepend the directory for the flux executable to PATH. This
 *  ensures that in "flux [OPTIONS] [COMMAND] flux" the second
 *  flux executable is the same as the first. This is important
 *  for example with "flux start".
 */
void setup_path (flux_conf_t cf, const char *argv0)
{
    char *selfdir;
    assert (argv0);

    /*  If argv[0] was explicitly "flux" then assume PATH is already set */
    if (strcmp (argv0, "flux") == 0)
        return;
    flux_conf_environment_from_env (cf, "PATH", "/bin:/usr/bin", ":");
    selfdir = dir_self ();
    flux_conf_environment_push (cf, "PATH", selfdir);
    free (selfdir);
}


void setup_broker_env (flux_conf_t cf, const char *path_override)
{
    const char *cf_path = flux_conf_get (cf, "general.broker_path");
    const char *path = path_override;

    if (!path)
        path = cf_path;
    if (!path)
        path = BROKER_PATH;
    flux_conf_environment_set(cf, "FLUX_BROKER_PATH", path, "");
}

void exec_subcommand_dir (bool vopt, const char *dir, char *argv[],
        const char *prefix)
{
    char *path = xasprintf ("%s%s%s%s",
            dir ? dir : "",
            dir ? "/" : "",
            prefix ? prefix : "", argv[0]);
    if (vopt)
        msg ("trying to exec %s", path);
    execvp (path, argv); /* no return if successful */
    free (path);
}

void exec_subcommand (const char *searchpath, bool vopt, char *argv[])
{
    if (strchr (argv[0], '/')) {
        exec_subcommand_dir (vopt, NULL, argv, NULL);
        err_exit ("%s", argv[0]);
    } else {
        char *cpy = xstrdup (searchpath);
        char *dir, *saveptr = NULL, *a1 = cpy;

        while ((dir = strtok_r (a1, ":", &saveptr))) {
            exec_subcommand_dir (vopt, dir, argv, "flux-");
            a1 = NULL;
        }
        free (cpy);
        msg_exit ("`%s' is not a flux command.  See 'flux --help'", argv[0]);
    }
}

optparse_t internal_cmd_optparse_create (const char *cmd)
{
    optparse_t p = optparse_create (cmd);
    if (!p)
        err_exit ("%s: optparse_create", cmd);
    return (p);
}

void internal_help (flux_conf_t cf, optparse_t p, int ac, char *av[])
{
    int n = 1;
    char *cmd;

    if ((n = optparse_parse_args (p, ac, av)) < 0)
        msg_exit ("flux-help: error processing args");

    if (n < ac) {
        const char *cf_path = flux_conf_get (cf, "general.man_path");
        const char *topic = av [n];
        if (cf_path)
            setenvf ("MANPATH", 1, "%s:%s", cf_path, MANDIR);
        else
            setenv ("MANPATH", MANDIR, 1);
        cmd = xasprintf ("man flux-%s %s", topic, topic);
        if (system (cmd) < 0)
            err_exit ("man");
        free (cmd);
    } else
        usage ();
}

static void print_environment(flux_conf_t cf, const char * prefix)
{
    const char *key, *value;
    for (value = (char*)flux_conf_environment_first(cf), key = (char*)flux_conf_environment_cursor(cf);
            value != NULL;
            value = flux_conf_environment_next(cf), key = flux_conf_environment_cursor(cf)) {
        printf("%s%s=%s\n", prefix, key, value);
    }
    fflush(stdout);
}

void internal_env (flux_conf_t cf, optparse_t p, int ac, char *av[])
{
    int n = 1;

    if ((n = optparse_parse_args (p, ac, av)) < 0)
        msg_exit ("flux-env: error processing args");

    if (av && av[n]) {
        execvp (av[n], av+n); /* no return if successful */
        err_exit("execvp (%s)", av[n]);
    } else
        print_environment(cf, "");
}

void internal_broker (flux_conf_t cf, optparse_t p, int ac, char *av[])
{
    const char *path = flux_conf_environment_get(cf, "FLUX_BROKER_PATH");
    execvp (path, av); /* no return if successful */
}

void internal_dmesg (flux_conf_t cf, optparse_t p, int ac, char *av[])
{
    struct optparse_option opts[] = {
        { .name = "clear",  .key = 'C',  .has_arg = 0,
          .usage = "Clear the ring buffer", },
        { .name = "read-clear",  .key = 'c',  .has_arg = 0,
          .usage = "Clear the ring buffer contents after printing", },
        { .name = "follow",  .key = 'f',  .has_arg = 0,
          .usage = "Track new entries as are logged", },
        OPTPARSE_TABLE_END,
    };
    int n;
    flux_t h;
    int flags = 0;
    flux_log_f print_cb = flux_log_fprint;

    if (optparse_add_option_table (p, opts) != OPTPARSE_SUCCESS)
        msg_exit ("optparse_add_option_table");
    if ((n = optparse_parse_args (p, ac, av)) < 0)
        msg_exit ("flux-dmesg: error processing args");
    if (n != ac)
        msg_exit ("flux-dmesg accepts no free arguments");

    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");
    if (optparse_hasopt (p, "read-clear") || optparse_hasopt (p, "clear"))
        flags |= FLUX_DMESG_CLEAR;
    if (optparse_hasopt (p, "clear"))
        print_cb = NULL;
    if (optparse_hasopt (p, "follow"))
        flags |= FLUX_DMESG_FOLLOW;
    if (flux_dmesg (h, flags, print_cb, stdout) < 0)
        err_exit ("flux_dmesg");
    flux_close (h);
}

void internal_getattr (flux_conf_t cf, optparse_t p, int ac, char *av[])
{
    int n;

    if ((n = optparse_parse_args (p, ac, av)) < 0)
        msg_exit ("flux-getattr: error processing args");
    if (n == ac - 1) {
        const char *val;
        int flags;
        flux_t h = flux_open (NULL, 0);
        if (!h)
            err_exit ("flux_open");
        if (!(val = flux_attr_get (h, av[n], &flags)))
            err_exit ("%s", av[n]);
        printf ("%s\n", val);
        flux_close (h);
    } else {
        usage ();
    }
}

void internal_setattr (flux_conf_t cf, optparse_t p, int ac, char *av[])
{
    struct optparse_option opts[] = {
        { .name = "expunge",  .key = 'e',  .has_arg = 0,
          .usage = "Unset the specified attribute", },
        OPTPARSE_TABLE_END,
    };
    int n;
    const char *name = NULL, *val = NULL;
    flux_t h;

    if (optparse_add_option_table (p, opts) != OPTPARSE_SUCCESS)
        msg_exit ("optparse_add_option_table");
    if ((n = optparse_parse_args (p, ac, av)) < 0)
        msg_exit ("flux-setattr: error processing args");
    if (optparse_hasopt (p, "expunge") && n == ac - 1) {
        name = av[n];
    } else if (!optparse_hasopt (p, "expunge") && n == ac - 2) {
        name = av[n];
        val = av[n + 1];
    } else
        usage ();
    if (!(h = flux_open (NULL, 0)))
            err_exit ("flux_open");
    if (flux_attr_set (h, name, val) < 0)
        err_exit ("%s", av[1]);
    flux_close (h);
}

void internal_lsattr (flux_conf_t cf, optparse_t p, int ac, char *av[])
{
    struct optparse_option opts[] = {
        { .name = "values",  .key = 'v',  .has_arg = 0,
          .usage = "List values with attributes", },
        OPTPARSE_TABLE_END,
    };
    int n;

    if (optparse_add_option_table (p, opts) != OPTPARSE_SUCCESS)
        msg_exit ("optparse_add_option_table");
    if ((n = optparse_parse_args (p, ac, av)) < 0)
        msg_exit ("flux-setattr: error processing args");
    if (n == ac) {
        const char *name, *val;
        flux_t h = flux_open (NULL, 0);
        if (!h)
            err_exit ("flux_open");
        name = flux_attr_first (h);
        while (name) {
            if (optparse_hasopt (p, "values")) {
                val = flux_attr_get (h, name, NULL);
                printf ("%-20s%s\n", name, val ? val : "-");
            } else {
                printf ("%s\n", name);
            }
            name = flux_attr_next (h);
        }
        flux_close (h);
    } else {
        usage ();
    }
}

struct builtin {
    const char *name;
    const char *doc;
    const char *usage;
    void       (*fn) (flux_conf_t, optparse_t, int ac, char *av[]);
};

struct builtin builtin_cmds [] = {
    {
      "help",
      "Display help information for flux commands",
      "[OPTIONS...] [COMMAND]",
      internal_help
    },
    {
      "env",
      "Print the flux environment or execute COMMAND inside it",
      "[OPTIONS...] [COMMAND...]",
      internal_env
    },
    {
      "broker",
      "Run the flux broker",
      "[OPTIONS...] [COMMAND...]",
      internal_broker
    },
    {
      "getattr",
      "Get broker attribute",
      "name",
      internal_getattr
    },
    {
      "setattr",
      "Set broker attribute",
      "name value",
      internal_setattr
    },
    {
      "lsattr",
      "List broker attributes",
      "[-v]",
      internal_lsattr
    },
    {
      "dmesg",
      "Print or control log ring buffer",
      "[OPTIONS...]",
      internal_dmesg
    },
    { NULL, NULL, NULL, NULL },
};


void run_builtin (struct builtin *cmd, flux_conf_t cf, int ac, char *av[])
{
    optparse_t p;
    char prog [66] = "flux-";

    /* cat command name onto 'flux-' prefix to get program name for
     *   help output (truncate if we have a ridiculously long cmd name)
     */
    strncat (prog, cmd->name, sizeof (prog) - 6);

    if ((p = optparse_create (prog)) == NULL)
        err_exit ("optparse_create (%s)", prog);

    if (cmd->usage)
        optparse_set (p, OPTPARSE_USAGE, cmd->usage);
    if (cmd->doc)
        optparse_add_doc (p, cmd->doc, -1);

    /* Run builtin: */
    if (!cmd->fn)
        msg_exit ("Error: builtin %s has no registered function!", prog);
    (*cmd->fn) (cf, p, ac, av);

    optparse_destroy (p);
}



bool handle_internal (flux_conf_t cf, int ac, char *av[])
{
    struct builtin *cmd = &builtin_cmds [0];
    while (cmd->name) {
        if (strcmp (av[0], cmd->name) == 0) {
            run_builtin (cmd, cf, ac, av);
            return true;
        }
        cmd++;
    }
    return false;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
