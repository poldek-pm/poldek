/* 
  Copyright (C) 2000 - 2002 Pawel A. Gajda (mis@k2.net.pl)
 
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
#include <fnmatch.h>
#include <time.h>

#include <pcre.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <trurl/trurl.h>

#include "sigint/sigint.h"
#include "i18n.h"
#include "pkgdir.h"
#include "pkg.h"
#include "pkgset.h"
#include "pkgdb.h"
#include "install.h"
#include "misc.h"
#include "rpm.h"
#include "log.h"
#include "shell.h"

int shOnTTY = 0;

static volatile sig_atomic_t shDone   = 0;
static volatile sig_atomic_t shInCmd  = 0;

static int gmt_off = 0;         /* TZ offset */

static unsigned argp_parse_flags = ARGP_NO_EXIT;

static int argv_is_help(int argc, const char **argv);

static int cmd_quit(struct cmdarg *cmdarg);
struct command command_quit = {
    COMMAND_NOARGS | COMMAND_NOHELP | COMMAND_NOOPTS,
    "quit", NULL, N_("Quit poldek"), NULL, NULL, NULL, cmd_quit,
    NULL, NULL, NULL, NULL
};

static int cmd_help(struct cmdarg *cmdarg);
struct command command_help = {
    COMMAND_NOARGS | COMMAND_NOHELP | COMMAND_NOOPTS,
    "help", NULL, N_("Display this help"), NULL, NULL, NULL, cmd_help,
    NULL, NULL, NULL, NULL
};

static 
int cmd_reload(struct cmdarg *cmdarg,
               int argc, const char **argv, struct argp *argp);

struct command command_reload = {
    COMMAND_NOARGS | COMMAND_NOOPTS, "reload", NULL,
    N_("Reload installed packages"),
    NULL, NULL, cmd_reload, NULL, NULL, NULL, NULL, NULL
};


extern struct command command_ls;
extern struct command command_install;
extern struct command command_uninstall;
extern struct command command_get;
extern struct command command_search;
extern struct command command_desc;


struct command *commands_tab[] = {
    &command_ls,
    &command_install, 
    &command_uninstall,
    &command_get,
    &command_search,
    &command_desc, 
    &command_reload, 
    &command_help,
    &command_quit,
    NULL
};



struct sh_cmdarg {
    unsigned        cmdflags;
    int             err;
    struct cmdarg   *cmdarg;
    struct command  *cmd;   
    error_t (*parse_opt_fn)(int, char*, struct argp_state*);
};

static tn_array       *commands;
static tn_array       *aliases;
static tn_array       *all_commands; /* commands + aliases,
                                        for command_generator() */
static char           *histfile;



static struct shell_s shell_s = {NULL, NULL, 0, NULL, NULL, 0, NULL};

#define CMPLT_CTX_AVPKGS       0
#define CMPLT_CTX_AVPKGS_UPGR  1
#define CMPLT_CTX_INSTPKGS     2

struct completion_ctx {
    int      type;
    tn_array *shpkgs;
};

static struct completion_ctx cmplt_ctx = { 0, NULL };

static
int command_cmp(struct command *c1, struct command *c2) 
{
    return strcmp(c1->name, c2->name);
}

static
int command_alias_cmp(struct command_alias *a1, struct command_alias *a2)
{
    return strcmp(a1->name, a2->name);
}

int cmd_ncmp(const char *name, const char *s) 
{
    return strncmp(name, s, strlen(s));
}


int shpkg_cmp(struct shpkg *p1, struct shpkg *p2) 
{
    return strcmp(p1->nevr, p2->nevr);
}

int shpkg_cmp_rev(struct shpkg *p1, struct shpkg *p2) 
{
    return -strcmp(p1->nevr, p2->nevr);
}


int shpkg_ncmp_str(struct shpkg *pkg, const char *name) 
{
    return strncmp(pkg->nevr, name, strlen(name));
}

int shpkg_cmp_btime(struct shpkg *p1, struct shpkg *p2)
{
    register int cmprc;

    cmprc = p1->pkg->btime - p2->pkg->btime;
    if (cmprc == 0)
        cmprc = shpkg_cmp(p1, p2);

    return cmprc;
}

int shpkg_cmp_btime_rev(struct shpkg *p1, struct shpkg *p2)
{
    return -shpkg_cmp_btime(p1, p2);
}

