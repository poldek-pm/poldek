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
#include "conf.h"
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

static struct poclidek_cmd *commands_tab[] = {
    &command_ls,
    &command_install, 
    &command_uninstall,
    &command_search,
    &command_desc,
    &command_cd,
    &command_pwd,
    NULL
};


#define CMD_CHAIN_ENT_CMD        (1 << 0)
#define CMD_CHAIN_ENT_PIPE       (1 << 1)
#define CMD_CHAIN_ENT_SEMICOLON  (1 << 2)
struct cmd_chain_ent {
    unsigned             flags;
    struct poclidek_cmd  *cmd;
    tn_array             *a_argv;
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
          struct poclidek_cmd *cmd,
          int argc, const char **argv)
{
    struct cmdarg        cmdarg;
    struct sh_cmdarg     sh_cmdarg;
    int                  rc = 1, verbose_;
    unsigned             parse_flags;
    struct argp          argp = { cmd->argp_opts,
                                  parse_opt,
                                  cmd->arg, cmd->doc, 0, 0, 0};

    
    verbose_ = verbose;
    if (argv == NULL)
        return 0;

    cmdarg.is_help = argv_is_help(argc, argv);
//    cmdarg.pkgs = NULL;
    cmdarg.flags = 0;
    cmdarg.cctx = cctx;
    cmdarg.ts = ts;
    if (cmdarg.ts == NULL)
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
    
    if ((cmd->flags & COMMAND_NOHELP) &&
        (cmd->flags & COMMAND_NOARGS) &&
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


tn_array *poclidek_get_current_pkgs_XXXX(struct poclidek_ctx *cctx)
{
    tn_array *pkgs;
    
    if (cctx->pkg_ctx == POLDEKCLI_PKGCTX_AVAIL) {
        poclidek_load_packages(cctx, 1);
        pkgs = cctx->avpkgs;
        
    } else {
        pkgs = cctx->instpkgs;
    }

    if (pkgs)
        pkgs = n_ref(pkgs);
    
    return pkgs;
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


int pkg_cmp_lookup(struct pkg *lpkg, tn_array *pkgs,
                   int compare_ver, int *cmprc,
                   char *evr, size_t size) 
{
    struct pkg *pkg = NULL;
    char name[256];
    int n, found = 0;

    snprintf(name, sizeof(name), "%s-", lpkg->name);
    n = n_array_bsearch_idx_ex(pkgs, name, (tn_fn_cmp)pkg_nvr_strncmp);

    if (n == -1)
        return 0;

    while (n < n_array_size(pkgs)) {
        pkg = n_array_nth(pkgs, n++);

        if (strcmp(pkg->name, lpkg->name) == 0) {
            found = 1;
            break;
        }

        if (*pkg->name != *lpkg->name)
            break;
    }
    
    if (!found)
        return 0;
    
    if (compare_ver == 0)
        *cmprc = pkg_cmp_evr(lpkg, pkg);
    else 
        *cmprc = pkg_cmp_ver(lpkg, pkg);
    
    snprintf(evr, size, "%s-%s", pkg->ver, pkg->rel);
    
    return found;
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

static
struct cmd_chain_ent *cmd_chain_ent_new(unsigned flags,
                                        struct poclidek_cmd *cmd, tn_array *a_argv)
{
    struct cmd_chain_ent *ent;
    ent = n_malloc(sizeof(*ent));
    ent->flags = flags;
    ent->cmd = cmd;
    
    if (a_argv)
        ent->a_argv = n_ref(a_argv);
    else
        ent->a_argv = NULL;
    
    return ent;
}


static
void cmd_chain_ent_free(struct cmd_chain_ent *ent)
{
    if (ent->a_argv) 
        n_array_free(ent->a_argv);
    free(ent);
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




static
struct poclidek_cmd *find_command(struct poclidek_ctx *cctx, const char *name)
{
    struct poclidek_cmd *cmd, tmpcmd;
    int  i, j, n;

    n_array_sort(cctx->commands);
	tmpcmd.name = (char*)name;
    if ((cmd = n_array_bsearch(cctx->commands, &tmpcmd)))
        return cmd;

    i = j = n_array_bsearch_idx_ex(cctx->commands, &tmpcmd,
                                   (tn_fn_cmp)poclidek_cmd_ncmp);

    if (i < 0) {
        logn(LOGERR, _("%s: no such command"), name);
        return cmd;
    }
    

    n = 1;
    j++;
    while (j < n_array_size(cctx->commands) &&
           poclidek_cmd_ncmp(n_array_nth(cctx->commands, j), &tmpcmd) == 0) {
        n++;
        j++;
    }
    
    if (n == 1)
        cmd = n_array_nth(cctx->commands, i);
    
    else
        logn(LOGERR, _("%s: ambiguous command"), name);
        
    return cmd;
}


static
int a_argv_contains_break(tn_array *a_argv)
{
    char              *brk = ";|";
    int               i;
    
    for (i=0; i < n_array_size(a_argv); i++) {
        char *arg = n_array_nth(a_argv, i);
        if (strchr(brk, *arg)) {
            n_assert(*(arg + 1) == '\0');
            return 1;
        }
    }
    return 0;
}

struct a_argv_ent {
    char      brk;
    tn_array  *a_argv;
};

static
void a_argv_ent_free(struct a_argv_ent *ent)
{
    if (ent->a_argv) 
        n_array_free(ent->a_argv);
    free(ent);
}

static
tn_array *a_argv_split(tn_array *a_argv, const char *brk)
{
    int               i, rc = 1;
    tn_array          *cmds, *tl;

    
    cmds = n_array_new(2, (tn_fn_free)a_argv_ent_free, NULL);
    tl = n_array_new(4, free, NULL);
    
    for (i=0; i < n_array_size(a_argv); i++) {
        char *arg = n_array_nth(a_argv, i);

        if (strchr(brk, *arg) == NULL) {
            n_array_push(tl, n_strdup(arg));
            
        } else {
            n_assert(*(arg + 1) == '\0');
            if (n_array_size(tl)) {
                struct a_argv_ent *ent;
                
                ent = n_malloc(sizeof(*ent));
                ent->brk = 0;
                ent->a_argv = tl;
                n_array_push(cmds, ent);
                
                ent = n_malloc(sizeof(*ent));
                ent->brk = *arg;
                ent->a_argv = NULL;
                n_array_push(cmds, ent);
                
                tl = n_array_new(4, free, NULL);
            }
        }
    }

    if (rc && n_array_size(tl)) {
        struct a_argv_ent *ent;
                
        ent = n_malloc(sizeof(*ent));
        ent->brk = 0;
        ent->a_argv = tl;
        n_array_push(cmds, ent);
        
    } else
        n_array_free(tl);
    
    if (!rc || n_array_size(cmds) == 0) {
        n_array_free(cmds);
        cmds = NULL;
    }
    
    return cmds;
}

static
tn_array *prepare_a_argv(struct poclidek_ctx *cctx, tn_array *cmd_chain, 
                         tn_array *a_argv)
{
    struct poclidek_cmd *cmd;
    tn_array            *tl;
    int                 rc = 0;

    
    n_assert(a_argv_contains_break(a_argv) == 0);

    if ((cmd = find_command(cctx, n_array_nth(a_argv, 0))) == NULL)
        return NULL;

    if ((cmd->flags & COMMAND_IS_ALIAS) == 0) {
        struct cmd_chain_ent *ent;
        ent = cmd_chain_ent_new(CMD_CHAIN_ENT_CMD, cmd, a_argv);
        n_array_push(cmd_chain, ent);
    
    } else {
        n_assert(cmd->cmdline);

        if ((tl = n_str_etokl(cmd->cmdline)) == NULL) {
            rc = 0;
            
        } else {
            int i;

            free(n_array_shift(a_argv));
            while (n_array_size(tl))
                n_array_unshift(a_argv, n_array_pop(tl));
            n_array_free(tl);
            
            if (!a_argv_contains_break(a_argv)) {
                prepare_a_argv(cctx, cmd_chain, a_argv);
                
            } else {
                tn_array *arr = a_argv_split(a_argv, ";|");
                
                for (i=0; i < n_array_size(arr); i++) {
                    struct a_argv_ent *ent = n_array_nth(arr, i);
                    if (ent->a_argv)
                        prepare_a_argv(cctx, cmd_chain, a_argv);
                    
                    else {
                        struct cmd_chain_ent *ent;
                        ent = cmd_chain_ent_new(CMD_CHAIN_ENT_CMD, cmd, a_argv);
                        n_array_push(cmd_chain, ent);
                    }
                }
                
                n_array_free(arr);
            }
        }
    }
    
    return cmd_chain;
}


static
tn_array *prepare_cmdline(struct poclidek_ctx *cctx, const char *line)
{
    tn_array  *cmd_chain, *arr, *tl;
    int       i;

    
    if ((tl = n_str_etokl(line)) == NULL) {
        logn(LOGERR, _("%s: parse error"), line);
        return NULL;
    }

    printf("line = (%s)\n", line);
    for (i=0; i<n_array_size(tl); i++)
        printf("tl[%d] = %s\n", i, (char*)n_array_nth(tl, i));

    cmd_chain = n_array_new(2, (tn_fn_free)cmd_chain_ent_free, NULL);
    arr = a_argv_split(tl, ";|");
                
    for (i=0; i < n_array_size(arr); i++) {
        struct a_argv_ent *ent = n_array_nth(arr, i);
        struct cmd_chain_ent *cent = NULL;

        switch (ent->brk) {
            case ';':
                cent = cmd_chain_ent_new(CMD_CHAIN_ENT_SEMICOLON, NULL, NULL);
                break;

            case '|':
                cent = cmd_chain_ent_new(CMD_CHAIN_ENT_PIPE, NULL, NULL);
                break;
                
            case 0:
                if (ent->a_argv) {
                    int j;
                    
                    for (j=0; j < n_array_size(ent->a_argv); j++)
                        printf("tl[%d][%d] = %s\n", i, j,
                               (char*)n_array_nth(ent->a_argv, j));
                    
                    prepare_a_argv(cctx, cmd_chain, ent->a_argv);
                    break;
                }
                                /* no break */

            default:
                n_assert(0);
                break;
        }
        
    }
    n_array_free(arr);
    n_array_free(tl);

    return cmd_chain;
}


int poclidek_exec_line(struct poclidek_ctx *cctx, struct poldek_ts *ts,
                       const char *cmdline) 
{
    tn_array            *cmd_chain;
    int                 nerr = 0, arg_ts, i, j;

    printf("exec_line = %s\n", cmdline);
    cmd_chain = prepare_cmdline(cctx, cmdline);

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
