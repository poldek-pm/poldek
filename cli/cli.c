/*
  Copyright (C) 2000 - 2004 Pawel A. Gajda <mis@pld.org.pl>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
  $Id$
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <signal.h>
#include <time.h>
#include <argp.h>

#include <trurl/trurl.h>

#include <sigint/sigint.h>
#include "pkgdir/pkgdir.h"
#include "i18n.h"
#include "log.h"
#include "cli.h"
#include "cmd_chain.h"
#include "cmd_pipe.h"

static unsigned argp_parse_flags = ARGP_NO_EXIT;

static int argv_is_help(int argc, const char **argv);
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

static struct poclidek_cmd *commands_tab[] = {
    &command_ls,
    &command_search,
    &command_get,
    &command_desc,
    &command_install, 
    &command_uninstall,
    &command_cd,
    &command_pwd,
    &command_external,
    &command_help,
    &command_alias,
    NULL
};

struct sh_cmdctx {
    unsigned        cmdflags;
    int             err;
    struct cmdctx   *cmdctx;
    struct poclidek_cmd  *cmd;
    error_t (*parse_opt_fn)(int, char*, struct argp_state*);
};


/* default parse_opt */
static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct sh_cmdctx *sh_cmdctx = state->input;
    unsigned flags = sh_cmdctx->cmdflags;
    int rc;


    state->input = sh_cmdctx->cmdctx;
    
    if (sh_cmdctx->parse_opt_fn)
        rc = sh_cmdctx->parse_opt_fn(key, arg, state);
    else
        rc = ARGP_ERR_UNKNOWN;
    state->input = sh_cmdctx;
    
    if (rc == EINVAL) 
        sh_cmdctx->err = 1;

    if (rc != ARGP_ERR_UNKNOWN)
        return rc;
    
    rc = 0;

    switch (key) {
        case 'v': {
            if ((flags & COMMAND_HASVERBOSE) == 0) {
                argp_usage (state);
                sh_cmdctx->err = 1;
                
            } else {
                poldek_VERBOSE++;
            }
        }
        break;
        
            
        case ARGP_KEY_ARG:
            DBGF("cli.arg %s\n", arg);
            if (flags & COMMAND_NOARGS) {
                argp_usage (state);
                sh_cmdctx->err = 1; 
                return EINVAL;
            }
            //printf("push\n");
            poldek_ts_add_pkgmask(sh_cmdctx->cmdctx->ts, arg);
            break;
            
        case 'h':
            argp_state_help(state, stdout, ARGP_HELP_LONG | ARGP_HELP_DOC |
                            ARGP_HELP_USAGE);
            return EINVAL;
            break;
            
        case ARGP_KEY_NO_ARGS:
            if (sh_cmdctx->cmdctx->rtflags & CMDCTX_ISHELP)
                break;
            
            //printf("ARGP_KEY_NO_ARGS --\n");
            if ((flags & COMMAND_NOARGS) == 0 &&
                (flags & COMMAND_EMPTYARGS) == 0) {
                //printf("ARGP_KEY_NO_ARGS\n");
                argp_usage (state);
                sh_cmdctx->err = 1; 
                return EINVAL;
            }
            break;
            
            
        case ARGP_KEY_ERROR:
            //printf("ARGP_KEY_ERROR\n");
            sh_cmdctx->err = 1;
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
        if ((cm->flags & COMMAND_IS_ALIAS) == 0)
            continue;

        if (strncmp(nam, cm->cmdline, n) == 0) {
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
    struct sh_cmdctx *sh_cmdctx = input;
    char *p, buf[4096];
    tn_array *aliases;
    int n = 0;
    
    if (key != ARGP_KEY_HELP_EXTRA)
        return (char*)text;
        
    if (sh_cmdctx->cmd->extra_help) 
        n += n_snprintf(&buf[n], sizeof(buf) - n, "  %s\n",
                        sh_cmdctx->cmd->extra_help);

    aliases = find_command_aliases(sh_cmdctx->cmd, sh_cmdctx->cmdctx->cctx);
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

static
int do_exec_cmd_ent(struct cmdctx *cmdctx, int argc, char **argv) 
{
    struct sh_cmdctx     sh_cmdctx;
    int                  rc = 1;
    unsigned             parse_flags;
    struct poclidek_cmd  *cmd;
    struct argp          argp = { cmdctx->cmd->argp_opts, parse_opt,
                                  cmdctx->cmd->arg,
                                  cmdctx->cmd->doc, 0, 0, 0};
    
    if (argv == NULL)
        return 0;

    cmd = cmdctx->cmd;
    if (argv_is_help(argc, (const char**)argv)) {
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

    sh_cmdctx.cmdflags = cmd->flags; 
    sh_cmdctx.err = 0;
    sh_cmdctx.cmdctx = cmdctx;
    sh_cmdctx.cmd = cmd;
    sh_cmdctx.parse_opt_fn = cmd->parse_opt_fn;

    argp.help_filter = help_filter;
    parse_flags = argp_parse_flags;
    argp_parse(&argp, argc, (char**)argv, parse_flags, 0, (void*)&sh_cmdctx);

    if (sh_cmdctx.err) {
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
static int argv_is_help(int argc, const char **argv)
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

int cmdctx_printf(struct cmdctx *cmdctx, const char *fmt, ...)
{
    va_list args;
    int n = 0;
    
    if (cmdctx_isctrlmsg(fmt)) {
        if (cmdctx->rtflags & CMDCTX_NOCTRLMSGS)
            return 1;
        fmt++;
    }

    va_start(args, fmt);
    if (cmdctx->pipe_right)
        n = cmd_pipe_vprintf(cmdctx->pipe_right, fmt, args);
    else 
        n = vfprintf(stdout, fmt, args);

    va_end(args);
    return n;
    
}

int cmdctx_printf_c(struct cmdctx *cmdctx, int color, const char *fmt, ...)
{
    va_list args;
    int n = 0;

    if (cmdctx_isctrlmsg(fmt)) {
        if (cmdctx->rtflags & CMDCTX_NOCTRLMSGS)
            return 1;
        fmt++;
    }

    va_start(args, fmt);
    if (cmdctx->pipe_right)
        n = cmd_pipe_vprintf(cmdctx->pipe_right, fmt, args);
    else 
        n = poldek_term_vprintf_c(color, fmt, args);

    return n;
}

int cmdctx_addtoresult(struct cmdctx *cmdctx, struct pkg *pkg) 
{
    if (cmdctx->pipe_right)
        return cmd_pipe_writepkg(cmdctx->pipe_right, pkg);
    return 1;
}

static
int command_cmp(struct poclidek_cmd *c1, struct poclidek_cmd *c2) 
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
    
    cmd->arg = _(cmd->arg);
    cmd->doc = _(cmd->doc);
    
    
    if (n_array_bsearch(cctx->commands, cmd)) {
        logn(LOGERR, _("ambiguous command %s"), cmd->name);
        return 0;
    }
    n_array_push(cctx->commands, cmd);
    n_array_sort(cctx->commands);
    return 1;
}


static void init_commands(struct poclidek_ctx *cctx) 
{
    char *homedir, *sysconfdir = "/etc", path[PATH_MAX];
    int n = 0;
    
    cctx->commands = n_array_new(16, (tn_fn_free)command_free,
                                 (tn_fn_cmp)command_cmp);
    n_array_ctl(cctx->commands, TN_ARRAY_AUTOSORTED);
    while (commands_tab[n] != NULL) {
        struct poclidek_cmd *cmd = commands_tab[n++];
        poclidek_add_command(cctx, cmd);
    }
	n_array_sort(cctx->commands);

#ifdef SYSCONFDIR
    if (access(SYSCONFDIR, R_OK) == 0)
        sysconfdir = SYSCONFDIR;
#endif

    n_snprintf(path, sizeof(path), "%s/poldek/aliases.conf", sysconfdir);
    poclidek_load_aliases(cctx, path);

    if ((homedir = getenv("HOME")) != NULL) {
		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/.poldek-aliases.conf", homedir);	
		if (!poclidek_load_aliases(cctx, path)) {
            snprintf(path, sizeof(path), "%s/.poldek.alias", homedir);
            poclidek_load_aliases(cctx, path);
        }
	}
    n_array_sort(cctx->commands);
}

static void *dent_alloc(struct poclidek_ctx *cctx, size_t size)
{
    return cctx->_dent_na->na_malloc(cctx->_dent_na, size);
}

static
int poclidek_init(struct poclidek_ctx *cctx, struct poldek_ctx *ctx)
{
    n_assert (cctx->ctx == NULL);
    cctx->_flags = 0;
    cctx->ctx = ctx;
    cctx->pkgs_available = NULL;
    cctx->pkgs_installed = NULL;
    cctx->_dent_na = n_alloc_new(32, TN_ALLOC_OBSTACK);
    cctx->_dent_alloc = dent_alloc;
    cctx->rootdir = pkg_dent_add_dir(cctx, NULL, "/");
    cctx->currdir = cctx->rootdir;
    cctx->homedir = NULL;
    init_commands(cctx);
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
    if (cctx->pkgs_available)
        n_array_free(cctx->pkgs_available);
    
    if (cctx->pkgs_installed)
        n_array_free(cctx->pkgs_installed);

    if (cctx->rootdir)
        pkg_dent_free(cctx->rootdir);

    if (cctx->dbpkgdir) {
        poclidek_save_installedcache(cctx, cctx->dbpkgdir);
        pkgdir_free(cctx->dbpkgdir);
    }

    n_alloc_free(cctx->_dent_na);
    n_array_free(cctx->commands);
    
    memset(cctx, 0, sizeof(*cctx));
}

void poclidek_free(struct poclidek_ctx *cctx) 
{
    poclidek_destroy(cctx);
    free(cctx);
}


int poclidek_load_packages(struct poclidek_ctx *cctx, unsigned flags) 
{
    int nerr = 0;
    

    DBGF("%d\n", flags);
    
    if (flags & POCLIDEK_LOAD_AVAILABLE) {
        if ((cctx->_flags & POLDEKCLI_LOADED_AVAILABLE) == 0) {
            cctx->_flags |= POLDEKCLI_LOADED_AVAILABLE;
            
            if (!poldek_load_sources(cctx->ctx)) 
                nerr++;
            else {
                tn_array *pkgs = poldek_get_avail_packages(cctx->ctx);
                if (pkgs) {
                    struct pkg_dent *dir;
                    n_array_ctl_set_cmpfn(pkgs, (tn_fn_cmp)pkg_nvr_strcmp);
                    dir = poclidek_dent_setup(cctx, POCLIDEK_AVAILDIR, pkgs, 0);
                    n_array_sort(pkgs);
                    cctx->pkgs_available = pkgs;
                    cctx->homedir = dir;
                    DBGF("currdir (%s)\n", cctx->rootdir->name);
                    if (cctx->currdir == cctx->rootdir)
                        poclidek_chdir(cctx, dir->name);
                }
            }
        }
    }

    if ((cctx->flags & POCLIDEK_SKIP_INSTALLED) && /* --skip-installed  */
        (flags & POCLIDEK_LOAD_RELOAD) == 0) {     /* ...and not reload  */
        return nerr == 0;
    }
    
    if (flags & POCLIDEK_LOAD_INSTALLED) {
        int reload = (flags & POCLIDEK_LOAD_RELOAD);
        
        if (reload)
            cctx->_flags &= ~POLDEKCLI_LOADED_INSTALLED;
        
        if ((cctx->_flags & POLDEKCLI_LOADED_INSTALLED) == 0) {
            cctx->_flags |= POLDEKCLI_LOADED_INSTALLED;
            if (!poclidek_load_installed(cctx, reload))
                nerr++;
            else {
                if (n_str_eq(cctx->currdir->name, "/"))
                    poclidek_chdir(cctx, POCLIDEK_INSTALLEDDIR);
            }
        }
    }
    
    return nerr == 0;
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

tn_array *poclidek_prepare_cmdline(struct poclidek_ctx *cctx, const char *line);


static
int poclidek_exec_cmd_ent(struct poclidek_ctx *cctx, struct poldek_ts *ts,
                          struct cmd_chain_ent *ent, struct cmd_pipe *cmd_pipe)
{
    struct cmdctx  cmdctx;
    char **argv;
    int rc = 0;
    
    DBGF("ent %s, %d, %p\n", ent->cmd->name, n_array_size(ent->a_argv),
         ent->next_piped);

    
    
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

    if (ent->prev_piped) {
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
            
            if (pipe_args) {
                while (n_array_size(pipe_args))
                    n_array_push(ent->a_argv, n_array_shift(pipe_args));
            }
        }
    }
    

    argv = alloca((n_array_size(ent->a_argv) + 1) * sizeof(*argv));
    a_argv_to_argv(ent->a_argv, argv);

    rc = do_exec_cmd_ent(&cmdctx, n_array_size(ent->a_argv), argv);
    
    if (ts == NULL) 
        poldek_ts_free(cmdctx.ts);

    if (ent->next_piped)
        return poclidek_exec_cmd_ent(cctx, ts, ent->next_piped, cmd_pipe);
    
    return rc;
}

int do_poclidek_execline(struct poclidek_ctx *cctx, struct poldek_ts *ts,
                         const char *cmdline, struct cmd_pipe *cmd_pipe) 
{
    tn_array              *cmd_chain;
    int                   rc = 0, i;

    DBGF("%s\n", cmdline);
    
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

int poclidek_exec(struct poclidek_ctx *cctx, struct poldek_ts *ts, int argc,
                  const char **argv)
{
    return do_poclidek_exec(cctx, ts, argc, argv, NULL);
}

void poclidek_apply_iinf(struct poclidek_ctx *cctx, struct poldek_iinf *iinf)
{
    int i, n = 0;
    struct pkg_dent *ent = NULL;
    
    if (iinf == NULL || cctx->pkgs_installed == NULL)
        return;
    
    if (cctx->rootdir)
        ent = poclidek_dent_find(cctx, POCLIDEK_INSTALLEDDIR);
        
    n_array_sort(cctx->pkgs_installed);
    
    for (i=0; i < n_array_size(iinf->uninstalled_pkgs); i++) {
        struct pkg *pkg = n_array_nth(iinf->uninstalled_pkgs, i);
        n_array_remove(cctx->pkgs_installed, pkg);
        if (ent)
            pkg_dent_remove_pkg(ent, pkg);
        
        n++;
        DBGF("- %s\n", pkg->nvr);
    }

    for (i=0; i < n_array_size(iinf->installed_pkgs); i++) {
        struct pkg *pkg = n_array_nth(iinf->installed_pkgs, i);
        n_array_push(cctx->pkgs_installed, pkg_link(pkg));
        n_array_push(cctx->dbpkgdir_added, pkg_link(pkg));
        if (ent)
            pkg_dent_add_pkg(cctx, ent, pkg);
        DBGF("+ %s\n", pkg->nvr);
        n++;
    }
    
    n_array_sort(cctx->pkgs_installed);
        
    if (n)
        cctx->ts_dbpkgdir = time(0);
}