int shpkg_cmp_bday(struct shpkg *p1, struct shpkg *p2)
{
    register int cmprc;
    
    cmprc = ((p1->pkg->btime + gmt_off) / 86400) -
        ((p2->pkg->btime + gmt_off) / 86400);
    
    if (cmprc == 0)
        cmprc = shpkg_cmp(p1, p2);
    
    return cmprc;
}

int shpkg_cmp_bday_rev(struct shpkg *p1, struct shpkg *p2)
{
    return -shpkg_cmp_bday(p1, p2); 
}



struct shpkg *shpkg_link(struct shpkg *shpkg) 
{
    shpkg->_ucnt++;
    return shpkg;
}

void shpkg_free(struct shpkg *shpkg) 
{
    if (shpkg->_ucnt) {
        shpkg->_ucnt--;
        return;
    }
    
    pkg_free(shpkg->pkg);
    free(shpkg);
}

#if 0
static
void shpkg_clean_flags(struct shpkg *shpkg) 
{
    shpkg->flags = 0;
}
#endif

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
            n_array_push(sh_cmdarg->cmdarg->pkgnames, arg);
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
            
        if (sh_cmdarg->cmd->aliases) {
            struct command_alias *aliases = sh_cmdarg->cmd->aliases;
            int i = 0;
            
            n += n_snprintf(&buf[n], sizeof(buf) - n, "%s", _("  Defined aliases:\n"));
            while (aliases[i].name) {
                n += n_snprintf(&buf[n], sizeof(buf) - n, "    %-16s  \"%s\"\n",
                              aliases[i].name, aliases[i].cmdline);
                i++;
            }
        }
        
        if (n > 0) {
            p = n_malloc(n + 1);
            return memcpy(p, buf, n + 1);
        }
    }
    
    return (char*)text;
}



static
int docmd(struct command *cmd, int argc, const char **argv)
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
    cmdarg.pkgnames = n_array_new(64, NULL, (tn_fn_cmp)strcmp);
    cmdarg.shpkgs = NULL;
    cmdarg.flags = 0;
    cmdarg.sh_s = &shell_s;
    cmdarg.sh_s->inst->flags = cmdarg.sh_s->inst_flags_orig;
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
    
    if (cmdarg.pkgnames)
        n_array_free(cmdarg.pkgnames);

    if (cmdarg.shpkgs)
        n_array_free(cmdarg.shpkgs);
    
    
    if (cmd->destroy_cmd_arg_d && cmdarg.d)
        cmd->destroy_cmd_arg_d(cmdarg.d);

    if ((cmd->flags & COMMAND_MODIFIESDB) && cmdarg.sh_s->ts_instpkgs > 0) {
        cmdarg.sh_s->dbpkgdir->ts = cmdarg.sh_s->ts_instpkgs;
        cmdarg.sh_s->ts_instpkgs = 0;
    }
    verbose = verbose_;
    return rc;
}


static
int execute_line(char *line)
{
    struct command *cmd, tmpcmd;
    char *p;
    const char **args;
    int rc = 0;

    p = line;
    while (*p && !isspace(*p))
	p++;
    
    if (*p)
        *p = '\0';
    else
        p = NULL;
    
    tmpcmd.name = line;
    if ((cmd = n_array_bsearch(commands, &tmpcmd)) == NULL) {
        struct command_alias *alias, tmpalias;
        
        
        tmpalias.name = tmpcmd.name;
        if ((alias = n_array_bsearch(aliases, &tmpalias)) == NULL) {
            logn(LOGERR, _("%s: no such command"), line);
            return 0;
            
        } else {
            char *l;
            int len = strlen(alias->cmdline) + 1;
            
            cmd = alias->cmd;

            if (p == NULL) {    /* no args */
                l = alloca(len);
                memcpy(l, alias->cmdline, len);
                
            } else {
                p++;
                len += strlen(p) + len + 1;
                l = alloca(len);
                snprintf(l, len, "%s %s", alias->cmdline, p);
                
                p = NULL;
            }
            //printf("alias exp %s -> %s\n", line, l);
            line = l;
        }
    }

    if (p)
        *p = ' ';
    
    if ((args = n_str_tokl(line, " \t"))) {
        int argc = 0;

        while (args[argc])
            argc++;
        
        rc = docmd(cmd, argc, args);
        n_str_tokl_free(args);
    }
    
    return rc;
}


