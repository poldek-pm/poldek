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
#include "i18n.h"
#include "misc.h"
#include "log.h"
#include "cli.h"
#include "cmd.h"
//#include "conf.h"
#include "arg_packages.h"


int shOnTTY = 0;

static volatile sig_atomic_t shInCmd  = 0;

static unsigned argp_parse_flags = ARGP_NO_EXIT;

static int argv_is_help(int argc, const char **argv);

extern struct poclidek_cmd command_ls;
extern struct poclidek_cmd command_install;
extern struct poclidek_cmd command_uninstall;
//extern struct poclidek_cmd command_get;
extern struct poclidek_cmd command_search;
extern struct poclidek_cmd command_desc;
extern struct poclidek_cmd command_cd;
extern struct poclidek_cmd command_pwd;
extern struct poclidek_cmd command_external;

static struct poclidek_cmd *commands_tab[] = {
    &command_ls,
    &command_install, 
    &command_uninstall,
    &command_search,
    &command_desc,
    &command_cd,
    &command_pwd,
    &command_external,
    NULL
};

struct sh_cmdarg {
    unsigned        cmdflags;
    int             err;
    struct cmdarg   *cmdarg;
    struct poclidek_cmd  *cmd;   
    error_t (*parse_opt_fn)(int, char*, struct argp_state*);
};


/* default parse_opt */
static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct sh_cmdarg *sh_cmdarg = state->input;
    unsigned flags = sh_cmdarg->cmdflags;
    int rc;


    state->input = sh_cmdarg->cmdarg;
    
    if (sh_cmdarg->parse_opt_fn)
        rc = sh_cmdarg->parse_opt_fn(key, arg, state);
    else
        rc = ARGP_ERR_UNKNOWN;
    state->input = sh_cmdarg;
    
    if (rc == EINVAL) 
        sh_cmdarg->err = 1;

    if (rc != ARGP_ERR_UNKNOWN)
        return rc;
    
    rc = 0;

    switch (key) {
        case 'v': {
            if ((flags & COMMAND_HASVERBOSE) == 0) {
                argp_usage (state);
                sh_cmdarg->err = 1;
                
            } else {
                verbose++;
            }
        }
        break;
        
            
        case ARGP_KEY_ARG:
            if (flags & COMMAND_NOARGS) {
                argp_usage (state);
                sh_cmdarg->err = 1; 
                return EINVAL;
            }
            //printf("push\n");
            poldek_ts_add_pkgmask(sh_cmdarg->cmdarg->ts, arg);
            break;
            
        case 'h':
            argp_state_help(state, stdout, ARGP_HELP_LONG | ARGP_HELP_DOC |
                            ARGP_HELP_USAGE);
            return EINVAL;
            break;
            
        case ARGP_KEY_NO_ARGS:
            if (sh_cmdarg->cmdarg->is_help)
                break;
            
            //printf("ARGP_KEY_NO_ARGS --\n");
            if ((flags & COMMAND_NOARGS) == 0 &&
                (flags & COMMAND_EMPTYARGS) == 0) {
                //printf("ARGP_KEY_NO_ARGS\n");
                argp_usage (state);
                sh_cmdarg->err = 1; 
                return EINVAL;
            }
            break;
            
            
        case ARGP_KEY_ERROR:
            //printf("ARGP_KEY_ERROR\n");
            sh_cmdarg->err = 1;
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
    struct sh_cmdarg *sh_cmdarg = input;

    if (key == ARGP_KEY_HELP_EXTRA) {
        char *p, buf[4096];
        int n = 0;
        
        
        if (sh_cmdarg->cmd->extra_help) 
            n += n_snprintf(&buf[n], sizeof(buf) - n, "  %s\n",
                          sh_cmdarg->cmd->extra_help);
        
#if 0
		alias.cmd = sh_cmdarg->cmd;

        if (n_array_bsearch_ex(cctx->aliases, &alias,
                               (tn_fn_cmp)command_alias_cmd_cmp)) {
           int i = 0;
		   struct poclidek_cmd_alias *alias;

            n += n_snprintf(&buf[n], sizeof(buf) - n, "%s",
                            _("  Defined aliases:\n"));
			
            while (i < n_array_size(cctx->aliases)) {
				alias = n_array_nth(cctx->aliases, i);
				if (alias->cmd == sh_cmdarg->cmd)
	                n += n_snprintf(&buf[n], sizeof(buf) - n,
                                    "    %-16s  \"%s\"\n",
                                    alias->name, alias->cmdline);
				i++;
            }
        }
#endif        
        if (n > 0) {
            p = n_malloc(n + 1);
            return memcpy(p, buf, n + 1);
        }
    }
    
    return (char*)text;
}



