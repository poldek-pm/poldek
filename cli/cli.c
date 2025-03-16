/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <argp.h>

#include <trurl/trurl.h>

#include "compiler.h"
#include <sigint/sigint.h>
#include "pkgdir/pkgdir.h"
#include "i18n.h"
#include "log.h"
#include "conf.h"
#include "cli.h"
#include "cmd_chain.h"
#include "cmd_pipe.h"

static unsigned argp_parse_flags = ARGP_NO_EXIT;

static int command_cmp(struct poclidek_cmd *c1, struct poclidek_cmd *c2);

extern struct poclidek_cmd command_ls;
extern struct poclidek_cmd command_install;
extern struct poclidek_cmd command_uninstall;
extern struct poclidek_cmd command_get;
extern struct poclidek_cmd command_search;
extern struct poclidek_cmd command_desc;
extern struct poclidek_cmd command_cd;
extern struct poclidek_cmd command_pwd;
extern struct poclidek_cmd command_external;
extern struct poclidek_cmd command_help;
extern struct poclidek_cmd command_alias;
extern struct poclidek_cmd command_reload;
extern struct poclidek_cmd command_clean;
extern struct poclidek_cmd command_pull;

static struct poclidek_cmd *commands_tab[] = {
    &command_ls,
    &command_install,
    &command_uninstall,
    &command_search,
    &command_desc,
    &command_get,
    &command_pull,
    &command_clean,
    &command_cd,
    &command_pwd,
    &command_external,
    &command_help,
    &command_alias,
    &command_reload,
    NULL
};


/* default parse_opt */
static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdctx *cmdctx = state->input;
    int rc = 0;

    switch (key) {
        case ARGP_KEY_INIT:
            state->child_inputs[0] = cmdctx;
            state->child_inputs[1] = NULL;
            break;

        case 'v': {
            if ((cmdctx->cmd->flags & COMMAND_HASVERBOSE) == 0) {
                argp_usage (state);
                cmdctx->rtflags |= CMDCTX_ERR;

            } else {
                poldek_up_verbose();
            }
        }
        break;

        case 'q':
            cmdctx->rtflags |= CMDCTX_NOCTRLMSGS;
            poldek_set_verbose(-1);
            break;


        case ARGP_KEY_ARG:
            DBGF("cli.arg %s, %d\n", arg,
                 cmdctx->cmd->flags & COMMAND_SELFARGS);
            if (cmdctx->cmd->flags & COMMAND_SELFARGS)
                return ARGP_ERR_UNKNOWN;


            if (cmdctx->cmd->flags & COMMAND_NOARGS) {
                argp_usage (state);
                cmdctx->rtflags |= CMDCTX_ERR;
                return EINVAL;
            }
            poldek_ts_add_pkgmask(cmdctx->ts, arg);
            break;

        case 'h':
            argp_state_help(state, stdout, ARGP_HELP_LONG | ARGP_HELP_USAGE);
            return EINVAL;
            break;

        case ARGP_KEY_NO_ARGS:
            DBGF("NOARGS %d\n",
                 cmdctx->rtflags & (CMDCTX_ISHELP|CMDCTX_GOTARGS));
            if (cmdctx->rtflags & (CMDCTX_ISHELP | CMDCTX_GOTARGS))
                break;

            if ((cmdctx->cmd->flags & (COMMAND_NOARGS|COMMAND_EMPTYARGS)) == 0) {
                argp_usage (state);
                cmdctx->rtflags |= CMDCTX_ERR;
                return EINVAL;
            }
            break;


        case ARGP_KEY_ERROR:
            cmdctx->rtflags |= CMDCTX_ERR;
            return EINVAL;
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }

    //printf("key = %d, rc = %d\n", key, rc);
    return rc;
}

static tn_array *find_command_aliases(struct poclidek_cmd *cmd,
                                      struct poclidek_ctx  *cctx)
{
    char nam[PATH_MAX];
    tn_array *aliases = NULL;
    int i, n;

    if (cmd->flags & COMMAND_IS_ALIAS)
        return NULL;