static
char *stripwhite(char *string)
{
    register char *s, *t;

    for (s = string; whitespace(*s); s++);

    if (*s == 0)
	return (s);

    t = s + strlen(s) - 1;
    while (t > s && whitespace(*t))
	t--;
    *++t = '\0';

    return s;
}



static void switch_pkg_completion(int ctx_type) 
{
    switch (ctx_type) {
        case CMPLT_CTX_AVPKGS_UPGR:
        case CMPLT_CTX_AVPKGS:
            cmplt_ctx.shpkgs = shell_s.avpkgs;
            cmplt_ctx.type = ctx_type;
            break;

        case CMPLT_CTX_INSTPKGS:
            if (shell_s.instpkgs) {
                cmplt_ctx.shpkgs = shell_s.instpkgs;
                cmplt_ctx.type = ctx_type;
            }
            break;
            
        default:
            n_assert(0);
    }
}

static
char *command_generator(const char *text, int state)
{
    static int i, len;
    char *name = NULL;

    
    if (state == 0) {
        len = strlen(text);
        if (len == 0)
            i = 0;
        else 
            i = n_array_bsearch_idx_ex(all_commands, text, (tn_fn_cmp)cmd_ncmp);
    }

    if (i > -1 && i < n_array_size(all_commands)) {
        char *cmd = n_array_nth(all_commands, i++);
        if (len == 0 || strncmp(cmd, text, len) == 0) 
            name = n_strdup(cmd);
    }
    
    return name;
}

static
char *pkgname_generator(const char *text, int state)
{
    static int           i, len;
    char                 *name = NULL;
    
    
    if (state == 0) {
        len = strlen(text);
        if (len == 0)
            i = 0;
        else 
            i = n_array_bsearch_idx_ex(cmplt_ctx.shpkgs, text,
                                       (tn_fn_cmp)shpkg_ncmp_str);
    }

    
    while (i > -1 && i < n_array_size(cmplt_ctx.shpkgs)) {
        struct shpkg *shpkg = n_array_nth(cmplt_ctx.shpkgs, i++);
        
        if (len == 0 || strncmp(shpkg->nevr, text, len) == 0) {
            name = shpkg->nevr;
            
            if (cmplt_ctx.type == CMPLT_CTX_AVPKGS_UPGR && shell_s.instpkgs) {
                int found, cmprc;

                name = NULL;
                found = shpkg_cmp_lookup(shpkg, shell_s.instpkgs, 0, &cmprc,
                                         NULL, 0);
                
                if (found && cmprc > 0) {
                    name = shpkg->nevr;
                    break;
                }
                
            } else {            /* no additional criteria */
                break;
            }
            
        }
    }
    
    if (name)
        name = n_strdup(name);
    
    return name;
}

#ifndef HAVE_READLINE_4_2
# define rl_completion_matches(a, b) completion_matches(a, b)
#endif

static
char **poldek_completion(const char *text, int start, int end)
{
    char **matches;
    char *p;
    
    
    start = start;
    end = end;
    matches = NULL;

    p = rl_line_buffer;

    while (isspace(*p))
        p++;

    if (*p) {
        if (strncmp(p, "un", 2) == 0) /* uninstall cmd */
            switch_pkg_completion(CMPLT_CTX_INSTPKGS);
        
        else if (strncmp(p, "upgr", 4) == 0) /* upgrade cmd */
            switch_pkg_completion(CMPLT_CTX_AVPKGS_UPGR);
        
        else if (strncmp(p, "gree", 4) == 0) /* greedy-upgrade cmd */
            switch_pkg_completion(CMPLT_CTX_AVPKGS_UPGR);
        
        else 
            switch_pkg_completion(CMPLT_CTX_AVPKGS);
    }
        
    if (start == 0 || strchr(p, ' ') == NULL) 
	matches = rl_completion_matches(text, command_generator);
    else
        matches = rl_completion_matches(text, pkgname_generator);
    
    return matches;
}


static
void initialize_readline(void)
{
    rl_readline_name = "poldek";
    rl_attempted_completion_function = poldek_completion;
    rl_completion_entry_function = command_generator;
}