static
int docmd(struct poclidek_ctx *cctx, struct poldek_ts *ts, 
          struct poclidek_cmd *cmd, int argc, const char **argv)
{
    struct cmdarg        cmdarg;
    struct sh_cmdarg     sh_cmdarg;
    int                  rc = 1, verbose_;
    unsigned             parse_flags;
    struct argp          argp = { cmd->argp_opts, parse_opt, cmd->arg,
                                  cmd->doc, 0, 0, 0};

    verbose_ = verbose;
    if (argv == NULL)
        return 0;

    cmdarg.is_help = argv_is_help(argc, argv);
    cmdarg.flags = 0;
    cmdarg.cctx = cctx;
    if ((cmdarg.ts = ts) == NULL)
        cmdarg.ts = poldek_ts_new(cctx->ctx);
    
    cmdarg.d = NULL;
    if (cmd->init_cmd_arg_d)
        cmdarg.d = cmd->init_cmd_arg_d();

    if (cmd->cmd_fn) {
        rc = cmd->cmd_fn(&cmdarg, argc, argv, &argp);
        goto l_end;
    }
    
    parse_flags = argp_parse_flags;
    shInCmd = 1;
    if ((cmd->flags & COMMAND_NOHELP) && (cmd->flags & COMMAND_NOARGS) &&
        (cmd->flags & COMMAND_NOOPTS)) {

        rc = cmd->do_cmd_fn(&cmdarg);
        goto l_end;
    }

    sh_cmdarg.cmdflags = cmd->flags; 
    sh_cmdarg.err = 0;
    sh_cmdarg.cmdarg = &cmdarg;
    sh_cmdarg.cmd = cmd;
    sh_cmdarg.parse_opt_fn = cmd->parse_opt_fn;

    argp.help_filter = help_filter;
    argp_parse(&argp, argc, (char**)argv, parse_flags, 0, (void*)&sh_cmdarg);

    if (sh_cmdarg.err) {
        rc = 0;
        goto l_end;
    }
    
    if (cmdarg.is_help) {
        rc = 1;
        goto l_end;
    }
    
    rc = cmd->do_cmd_fn(&cmdarg);

 l_end:
    shInCmd = 0;
    
    //if (cmdarg.pkgs)
    //    n_array_free(cmdarg.pkgs);
    
    
    if (cmd->destroy_cmd_arg_d && cmdarg.d)
        cmd->destroy_cmd_arg_d(cmdarg.d);

    if (ts == NULL)  /* free our own ts */
        poldek_ts_free(cmdarg.ts);
    
#if 0                           /* DUPA */
    if ((cmd->flags & COMMAND_MODIFIESDB) && cmdarg.sh_s->ts_instpkgs > 0) {
        cmdarg.sh_s->dbpkgdir->ts = cmdarg.sh_s->ts_instpkgs;
        cmdarg.sh_s->ts_instpkgs = 0;
    }
#endif    
    verbose = verbose_;
    return rc;
}

struct docmd_arg {
    struct poclidek_ctx *cctx;
    struct poldek_ts *ts;
    struct poclidek_cmd *cmd;
    int argc; const char **argv;
};

static
int docmd_proc(void *arg) {
    struct docmd_arg *a= arg;

    return docmd(a->cctx, a->ts, a->cmd, a->argc, a->argv);
}

tn_array *poclidek_cmdarg_dents(struct cmdarg *cmdarg, const char *path,
                                int exact)
{
    tn_array *ents = NULL;

    
    if (poldek_ts_get_arg_count(cmdarg->ts))
        ents = poclidek_resolve_dents(path, cmdarg->cctx, cmdarg->ts, exact);
    
    else {
        ents = poclidek_get_dents(cmdarg->cctx, path);
        ents = n_ref(ents);
    }
    
    return ents;
}