    n = n_snprintf(nam, sizeof(nam), "%s ", cmd->name);
    for (i=0; i < n_array_size(cctx->commands); i++) {
        struct poclidek_cmd *cm = n_array_nth(cctx->commands, i);
        int found = 0;

        if ((cm->flags & COMMAND_IS_ALIAS) == 0)
            continue;

        if (cm->aliasto && n_str_eq(cmd->name, cm->aliasto))
            found = 1;

        else if (strncmp(nam, cm->cmdline, n) == 0)
            found = 1;

        if (found) {
            if (aliases == NULL)
                aliases = n_array_new(4, NULL, (tn_fn_cmp)command_cmp);
            n_array_push(aliases, cm);
        }
    }

    if (aliases)
        n_array_sort(aliases);
    return aliases;
}

static char *help_filter(int key, const char *text, void *input)
{
    struct cmdctx *cmdctx = input;
    char *p, buf[4096];
    tn_array *aliases;
    int n = 0;

#if 0
    printf("HELP_FILTER %d [%s]\n", key, text);

    if (key == ARGP_KEY_HELP_POST_DOC) {
        return strdup("POST DOC");
    }

    if (key == ARGP_KEY_HELP_PRE_DOC) {
        return strdup("PRE DOC");
    }

    if (key == ARGP_KEY_HELP_ARGS_DOC) {
        return strdup("ARGP_KEY_HELP_ARGS_DOC");
    }
#endif

    if (key == ARGP_KEY_HELP_PRE_DOC && cmdctx->cmd->_sys_alias) {
        const char *a = cmdctx->cmd->arg ? cmdctx->cmd->arg : "";
        char op[128];

        if (cmdctx->cmd->argp_opts) {
            snprintf(op, sizeof(op), _("[OPTION...] %s"), a);
            a = op;
        }

        n += n_snprintf(&buf[n], sizeof(buf) - n, "       %s %s\n",
                        cmdctx->cmd->_sys_alias, op);
        p = n_malloc(n + 1);
        return memcpy(p, buf, n + 1);

    }

    if (key != ARGP_KEY_HELP_EXTRA) {
        return (char*)text;
    }

    if (cmdctx->cmd->extra_help)
        n += n_snprintf(&buf[n], sizeof(buf) - n, "  %s\n",
                        cmdctx->cmd->extra_help);

    aliases = find_command_aliases(cmdctx->cmd, cmdctx->cctx);
    if (aliases) {
        int i = 0;

        n += n_snprintf(&buf[n], sizeof(buf) - n, "%s",
                        _("  Defined aliases:\n"));

        for (i=0; i < n_array_size(aliases); i++) {
            struct poclidek_cmd *al = n_array_nth(aliases, i);
            n += n_snprintf(&buf[n], sizeof(buf) - n, "    %-16s  \"%s\"\n",
                            al->name, al->cmdline);
        }
        n_array_free(aliases);
    }

    if (n > 0) {
        p = n_malloc(n + 1);
        return memcpy(p, buf, n + 1);
    }

    return (char*)text;
}

static struct argp_option common_options[] = {
    {0,  'v', 0, 0, N_("Be verbose."), 6000, },
    {0,  'q', 0, 0, N_("Be quiet"), 6000, },
    {0,  'h', 0, OPTION_HIDDEN, N_("Help"), 6000, },
    { 0, 0, 0, 0, 0, 0 },
};


static int do_exec_cmd_ent(struct cmdctx *cmdctx, int argc, char **argv)
{
    int                  rc = 1;
    unsigned             parse_flags;
    struct poclidek_cmd  *cmd = cmdctx->cmd;
    struct argp          cmd_argp = { cmd->argp_opts, cmd->parse_opt_fn,
                                      cmd->arg,
                                      cmd->doc, 0, 0, 0};

    struct argp_child cmd_argp_child[2] = { { &cmd_argp, 0, NULL, 0 },
                                            { NULL, 0, NULL, 0 },   };

    struct argp argp = { common_options, parse_opt, 0, 0,
                         cmd_argp_child, 0, 0 };


    if (argv == NULL)
        return 0;

    cmd = cmdctx->cmd;
    if (poclidek_argv_is_help(argc, (const char**)argv)) {
        cmdctx->rtflags |= CMDCTX_ISHELP;
        DBGF("is_help!\n");
    }

    if (poldek_verbose() < 0)
        cmdctx->rtflags |= CMDCTX_NOCTRLMSGS;

    cmdctx->_data = NULL;
    if (cmd->init_cmd_arg_d)
        cmdctx->_data = cmd->init_cmd_arg_d();

    if (cmd->cmd_fn) { /* option parses its args itself */
        DBGF("run cmd_fn(arc, argv)\n");
        rc = cmd->cmd_fn(cmdctx, argc, (const char**)argv, &argp);
        goto l_end;
    }


    if ((cmd->flags & COMMAND_NOHELP) && (cmd->flags & COMMAND_NOARGS) &&
        (cmd->flags & COMMAND_NOOPTS)) {
        rc = cmd->do_cmd_fn(cmdctx);
        goto l_end;
    }

    argp.help_filter = help_filter;
    parse_flags = argp_parse_flags;
    argp_parse(&argp, argc, (char**)argv, parse_flags, 0, cmdctx);

    if (cmdctx->rtflags & CMDCTX_ERR) {
        rc = 0;
        goto l_end;
    }

    if (cmdctx->rtflags & CMDCTX_ISHELP) {
        rc = 1;
        goto l_end;
    }

    rc = cmd->do_cmd_fn(cmdctx);

 l_end:
    if (cmd->destroy_cmd_arg_d && cmdctx->_data)
        cmd->destroy_cmd_arg_d(cmdctx->_data);

    return rc;
}

