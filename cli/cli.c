/* 
  Copyright (C) 2000 - 2004 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
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
#include <time.h>

#include <trurl/trurl.h>

#include <sigint/sigint.h>
#include "pkgdir/pkgdir.h"
#include "i18n.h"
#include "misc.h"
#include "log.h"
#include "cli.h"
#include "cmd.h"
#include "cmd_pipe.h"
#include "poclidek.h"


static unsigned argp_parse_flags = ARGP_NO_EXIT;

static int argv_is_help(int argc, const char **argv);

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
                verbose++;
            }
        }
        break;
        
            
        case ARGP_KEY_ARG:
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

static char *help_filter(int key, const char *text, void *input) 
{
    struct sh_cmdctx *sh_cmdctx = input;

    if (key == ARGP_KEY_HELP_EXTRA) {
        char *p, buf[4096];
        int n = 0;
        
        
        if (sh_cmdctx->cmd->extra_help) 
            n += n_snprintf(&buf[n], sizeof(buf) - n, "  %s\n",
                          sh_cmdctx->cmd->extra_help);
        
        if (n > 0) {
            p = n_malloc(n + 1);
            return memcpy(p, buf, n + 1);
        }
    }
    
    return (char*)text;
}

static
int do_exec_cmd_ent(struct cmdctx *cmdctx, int argc, char **argv) 
{
    struct sh_cmdctx     sh_cmdctx;
    int                  rc = 1, verbose_;
    unsigned             parse_flags;
    struct poclidek_cmd  *cmd;
    struct argp          argp = { cmdctx->cmd->argp_opts, parse_opt,
                                  cmdctx->cmd->arg,
                                  cmdctx->cmd->doc, 0, 0, 0};

    verbose_ = verbose;
    if (argv == NULL)
        return 0;

    cmd = cmdctx->cmd;
    if (argv_is_help(argc, (const char**)argv)) {
        cmdctx->rtflags |= CMDCTX_ISHELP;
        printf("is_help!\n");
    }

    if (verbose < 0)
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

    verbose = verbose_;
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
        n = vprintf_c(color, fmt, args);

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
    int n = 0;
	char   *homedir;
    
    cctx->commands = n_array_new(16, NULL, (tn_fn_cmp)command_cmp);
    n_array_ctl(cctx->commands, TN_ARRAY_AUTOSORTED);
    while (commands_tab[n] != NULL) {
        struct poclidek_cmd *cmd = commands_tab[n++];
        poclidek_add_command(cctx, cmd);
    }
	n_array_sort(cctx->commands);

    poclidek_load_aliases(cctx, "/etc/poldek/alias");
	if ((homedir = getenv("HOME")) != NULL) {
		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/.poldek.alias", homedir);	
		poclidek_load_aliases(cctx, path);
	}
    n_array_sort(cctx->commands);
}

static void *dent_alloc(struct poclidek_ctx *cctx, size_t size)
{
    return cctx->_dent_na->na_malloc(cctx->_dent_na, size);
}


int poclidek_init(struct poclidek_ctx *cctx, struct poldek_ctx *ctx)
{
    n_assert (cctx->ctx == NULL);
    cctx->flags = 0;
    cctx->ctx = ctx;
    cctx->pkgs_available = NULL;
    cctx->pkgs_installed = NULL;
    cctx->_dent_na = n_alloc_new(32, TN_ALLOC_OBSTACK);
    cctx->dent_alloc = dent_alloc;
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
    n_free(cctx);
}


int poclidek_load_packages(struct poclidek_ctx *cctx) 
{
    struct poldek_ctx *ctx;

    if (cctx->flags & POLDEKCLI_PACKAGES_LOADED)
        return 1;

    cctx->flags |= POLDEKCLI_PACKAGES_LOADED;

    ctx = cctx->ctx;
    
    if (!poldek_load_sources(ctx))
        return 0;

    cctx->pkgs_available = poldek_get_avail_packages(ctx);
    if (cctx->pkgs_available) {
        n_array_ctl_set_cmpfn(cctx->pkgs_available, (tn_fn_cmp)pkg_nvr_strcmp);
        n_array_sort(cctx->pkgs_available);
    }
    
    poclidek_dent_init(cctx);
    if (cctx->flags & POLDEKCLI_SKIPINSTALLED)
        return 1;
    
    
    return poclidek_load_installed(cctx, 0); 
}

static char **a_argv_to_argv(tn_array *a_argv, char **argv) 
{
    int i;
    for (i=0; i < n_array_size(a_argv); i++) {
        argv[i] = n_array_nth(a_argv, i);
        //printf("  %d. %s\n", j, argv[j]);
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
        cmdctx.ts = poldek_ts_new(cctx->ctx);

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

static 
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


static
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


struct poclidek_rcmd *poclidek_rcmd_new(struct poclidek_ctx *cctx,
                                        struct poldek_ts *ts)
{
    struct poclidek_rcmd *rcmd = n_malloc(sizeof(*rcmd));
    rcmd->_cctx = cctx;
    rcmd->_ts = ts;
    rcmd->rpkgs = NULL;
    rcmd->rbuf = NULL;
    rcmd->rc = -1;
    return rcmd;
}

void poclidek_rcmd_free(struct poclidek_rcmd *rcmd)
{
    if (rcmd->rpkgs)
        n_array_free(rcmd->rpkgs);

    if (rcmd->rbuf)
        n_buf_free(rcmd->rbuf);

    memset(rcmd, 0, sizeof(*rcmd));
    free(rcmd);
}


int poclidek_rcmd_exec(struct poclidek_rcmd *rcmd, int argc, const char **argv)
{
    struct cmd_pipe *pipe = cmd_pipe_new();
    rcmd->rc = do_poclidek_exec(rcmd->_cctx, rcmd->_ts, argc, argv, pipe);
    rcmd->rpkgs = n_ref(pipe->pkgs);
    rcmd->rbuf = n_ref(pipe->nbuf);
    cmd_pipe_free(pipe);
    return rcmd->rc;
}

int poclidek_rcmd_execline(struct poclidek_rcmd *rcmd, const char *cmdline)
{
    struct cmd_pipe *pipe = cmd_pipe_new();
    rcmd->rc = do_poclidek_execline(rcmd->_cctx, rcmd->_ts, cmdline, pipe);
    rcmd->rpkgs = n_ref(pipe->pkgs);
    rcmd->rbuf = n_ref(pipe->nbuf);
    cmd_pipe_free(pipe);
    return rcmd->rc;
}



void poclidek_apply_iinf(struct poclidek_ctx *cctx, struct install_info *iinf)
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
        printf("- %s\n", pkg->nvr);
    }
        
    for (i=0; i < n_array_size(iinf->installed_pkgs); i++) {
        struct pkg *pkg = n_array_nth(iinf->installed_pkgs, i);
        n_array_push(cctx->pkgs_installed, pkg_link(pkg));
        if (ent)
            pkg_dent_add_pkg(cctx, ent, pkg);
        
        n++;
    }
    
    n_array_sort(cctx->pkgs_installed);
        
    if (n)
        cctx->ts_dbpkgdir = time(0);
}