tn_array *poclidek_resolve_packages(struct poclidek_ctx *cctx,
                                    struct poldek_ts *ts,
                                    int exact)
{
    tn_array *pkgs;

    if ((pkgs = poclidek_get_dent_packages(cctx, NULL)) == NULL)
        return NULL;

    return arg_packages_resolve(ts->aps, pkgs,
                                exact ? ARG_PACKAGES_RESOLV_EXACT : 0);
}


/* argp workaround */
static int argv_is_help(int argc, const char **argv)
{
    int i, is_help = 0;

    for (i=0; i<argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-?") == 0 ||
            strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--usage") == 0) {
            
            is_help = 1;
            break;
        }
    }
    return is_help;
}

static
int cmd_help(struct cmdarg *cmdarg)
{
    int i = 0;
    
    cmdarg = cmdarg;
    
    //printf("%s\n", poldek_banner);
    while (commands_tab[i]) {
        struct poclidek_cmd *cmd = commands_tab[i++];
        char buf[256], *p;
        
        
        p = cmd->arg ? cmd->arg : "";
        if (cmd->argp_opts) {
            snprintf(buf, sizeof(buf), _("[OPTION...] %s"), cmd->arg);
            p = buf;
        }
        printf("%-9s %-36s %s\n", cmd->name, p, cmd->doc);
    }
    printf(_("\nFor now \"search\" and \"desc\" commands don't work with "
             "installed packages.\n"));
    
    printf(_("\nType COMMAND -? for details.\n"));
    return 0;
}