/* argp workaround */
int poclidek_argv_is_help(int argc, const char **argv)
{
    int i, is_help = 0;

    for (i=0; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-?") == 0 ||
            strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--usage") == 0) {

            is_help = 1;
            break;
        }
    }
    return is_help;
}


static int cmdctx_isctrlmsg(const char *fmt)
{
    return *fmt == '!';
}

static int do_cmdctx_printf(struct cmdctx *cmdctx, int color, const char *fmt,
                            va_list args)
{
    int is_piped = cmdctx->pipe_right != NULL;
    int is_ctrl, n = 0;

    if ((is_ctrl = cmdctx_isctrlmsg(fmt))) {
        if (cmdctx->rtflags & CMDCTX_NOCTRLMSGS)
            return 0;
        fmt++;
    }

    if (is_piped && is_ctrl) {  /* skip control messages */
        return 0;

    } else if (!is_piped) {
        n = color ? poldek_term_vprintf_c(color, fmt, args) : vfprintf(stdout, fmt, args);

    } else if (is_piped) {
        n = cmd_pipe_vprintf(cmdctx->pipe_right, fmt, args);

    } else {
        n_assert(0);
    }

    return n;
}

int cmdctx_printf(struct cmdctx *cmdctx, const char *fmt, ...)
{
    va_list args;
    int n;

    va_start(args, fmt);
    n = do_cmdctx_printf(cmdctx, 0, fmt, args);
    va_end(args);

    return n;
}

int cmdctx_printf_c(struct cmdctx *cmdctx, int color, const char *fmt, ...)
{
    va_list args;
    int n;

    va_start(args, fmt);
    n = do_cmdctx_printf(cmdctx, color, fmt, args);
    va_end(args);

    return n;
}

int cmdctx_addtoresult(struct cmdctx *cmdctx, struct pkg *pkg)
{
    if (cmdctx->pipe_right)
        return cmd_pipe_writepkg(cmdctx->pipe_right, pkg);
    return 1;
}

static int command_cmp(struct poclidek_cmd *c1, struct poclidek_cmd *c2)
{
    return strcmp(c1->name, c2->name);
}

int poclidek_cmd_ncmp(struct poclidek_cmd *c1, struct poclidek_cmd *c2)
{
    return strncmp(c1->name, c2->name, strlen(c2->name));
}

static void command_free(struct poclidek_cmd *cmd)
{
    if (cmd->_free)
        cmd->_free(cmd);
}

static void translate_argp_options(struct argp_option *arr)
{
    int i = 0;

    while (arr[i].doc) {
        arr[i].doc = _(arr[i].doc);
        if (arr[i].arg)
            arr[i].arg = _(arr[i].arg);
        i++;
    }
}