void sh_resolve_packages(tn_array *pkgnames, tn_array *avshpkgs, tn_array **pkgsp,
                         int strict)
{
    tn_array *pkgs = NULL;
    int i, j;
    int *matches, *matches_bycmp;
    
    
    *pkgsp = NULL;

    for (i=0; i<n_array_size(pkgnames); i++) {
        char *name = n_array_nth(pkgnames, i);
        
        if (*name == '*' && *(name + 1) == '\0') {
            *pkgsp = avshpkgs;
            return;
        }
    }

    matches = alloca(n_array_size(pkgnames) * sizeof(*matches));
    memset(matches, 0, n_array_size(pkgnames) * sizeof(*matches));

    matches_bycmp = alloca(n_array_size(pkgnames) * sizeof(*matches_bycmp));
    memset(matches_bycmp, 0, n_array_size(pkgnames) * sizeof(*matches_bycmp));
    
    
    pkgs = n_array_new(16, NULL, (tn_fn_cmp)shpkg_cmp);

    for (i=0; i<n_array_size(avshpkgs); i++) {
        struct shpkg *shpkg = n_array_nth(avshpkgs, i);
        
        for (j=0; j < n_array_size(pkgnames); j++) {
            char *mask = n_array_nth(pkgnames, j);

            if (strcmp(mask, shpkg->pkg->name) == 0) {
                n_array_push(pkgs, shpkg);
                matches_bycmp[j]++;
                matches[j]++;
                
            } else if (fnmatch(mask, shpkg->nevr, 0) == 0) {
                n_array_push(pkgs, shpkg);
                matches[j]++;
            }
        }
    }

    
    for (j=0; j < n_array_size(pkgnames); j++) {
        const char *mask = n_array_nth(pkgnames, j);
        
        if (matches[j] == 0) {
            logn(LOGERR, _("%s: no such package"), mask);
            if (strict && n_array_size(pkgs))
                n_array_clean(pkgs);
        }

        if (matches_bycmp[j] > 1) {
            int pri = strict ? LOGERR : LOGWARN;
            logn(pri, _("%s: ambiguous name"), mask);
            if (strict && n_array_size(pkgs))
                n_array_clean(pkgs);
        }
    }
    
    if (n_array_size(pkgs) > 0) {
        n_array_sort(pkgs);
        n_array_uniq(pkgs);
    }

    *pkgsp = pkgs;
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

extern char poldek_banner[];
static
int cmd_help(struct cmdarg *cmdarg)
{
    int i = 0;
    
    cmdarg = cmdarg;
    
    printf("%s\n", poldek_banner);
    while (commands_tab[i]) {
        struct command *cmd = commands_tab[i++];
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

static
int cmd_quit(struct cmdarg *cmdarg)
{
    cmdarg = cmdarg;
    shDone = 1;
    return 1;
}

static time_t mtime(const char *pathname) 
{
    struct stat st;
    
    if (stat(pathname, &st) != 0)
        return 0;

    return st.st_mtime;
}

char *mkdbcache_path(char *path, size_t size, const char *cachedir,
                     const char *dbfull_path)
{
    int len;
    char tmp[PATH_MAX], *p;
    
    n_assert(cachedir);
    if (*dbfull_path == '/')
        dbfull_path++;

    len = n_snprintf(tmp, sizeof(tmp), "%s", dbfull_path);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
        len--;
    }
    n_assert(len);
    p = tmp;

    while (*p) {
        if (*p == '/')
            *p = '.';
        p++;
    }
    
    snprintf(path, size, "%s/%s.dbcache.%s.gz", cachedir,
             default_pkgidx_name, tmp);
    
    return path;
}


static struct pkgdir *load_installed_pkgdir(int reload) 
{
    char dbfull_path[PATH_MAX], dbcache_path[PATH_MAX];
    const char *rootdir, *dbpath;
    struct pkgdir *dir = NULL;
    time_t mtime_rpmdb = 0, mtime_dbcache = 0;
    
    rootdir = shell_s.inst->rootdir;
    dbpath = rpm_get_dbpath();
    
    snprintf(dbfull_path, sizeof(dbfull_path), "%s%s",
             *(rootdir + 1) == '\0' ? "" : rootdir,
             dbpath != NULL ? dbpath : "");
    
    if (*dbfull_path == '\0')
        return NULL;

    
    mkdbcache_path(dbcache_path, sizeof(dbcache_path),
                   shell_s.inst->cachedir, dbfull_path);

    if (!reload) {              /* use cache */
        mtime_dbcache = mtime(dbcache_path);
        mtime_rpmdb = rpm_dbmtime(dbfull_path);
        
        if (mtime_rpmdb && mtime_dbcache && mtime_rpmdb < mtime_dbcache) {
            dir = pkgdir_new("db", dbcache_path, NULL, PKGDIR_NEW_VERIFY);
            if (dir != NULL) {
                msgn(1, _("Loading cache %s..."), dbcache_path);
                if (pkgdir_load(dir, NULL, PKGDIR_LD_NOUNIQ)) {
                    int n = n_array_size(dir->pkgs);
                    msgn(1, ngettext("%d package read",
                                     "%d packages read", n), n);
                    
                } else {
                    pkgdir_free(dir);
                    dir = NULL;
                }
            }
        }
    }
    
    
    if (dir == NULL) {
        mtime_rpmdb += 1;       /* will be saved on exit */
        dir = pkgdir_load_db(shell_s.inst->rootdir, dbpath);
    }
    

    if (dir == NULL) {
        logn(LOGERR, _("Load installed packages failed"));
        
    } else {
        dir->idxpath = n_strdup(dbcache_path);
        dir->ts = mtime_rpmdb;
    }
    
    return dir;
}

static void save_installed_pkgdir(struct pkgdir *pkgdir) 
{
    time_t mtime_rpmdb, mtime_dbcache;
    const char *dbpath, *rootdir;
    char dbfull_path[PATH_MAX], dbcache_path[PATH_MAX];
    
    rootdir = shell_s.inst->rootdir;
    dbpath = rpm_get_dbpath();
    
    snprintf(dbfull_path, sizeof(dbfull_path), "%s%s",
             *(rootdir + 1) == '\0' ? "" : rootdir,
             dbpath != NULL ? dbpath : "");
    
    if (*dbfull_path == '\0')
        return;
    mtime_rpmdb = rpm_dbmtime(dbfull_path);
    
    if (mtime_rpmdb > pkgdir->ts) /* changed outside poldek */
        return;

    if (mtime_rpmdb == pkgdir->ts) { /* not touched, check if cache exists  */
        mkdbcache_path(dbcache_path, sizeof(dbcache_path),
                       shell_s.inst->cachedir, dbfull_path);
        mtime_dbcache = mtime(dbcache_path);
        if (mtime_dbcache && mtime_dbcache >= pkgdir->ts)
            return;
    }
    
    pkgdir_create_idx(pkgdir, NULL, PKGDIR_CREAT_asCACHE |
                      PKGDIR_CREAT_wMD5 | PKGDIR_CREAT_woTOC);
}


static void map_fn_2shpkg(void *pkg, void *shpkgs) 
{
    struct shpkg      *shpkg;
    char              nevr[1024];
    int               len;


    len = pkg_snprintf(nevr, sizeof(nevr), pkg);
    shpkg = n_malloc(sizeof(*shpkg) + len + 1);
    memcpy(shpkg->nevr, nevr, len + 1);
    shpkg->pkg = pkg_link(pkg);
    shpkg->flags = 0;
    shpkg->_ucnt = 0;
    n_array_push(shpkgs, shpkg);
}

static tn_array *pkgs_to_shpkgs(tn_array **shpkgsp, tn_array *pkgs) 
{
    n_array_clean(*shpkgsp);
    n_array_map_arg(pkgs, map_fn_2shpkg, *shpkgsp);
    n_array_sort(*shpkgsp);
    return *shpkgsp;
}


static void map_fn_2pkg(void *shpkg, void *pkgs) 
{
    struct shpkg *shp = shpkg;
    n_array_push(pkgs, pkg_link(shp->pkg));
}

static tn_array *shpkgs_to_pkgs(tn_array **pkgsp, tn_array *shpkgs) 
{
    n_array_clean(*pkgsp);
    n_array_map_arg(shpkgs, map_fn_2pkg, *pkgsp);
    n_array_sort(*pkgsp);
    return *pkgsp;
}


int shpkg_cmp_lookup(struct shpkg *lshpkg, tn_array *shpkgs,
                     int compare_ver, int *cmprc,
                     char *evr, size_t size) 
{
    struct shpkg *shpkg = NULL;
    char name[256];
    int n, finded = 0;

    snprintf(name, sizeof(name), "%s-", lshpkg->pkg->name);
    n = n_array_bsearch_idx_ex(shpkgs, name, (tn_fn_cmp)shpkg_ncmp_str);

    if (n == -1)
        return 0;

    while (n < n_array_size(shpkgs)) {
        shpkg = n_array_nth(shpkgs, n++);

        if (strcmp(shpkg->pkg->name, lshpkg->pkg->name) == 0) {
            finded = 1;
            break;
        }

        if (*shpkg->pkg->name != *lshpkg->pkg->name)
            break;
    }
    
    if (!finded)
        return 0;
    
    if (compare_ver == 0)
        *cmprc = pkg_cmp_evr(lshpkg->pkg, shpkg->pkg);
    else 
        *cmprc = pkg_cmp_ver(lshpkg->pkg, shpkg->pkg);
    
    snprintf(evr, size, "%s-%s", shpkg->pkg->ver, shpkg->pkg->rel);
    
    return finded;
}


static int load_installed_packages(struct shell_s *sh_s, int reload) 
{
    struct pkgdir *pkgdir;
    
    if ((pkgdir = load_installed_pkgdir(reload)) == NULL)
        return 0;
    
    if (sh_s->dbpkgdir)
        pkgdir_free(sh_s->dbpkgdir);
    
    sh_s->dbpkgdir = pkgdir;
    sh_s->ts_instpkgs = pkgdir->ts;
    pkgs_to_shpkgs(&sh_s->instpkgs, pkgdir->pkgs);

    return 1;
}


static 
int cmd_reload(struct cmdarg *cmdarg,
               int argc, const char **argv, struct argp *argp)
{
    argp = argp;
    cmdarg = cmdarg;
    
    if (argv_is_help(argc, argv)) {
        printf(_("Just type \"reload\"\n"));
        return 1;
    }

    if (shell_s.instpkgs) {
        load_installed_packages(&shell_s, 1);
        
    } else {
        shell_s.instpkgs = n_array_new(1024, (tn_fn_free)shpkg_free,
                                       (tn_fn_cmp)shpkg_cmp);
        
        load_installed_packages(&shell_s, 0);
    }

    return 1;
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


static void sigint_cb(void)
{
    if (!shInCmd)
        shDone = 1;
}

static void shell_end(int sig) 
{
    if (sig > 0) {
        signal(sig, SIG_DFL);
        shDone = 1;
    }
}



static void init_commands(void) 
{
    int n = 0;
    
    commands = n_array_new(16, NULL, (tn_fn_cmp)command_cmp);
    aliases  = n_array_new(16, NULL, (tn_fn_cmp)command_alias_cmp);
    all_commands = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    
    while (commands_tab[n] != NULL) {
        struct command *cmd = commands_tab[n++];
        if (cmd->argp_opts)
            translate_argp_options(cmd->argp_opts);

        cmd->arg = _(cmd->arg);
        cmd->doc = _(cmd->doc);

        n_array_push(commands, cmd);
        if (n_array_bsearch(all_commands, cmd->name)) {
            logn(LOGERR, _("ambiguous command %s"), cmd->name);
            exit(EXIT_FAILURE);
        }
        n_array_push(all_commands, cmd->name);
        n_array_sort(all_commands);
        
        if (cmd->aliases) {
            int i = 0;

            while (cmd->aliases[i].name) {
                if (n_array_bsearch(aliases, &cmd->aliases[i])) {
                    logn(LOGERR, _("ambiguous alias %s"), cmd->aliases[i].name);
                    exit(EXIT_FAILURE);
                }
                
                n_array_push(aliases, &cmd->aliases[i]);
                n_array_sort(aliases);
                
                if (n_array_bsearch(all_commands, cmd->aliases[i].name)) {
                    logn(LOGERR, _("ambiguous alias %s"), cmd->aliases[i].name);
                    exit(EXIT_FAILURE);
                }
                n_array_push(all_commands, cmd->aliases[i].name);
                n_array_sort(all_commands);
                i++;
            }
        }
    }
    
    n_array_sort(commands);
}


static void setup_gmt_off(void) 
{
    time_t t;
    struct tm *tm;

    t = time(NULL);
    if ((tm = localtime(&t))) 
#ifdef HAVE_TM_GMTOFF
        gmt_off = tm->tm_gmtoff;
#elif defined HAVE_TM___GMTOFF
        gmt_off = tm->__tm_gmtoff;
#endif        
}

static
int init_shell_data(struct pkgset *ps, struct inst_s *inst, int skip_installed) 
{
    int i;

    setup_gmt_off();
    
    n_assert (shell_s.pkgset == NULL);
    
    if (inst->rootdir == NULL)
        inst->rootdir = "/";

    shell_s.pkgset = ps;
    shell_s.inst = inst;
    shell_s.inst_flags_orig = inst->flags;
    
    shell_s.avpkgs = n_array_new(n_array_size(ps->pkgs), (tn_fn_free)shpkg_free,
                                 (tn_fn_cmp)shpkg_cmp);
    
    
    for (i=0; i < n_array_size(ps->pkgs); i++) {
        struct pkg    *pkg = n_array_nth(ps->pkgs, i);
        struct shpkg  *shpkg;
        char          buf[1024];
        int           len;
        
        len = pkg_snprintf(buf, sizeof(buf), pkg);
        shpkg = n_malloc(sizeof(*shpkg) + len + 1);
        memcpy(shpkg->nevr, buf, len + 1);
        shpkg->pkg = pkg_link(pkg);
        shpkg->flags = 0;
        shpkg->_ucnt = 0;
        n_array_push(shell_s.avpkgs, shpkg);
    }
    
    n_array_ctl(shell_s.avpkgs, TN_ARRAY_AUTOSORTED);
    n_array_sort(shell_s.avpkgs);
    
    
    shell_s.instpkgs = NULL;
    if (skip_installed == 0) {
        shell_s.instpkgs = n_array_new(1024, (tn_fn_free)shpkg_free,
                                       (tn_fn_cmp)shpkg_cmp);
        n_array_ctl(shell_s.instpkgs, TN_ARRAY_AUTOSORTED);
        load_installed_packages(&shell_s, 0);
    }

    switch_pkg_completion(CMPLT_CTX_AVPKGS);
    init_commands();
    return 1;
}


int shell_exec(struct pkgset *ps, struct inst_s *inst, int skip_installed,
               const char *cmd) 
{
    char *s, *p;

    
    init_shell_data(ps, inst, skip_installed);
    
    p = alloca(strlen(cmd) + 1);
    memcpy(p, cmd, strlen(cmd) + 1);
    
    s = stripwhite(p);
    if (*s) 
        return execute_line(s);
    
    return 0;
}

static void sigint_reached_fn(void)
{
    logn(LOGNOTICE, "interrupt signal reached");
}

int shell_main(struct pkgset *ps, struct inst_s *inst, int skip_installed)
{
    char *line, *s, *home;
    
    
    if (!isatty(fileno(stdout))) {
        logn(LOGERR, _("not a tty"));
        return 0;
    }
    shOnTTY = 1;

    init_shell_data(ps, inst, skip_installed);
    term_init();
    initialize_readline();
    histfile = NULL;

    if ((home = getenv("HOME"))) {
        int len = strlen(home) + strlen("/.poldek_history") + 2;
        histfile = alloca(len);
        snprintf(histfile, len, "%s/.poldek_history", home);
        read_history(histfile);
    }

    sigint_init();
    sigint_reached_cb = sigint_reached_fn;
    sigint_push(sigint_cb);
    signal(SIGTERM, shell_end);
    signal(SIGQUIT, shell_end);
    
    printf(_("\nWelcome to the poldek shell mode. "
             "Type \"help\" for help with commands.\n\n"));

    shDone = 0;
    while (!shDone) {
        sigint_reset();
        if ((line = readline("poldek> ")) == NULL)
            break;
        
        s = stripwhite(line);
        if (*s) {
            add_history(s);
            //print_mem_info("BEFORE");
            execute_line(s);
            //print_mem_info("AFTER ");
        }
        free(line);

        signal(SIGTERM, shell_end);
        signal(SIGQUIT, shell_end);
    }

    if (histfile) 
        write_history(histfile);

    if (shell_s.dbpkgdir) {
        printf("\n");
        shpkgs_to_pkgs(&shell_s.dbpkgdir->pkgs, shell_s.instpkgs);
        save_installed_pkgdir(shell_s.dbpkgdir);
    }

    sigint_pop();
    return 1;
}