int sh_printf_c(FILE *stream, int color, const char *fmt, ...)
{
    va_list args;
    int n = 0;

    va_start(args, fmt);
    if (stream == stdout)
        n = vprintf_c(color, fmt, args);
    else
        n = vfprintf(stream, fmt, args);
    
    va_end(args);
    return n;
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


static void init_commands(struct poclidek_ctx *cctx) 
{
    int n = 0;
	char   *homedir;
    
    cctx->commands = n_array_new(16, NULL, (tn_fn_cmp)command_cmp);
	
    while (commands_tab[n] != NULL) {
        struct poclidek_cmd *cmd = commands_tab[n++];
        if (cmd->argp_opts)
            translate_argp_options(cmd->argp_opts);

        cmd->arg = _(cmd->arg);
        cmd->doc = _(cmd->doc);
        
        
        if (n_array_bsearch(cctx->commands, cmd)) {
            logn(LOGERR, _("ambiguous command %s"), cmd->name);
            exit(EXIT_FAILURE);
        }
        n_array_push(cctx->commands, cmd);
        n_array_sort(cctx->commands);
    }
	n_array_sort(cctx->commands);

    poclidek_load_aliases(cctx, "/etc/poldek.alias");
	if ((homedir = getenv("HOME")) != NULL) {
		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/.poldek.alias", homedir);	
		poclidek_load_aliases(cctx, path);
	}
    n_array_sort(cctx->commands);
}

int poclidek_init(struct poclidek_ctx *cctx, struct poldek_ctx *ctx)
{
    n_assert (cctx->ctx == NULL);
    cctx->flags = 0;
    cctx->ctx = ctx;
    cctx->avpkgs = NULL;
    cctx->instpkgs = NULL;
    poclidek_set_pkgctx(cctx, POLDEKCLI_PKGCTX_AVAIL);

    init_commands(cctx);
    return 1;
}


void poclidek_destroy(struct poclidek_ctx *cctx) 
{
    if (cctx->avpkgs)
        n_array_free(cctx->avpkgs);
    
    if (cctx->instpkgs)
        n_array_free(cctx->instpkgs);

    if (cctx->rootdir)
        pkg_dent_free(cctx->rootdir);

    if (cctx->dbpkgdir) {
        poclidek_save_installedcache(cctx, cctx->dbpkgdir);
        pkgdir_free(cctx->dbpkgdir);
    }
    
    memset(cctx, 0, sizeof(*cctx));
}


int poclidek_load_packages(struct poclidek_ctx *cctx, int skip_installed) 
{
    struct poldek_ctx *ctx;
    

    if (cctx->flags & POLDEKCLI_PACKAGES_LOADED)
        return 1;

    cctx->flags |= POLDEKCLI_PACKAGES_LOADED;

    ctx = cctx->ctx;
    
    if (!poldek_load_sources(ctx))
        return 0;

    cctx->avpkgs = poldek_get_avpkgs_bynvr(ctx);
    poclidek_dent_init(cctx);
    
    poclidek_load_installed(cctx, 0); 
    
    cctx->instpkgs = NULL;
    if (skip_installed == 0) {
        
        n_array_ctl(cctx->instpkgs, TN_ARRAY_AUTOSORTED);
        //load_installed_packages(&shell_s, 0);
    }

    
    return 1;
}



tn_array *poclidek_prepare_cmdline(struct poclidek_ctx *cctx, const char *line);

int poclidek_exec_line(struct poclidek_ctx *cctx, struct poldek_ts *ts,
                       const char *cmdline) 
{
    tn_array            *cmd_chain;
    int                 nerr = 0, arg_ts, i, j;

    printf("exec_line = %s\n", cmdline);
    cmd_chain = poclidek_prepare_cmdline(cctx, cmdline);
    //return 1;
    arg_ts = (ts != NULL);
    
    for (i=0; i < n_array_size(cmd_chain); i++) {
        struct cmd_chain_ent  *ent;
        char                  **argv;
        
        ent = n_array_nth(cmd_chain, i);
        if (ent->flags & (CMD_CHAIN_ENT_SEMICOLON | CMD_CHAIN_ENT_PIPE))
            continue;
        
        printf("ent %s, %d\n", ent->cmd->name, n_array_size(ent->a_argv));
        argv = alloca((n_array_size(ent->a_argv) + 1) * sizeof(*argv));
        for (j=0; j < n_array_size(ent->a_argv); j++) {
            argv[j] = n_array_nth(ent->a_argv, j);
            printf("  %d. %s\n", j, argv[j]);
        }
        argv[j] = NULL;

        if (arg_ts == 0)       /* ts related to 1st cmd only */
            ts = poldek_ts_new(cctx->ctx);
        
        nerr += docmd(cctx, ts, ent->cmd, n_array_size(ent->a_argv), argv);
        
        if (!arg_ts) 
            poldek_ts_free(ts);
        arg_ts = 0;
    }

    return nerr == 0;
}



int poclidek_exec(struct poclidek_ctx *cctx, struct poldek_ts *ts, 
                   int argc, const char **argv)
{
    char                *cmdline;
    int                 len, n, i ;

    len = 0;
    for (i=0; i < argc; i++)
        len += 2 * strlen(argv[i]);

    cmdline = alloca(len + 1);
    n = 0;
    
    for (i=0; i < argc; i++)
        n += n_snprintf(&cmdline[n], len - n, "%s ", argv[i]);
    
    return poclidek_exec_line(cctx, ts, cmdline);
}


void poclidek_apply_iinf(struct poclidek_ctx *cctx, struct install_info *iinf)
{
    int i, n = 0;
        
    if (iinf == NULL)
        return;
    
    if (cctx->instpkgs) {
        for (i=0; i < n_array_size(iinf->uninstalled_pkgs); i++) {
            struct pkg *pkg = n_array_nth(iinf->uninstalled_pkgs, i);
            n_array_remove(cctx->instpkgs, pkg);
            n++;
            printf("- %s\n", pkg->nvr);
        }
        
        for (i=0; i < n_array_size(iinf->installed_pkgs); i++) {
            struct pkg *pkg = n_array_nth(iinf->installed_pkgs, i);
            n_array_push(cctx->instpkgs, pkg_link(pkg));
            n++;
        }
        n_array_sort(cctx->instpkgs);
        
        
        //printf("s = %d\n", n_array_size(cmdarg->cctx->instpkgs));
        if (n)
            cctx->ts_instpkgs = time(0);
    }
}