int poclidek_add_command(struct poclidek_ctx *cctx, struct poclidek_cmd *cmd)
{
    cmd->_seqno = n_array_size(cctx->commands);
    if (cmd->argp_opts)
        translate_argp_options(cmd->argp_opts);

    if (cmd->arg)
        cmd->arg = _(cmd->arg);

    if (cmd->doc)
        cmd->doc = _(cmd->doc);

    if (n_array_bsearch(cctx->commands, cmd)) {
        logn(LOGERR, _("ambiguous command %s"), cmd->name);
        return 0;
    }

    if (cmd->_sys_alias) {
        struct poclidek_cmd *a = n_malloc(sizeof(*a));
        memcpy(a, cmd, sizeof(*a));
        a->flags |= COMMAND_SYSALIAS | COMMAND__MALLOCED;
        a->name = cmd->_sys_alias;
        a->_sys_alias = NULL;
        a->_free = (void (*)(struct poclidek_cmd *))n_free;
        n_array_push(cctx->commands, a);
    }

    n_array_push(cctx->commands, cmd);
    n_array_sort(cctx->commands);
    return 1;
}

static void init_commands(struct poclidek_ctx *cctx, tn_hash *global_aliases,
                          tn_hash *local_aliases)
{
    int n = 0;

    cctx->commands = n_array_new(16, (tn_fn_free)command_free,
                                 (tn_fn_cmp)command_cmp);

    n_array_ctl(cctx->commands, TN_ARRAY_AUTOSORTED);
    while (commands_tab[n] != NULL) {
        struct poclidek_cmd *cmd = commands_tab[n++];
        poclidek_add_command(cctx, cmd);
    }
    n_array_sort(cctx->commands);

    n = 0;
    if (global_aliases)
        n += poclidek__add_aliases(cctx, global_aliases);

    if (local_aliases)
        n += poclidek__add_aliases(cctx, local_aliases);

    if (n == 0)
        poclidek__load_aliases(cctx); /* load legacy config(s) */

    n_array_sort(cctx->commands);
}

static void *dent_alloc(struct poclidek_ctx *cctx, size_t size)
{
    return cctx->_dent_na->na_malloc(cctx->_dent_na, size);
}

static int config_path(char *globalpath, char *localpath, int size)
{
    char *homedir, *sysconfdir = "/etc";

#ifdef SYSCONFDIR
    if (n_str_ne(sysconfdir, SYSCONFDIR) && access(SYSCONFDIR, R_OK) == 0)
        sysconfdir = SYSCONFDIR;
#endif
    *localpath = *globalpath = '\0';

    n_snprintf(globalpath, size, "%s/poldek/cli.conf", sysconfdir);
    if (access(globalpath, R_OK) != 0)
        *globalpath = '\0';

    /* TODO: use XDG_CONFIG_HOME */
    if ((homedir = getenv("HOME")) != NULL) {
        n_snprintf(localpath, size, "%s/.poclidekrc", homedir);
        if (access(localpath, R_OK) != 0) {
            n_snprintf(localpath, size, "%s/.poldekclirc", homedir);
            if (access(localpath, R_OK) != 0)
                *localpath = '\0';
        }
    }

    DBGF("%s\n", sysconfdir);

    return *localpath != '\0' || *globalpath != '\0';
}

static tn_hash *loadconf(void)
{
    char path[PATH_MAX], localpath[PATH_MAX];
    tn_hash *htcnf = NULL, *local_htcnf = NULL;

    if (!config_path(path, localpath, sizeof(path)))
        return NULL;

    if (*path)
        htcnf = poldek_conf_load(path, POLDEK_LDCONF_FOREIGN);

    if (*localpath)
        local_htcnf = poldek_conf_load(localpath, POLDEK_LDCONF_FOREIGN);

    if (htcnf == NULL) {
        htcnf = local_htcnf;
        local_htcnf = NULL;
    }

    if (htcnf && local_htcnf) {
        tn_array *aliases = poldek_conf_get_sections(htcnf, "aliases");
        if (aliases) /* XXX: yep, hacky a bit; sections should be movable
                        via conf API */
            n_hash_insert(local_htcnf, "global_aliases", n_ref(aliases));

        n_hash_free(htcnf);
        htcnf = local_htcnf;
        local_htcnf = NULL;
    }

    return htcnf;
}

static int poclidek_init(struct poclidek_ctx *cctx, struct poldek_ctx *ctx)
{
    tn_hash *htcnf;

    n_assert(cctx->ctx == NULL);

    cctx->_flags = 0;
    cctx->ctx = poldek_link(ctx);
    cctx->pkgs_available = NULL;
    cctx->pkgs_installed = NULL;
    cctx->_dent_na = n_alloc_new(32, TN_ALLOC_OBSTACK);
    cctx->_dent_alloc = dent_alloc;
    cctx->rootdir = pkg_dent_add_dir(cctx, NULL, "/");
    cctx->currdir = cctx->rootdir;
    cctx->homedir = cctx->rootdir;
    cctx->htcnf = htcnf = loadconf();

    init_commands(cctx,
                  htcnf? poldek_conf_get_section(htcnf, "global_aliases"):NULL,
                  htcnf? poldek_conf_get_section(htcnf, "aliases"):NULL);

    return 1;
}

struct poclidek_ctx *poclidek_new(struct poldek_ctx *ctx)
{
    struct poclidek_ctx *cctx = n_calloc(1, sizeof(*cctx));

    if (poclidek_init(cctx, ctx))
        return cctx;

    n_free(cctx);
    return NULL;
}

static
void poclidek_destroy(struct poclidek_ctx *cctx)
{
    if (cctx->htcnf) {
        n_hash_free(cctx->htcnf);
        cctx->htcnf = NULL;
    }

    if (cctx->pkgs_available) {
	n_array_free((tn_array*)cctx->pkgs_available);
	cctx->pkgs_available = NULL;
    }

    cctx->pkgs_installed = NULL;

    if (cctx->rootdir)
        pkg_dent_free(cctx->rootdir);

    if (cctx->dbpkgdir) {
        poclidek_save_installedcache(cctx, cctx->dbpkgdir);
        pkgdir_free(cctx->dbpkgdir);
    }

    n_alloc_free(cctx->_dent_na);
    n_array_free(cctx->commands);

    n_assert(cctx->ctx);
    poldek_free(cctx->ctx);
    memset(cctx, 0, sizeof(*cctx));
}

void poclidek_free(struct poclidek_ctx *cctx)
{
    poclidek_destroy(cctx);
    free(cctx);
}

static
struct pkg_dent *add_stub_dir(struct poclidek_ctx *cctx,
                              const char *path, tn_array *stubpkgs)
{
    struct pkg_dent *dent = NULL;

    if ((dent = poclidek_dent_find(cctx, path)) == NULL) {
        dent = pkg_dent_add_dir(cctx, cctx->rootdir, path);
        if (stubpkgs == NULL)
            dent->flags |= PKG_DENT_STUB_EMPTY;
    }

    if (stubpkgs) {
        pkg_dent_add_pkgs(cctx, dent, stubpkgs);
        pkg_dent_clr_isstub(dent);
        dent->flags |= PKG_DENT_STUB;
    }

    return dent;
}

static
struct pkg_dent *load_available(struct poclidek_ctx *cctx)
{
    struct pkg_dent *dir = NULL;

    if ((cctx->_flags & POLDEKCLI_LOADED_AVAILABLE) == 0) {
        tn_array *pkgs = NULL;

        cctx->_flags |= POLDEKCLI_LOADED_AVAILABLE;

        if (!poldek_load_sources(cctx->ctx))
            return NULL;

        if ((pkgs = poldek_get_avail_packages(cctx->ctx)) != NULL) {
            n_array_ctl_set_cmpfn(pkgs, (tn_fn_cmp)pkg_nvr_strcmp);
            dir = poclidek_dent_setup(cctx, POCLIDEK_AVAILDIR, pkgs, 1);

            n_array_sort(pkgs);
            cctx->pkgs_available = pkgs;
            //cctx->homedir = dir;
            //if (cctx->currdir == cctx->rootdir)
            //    poclidek_chdir(cctx, dir->name);
        }
    }

    return dir;
}

static int load_installed(struct poclidek_ctx *cctx, int flags)
{
#if 0 /* skip-installed is default now  */
    if ((cctx->flags & POCLIDEK_SKIP_INSTALLED) && /* --skip-installed  */
        (flags & POCLIDEK_LOAD_RELOAD) == 0) {     /* ...and not reload  */

        return 1;
    }
#endif
    int reload = (flags & POCLIDEK_LOAD_RELOAD);

    if (reload)
        cctx->_flags &= ~POLDEKCLI_LOADED_INSTALLED;

    if ((cctx->_flags & POLDEKCLI_LOADED_INSTALLED) == 0) {
        //cctx->_flags |= POLDEKCLI_LOADED_INSTALLED;

        if (!poclidek__load_installed(cctx, reload))
            return 0;

        cctx->_flags |= POLDEKCLI_LOADED_INSTALLED;
        if (cctx->currdir == cctx->rootdir)
            poclidek_chdir(cctx, POCLIDEK_INSTALLEDDIR);
    }

    return 1;
}

int poclidek_load_packages(struct poclidek_ctx *cctx, unsigned flags)
{
    int nerr = 0;

    if (flags & POCLIDEK_LOAD_AVAILABLE) {
        if (!load_available(cctx))
            nerr++;
    }

    if (flags & POCLIDEK_LOAD_INSTALLED) {
        if (!load_installed(cctx, flags))
            nerr++;
    }

    return nerr == 0;
}

int poclidek_setup(struct poclidek_ctx *cctx)
{
    struct pkg_dent *idir = NULL, *dir = NULL;

    if (!poldek_setup(cctx->ctx)) {
        return 0;
    }

    /* already called */
    if (poclidek_dent_find(cctx, POCLIDEK_INSTALLEDDIR)) {
        return 1;
    }

    /*
       add /avail and /install stubs to allow command completion
       even if packages are not yet loaded
    */
    idir = add_stub_dir(cctx, POCLIDEK_INSTALLEDDIR, NULL);

    n_assert(poclidek_dent_find(cctx, POCLIDEK_INSTALLEDDIR));

    tn_array *sources = poldek_get_sources(cctx->ctx);
    if (sources && n_array_size(sources) > 0) {
        tn_array *pkgs = poldek_load_stubs(cctx->ctx);

        if (pkgs == NULL) {
            dir = load_available(cctx); /* just load repos if no stubs */

        } else {
            n_array_ctl_set_cmpfn(pkgs, (tn_fn_cmp)pkg_nvr_strcmp);
            dir = add_stub_dir(cctx, POCLIDEK_AVAILDIR, pkgs);
            n_array_free(pkgs);
        }
    }

    if (!dir)                   /* no available */
        dir = idir;

    if (dir) {
        cctx->homedir = dir;
        poclidek_chdir(cctx, dir->name);
    }

    n_array_free(sources);

    return 1;
}

static char **a_argv_to_argv(tn_array *a_argv, char **argv)
{
    int i;
    for (i=0; i < n_array_size(a_argv); i++) {
        argv[i] = n_array_nth(a_argv, i);
        DBGF("  %d. (%s)\n", i, argv[i]);
    }
    argv[i] = NULL;
    return argv;
}

/* executes command chain (a pipeline) */
static
int poclidek_exec_cmd_ent(struct poclidek_ctx *cctx, struct poldek_ts *ts,
                          struct cmd_chain_ent *ent, struct cmd_pipe *cmd_pipe)
{
    struct cmdctx  cmdctx;
    char **argv;
    int rc = 0, runit = 1;

    DBGF("ent %s, %d, %p\n", ent->cmd->name, n_array_size(ent->a_argv),
         ent->next_piped);

    /* lazy load packages */
    if (ent->cmd->flags & COMMAND_NEEDAVAIL) {
        poclidek_setup(cctx);
    }

    memset(&cmdctx, 0, sizeof(cmdctx));
    cmdctx.cmd = ent->cmd;
    cmdctx.cctx = cctx;

    if ((cmdctx.ts = ts) == NULL)
        cmdctx.ts = poldek_ts_new(cctx->ctx, 0);

    if (ent->next_piped) {
        ent->pipe_right = cmd_pipe_new();
        cmdctx.pipe_right = ent->pipe_right;

    } else if (cmd_pipe) {
        DBGF("piped %s\n", ent->cmd->name);
        ent->pipe_right = cmd_pipe_link(cmd_pipe);
        cmdctx.pipe_right = ent->pipe_right;
    }

    if (ent->prev_piped) {      /* | cmd */
        struct cmd_pipe *pipe;
        tn_array *pipe_args = NULL;

        pipe = ent->prev_piped->pipe_right;
        ent->prev_piped->pipe_right = NULL;

        cmdctx.pipe_left = pipe;

        if (ent->cmd->flags & COMMAND_PIPE_XARGS) {
            if (ent->cmd->flags & COMMAND_PIPE_PACKAGES)
                pipe_args = cmd_pipe_xargs(pipe, CMD_PIPE_CTX_PACKAGES);
            else
                pipe_args = cmd_pipe_xargs(pipe, CMD_PIPE_CTX_ASCII);

            if (pipe_args == NULL) {
                runit = 0;      /* do not execute command if pipe is empty */
                rc = 0;
                goto l_end;

            } else {
                while (n_array_size(pipe_args))
                    n_array_push(ent->a_argv, n_array_shift(pipe_args));
                n_array_free(pipe_args);
            }
        }
    }


    argv = alloca((n_array_size(ent->a_argv) + 1) * sizeof(*argv));
    a_argv_to_argv(ent->a_argv, argv);

    rc = do_exec_cmd_ent(&cmdctx, n_array_size(ent->a_argv), argv);

 l_end:
    if (ts == NULL)
        poldek_ts_free(cmdctx.ts);

    if (runit && ent->next_piped)
        return poclidek_exec_cmd_ent(cctx, ts, ent->next_piped, cmd_pipe);

    return rc;
}

int do_poclidek_execline(struct poclidek_ctx *cctx, struct poldek_ts *ts,
                         const char *cmdline, struct cmd_pipe *cmd_pipe)
{
    tn_array              *cmd_chain;
    int                   rc = 0, i, verbose;

    /* keep verbose setting as it changes when '-q' option is used */
    verbose = poldek_verbose();

    cmd_chain = poclidek_prepare_cmdline(cctx, cmdline);
    if (cmd_chain == NULL)
        return 0;

    for (i=0; i < n_array_size(cmd_chain); i++) {
        struct cmd_chain_ent  *ent;

        ent = n_array_nth(cmd_chain, i);
        if (ent->flags & (CMD_CHAIN_ENT_SEMICOLON | CMD_CHAIN_ENT_PIPE)) {
            n_assert(0);
            continue;
        }

        if (cmd_pipe && i == n_array_size(cmd_chain) - 1)
            rc = poclidek_exec_cmd_ent(cctx, ts, ent, cmd_pipe);
        else
            rc = poclidek_exec_cmd_ent(cctx, ts, ent, NULL);
    }

    /* restore verbose setting */
    poldek_set_verbose(verbose);

    n_array_free(cmd_chain);
    return rc;
}

int poclidek_execline(struct poclidek_ctx *cctx, struct poldek_ts *ts,
                      const char *cmdline)
{
    return do_poclidek_execline(cctx, ts, cmdline, NULL);
}


int do_poclidek_exec(struct poclidek_ctx *cctx, struct poldek_ts *ts, int argc,
                     const char **argv, struct cmd_pipe *pipe)
{
    char *cmdline;
    int  len, n, i ;

    len = 0;
    for (i=0; i < argc; i++)
        len += 2 * strlen(argv[i]);

    cmdline = alloca(len + 1);
    n = 0;

    for (i=0; i < argc; i++)
        n += n_snprintf(&cmdline[n], len - n, "%s ", argv[i]);

    return do_poclidek_execline(cctx, ts, cmdline, pipe);
}

int poclidek_exec(struct poclidek_ctx *cctx, struct poldek_ts *ts,
                  int argc, const char **argv)
{
    return do_poclidek_exec(cctx, ts, argc, argv, NULL);
}

void poclidek_apply_iinf(struct poclidek_ctx *cctx, struct poldek_ts *ts)
{
    int i, n = 0;
    struct pkg_dent *ent = NULL;

    if (cctx->rootdir)
        ent = poclidek_dent_find(cctx, POCLIDEK_INSTALLEDDIR);

    for (i=0; i < n_array_size(ts->pkgs_removed); i++) {
        struct pkg *pkg = n_array_nth(ts->pkgs_removed, i);

        pkgdir_remove_package(cctx->dbpkgdir, pkg);
        if (ent)
            pkg_dent_remove_pkg(ent, pkg);

        n++;
        DBGF("- %s\n", pkg_id(pkg));
    }

    for (i=0; i < n_array_size(ts->pkgs_installed); i++) {
        struct pkg *pkg = n_array_nth(ts->pkgs_installed, i);

        /*
          assure new packages haven't recno and clean it if they have;
          playing with recno is messy and should be fixed.
        */
        if (pkg->recno) {
            logn(LOGERR, "%s: recno is set, should not happen", pkg_id(pkg));
            pkg->recno = 0;
        }

        pkgdir_add_package(cctx->dbpkgdir, pkg);
        if (ent)
            pkg_dent_add_pkg(cctx, ent, pkg);
        DBGF("+ %s\n", pkg_id(pkg));
        n++;
    }

    if (n)
        cctx->ts_dbpkgdir = time(0);
}
