/* 
  Copyright (C) 2000, 2001 Pawel A. Gajda (mis@k2.net.pl)
 
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
#include <time.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <argp.h>
#include <fnmatch.h>
#include <locale.h>

#include <pcre.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <trurl/trurl.h>

#include "pkg.h"
#include "pkgset.h"
#include "pkgdb.h"
#include "install.h"
#include "misc.h"
#include "rpm.h"
#include "log.h"
#include "shell.h"
#include "term.h"

#define DEFAULT_TERM_WIDTH  80
#define DEFAULT_TERM_HEIGHT 24

static int term_width  = DEFAULT_TERM_WIDTH;
static int term_height = DEFAULT_TERM_HEIGHT;


static unsigned argp_parse_flags = ARGP_NO_EXIT;
static volatile sig_atomic_t winch_reached = 0;


static int argv_is_help(int argc, const char **argv);

static int cmd_quit(struct cmdarg *cmdarg);
struct command command_quit = {
    COMMAND_NOARGS | COMMAND_NOHELP | COMMAND_NOOPTS,
    "quit", NULL, "Quit poldek", NULL, NULL, NULL, cmd_quit,
    NULL, NULL, NULL, NULL
};

static int cmd_help(struct cmdarg *cmdarg);
struct command command_help = {
    COMMAND_NOARGS | COMMAND_NOHELP | COMMAND_NOOPTS,
    "help", NULL, "Display this help", NULL, NULL, NULL, cmd_help,
    NULL, NULL, NULL, NULL
};

static 
int cmd_reload(struct cmdarg *cmdarg,
               int argc, const char **argv, struct argp *argp);

struct command command_reload = {
    COMMAND_NOARGS | COMMAND_NOOPTS, "reload", NULL, "Reload installed packages",
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
static tn_array       *all_commands; /* for command_generator() */
static char           *histfile;
static int            done = 0;

static struct shell_s shell_s = {NULL, NULL, 0, NULL, NULL};
static tn_array *compl_shpkgs = NULL;



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

int shpkg_ncmp_str(struct shpkg *pkg, const char *name) 
{
    return strncmp(pkg->nevr, name, strlen(name));
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
    rc = sh_cmdarg->parse_opt_fn(key, arg, state);
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
                if (arg == NULL)
                    verbose = 1;
                else  {
                    char *p = arg;
                    while (*p == 'v')
                    p++;
                    
                    if (*p != '\0') {
                        argp_usage (state);
                        sh_cmdarg->err = 1; 
                    } else {
                        verbose = p - arg + 1;
                    }
                }
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
            n += snprintf(&buf[n], sizeof(buf) - n, "  %s\n",
                          sh_cmdarg->cmd->extra_help);
            
        if (sh_cmdarg->cmd->aliases) {
            struct command_alias *aliases = sh_cmdarg->cmd->aliases;
            int i = 0;
            
            n += snprintf(&buf[n], sizeof(buf) - n, "  Defined aliases:\n");
            while (aliases[i].name) {
                n += snprintf(&buf[n], sizeof(buf) - n, "    %-12s  \"%s\"\n",
                              aliases[i].name, aliases[i].cmdline);
                i++;
            }
        }
        
        if (n > 0) {
            p = malloc(n + 1);
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
    cmdarg.sh_s->inst->instflags = 0;
    cmdarg.d = NULL;
    
    if (cmd->init_cmd_arg_d)
        cmdarg.d = cmd->init_cmd_arg_d();

    if (cmd->cmd_fn) {
        rc = cmd->cmd_fn(&cmdarg, argc, argv, &argp);
        goto l_end;
    }

    
    parse_flags = argp_parse_flags;

/*    if (cmd->flags & COMMAND_NOHELP)
      parse_flags |= ARGP_NO_HELP;*/

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
    
    if (cmdarg.pkgnames)
        n_array_free(cmdarg.pkgnames);

    if (cmdarg.shpkgs)
        n_array_free(cmdarg.shpkgs);
    
    
    if (cmd->destroy_cmd_arg_d && cmdarg.d)
        cmd->destroy_cmd_arg_d(cmdarg.d);
    
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
            log(LOGERR, "%s: no such command\n", line);
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


#define COMPL_CTX_AV_PKGS    0
#define COMPL_CTX_INST_PKGS  1
static void switch_pkg_completion(int ctx) 
{
    switch (ctx) {
        case COMPL_CTX_AV_PKGS:
            compl_shpkgs = shell_s.avpkgs;
            break;

        case COMPL_CTX_INST_PKGS:
            if (shell_s.instpkgs)
                compl_shpkgs = shell_s.instpkgs;
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
            name = strdup(cmd);
    }
    
    return name;
}

static
char *pkgname_generator(const char *text, int state)
{
    static int i, len;
    char *name = NULL;

    
    if (state == 0) {
        len = strlen(text);
        if (len == 0)
            i = 0;
        else 
            i = n_array_bsearch_idx_ex(compl_shpkgs, text,
                                       (tn_fn_cmp)shpkg_ncmp_str);
    }

    if (i > -1 && i < n_array_size(compl_shpkgs)) {
        struct shpkg *shpkg = n_array_nth(compl_shpkgs, i++);
        if (len == 0 || strncmp(shpkg->nevr, text, len) == 0) 
            name = strdup(shpkg->nevr);
    }
    
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
            switch_pkg_completion(COMPL_CTX_INST_PKGS);
        else 
            switch_pkg_completion(COMPL_CTX_AV_PKGS);
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


void sh_resolve_packages(tn_array *pkgnames, tn_array *avshpkgs, tn_array **pkgsp, int strict)
{
    tn_array *pkgs = NULL;
    int i, j;
    int *matches;
    
    
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
    
    pkgs = n_array_new(16, NULL, (tn_fn_cmp)shpkg_cmp);

    for (i=0; i<n_array_size(avshpkgs); i++) {
        struct shpkg *shpkg = n_array_nth(avshpkgs, i);
        
        for (j=0; j<n_array_size(pkgnames); j++) {
            if (fnmatch(n_array_nth(pkgnames, j), shpkg->nevr, 0) == 0) {
                n_array_push(pkgs, shpkg);
                matches[j]++;
            }
        }
    }

    
    for (j=0; j<n_array_size(pkgnames); j++) {
        if (matches[j] == 0) {
            log(LOGERR, "%s: no such package\n", (char*)n_array_nth(pkgnames, j));
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
            strcmp(argv[i], "--usage") == 0) {
            
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
    
    while (commands_tab[i]) {
        struct command *cmd = commands_tab[i++];
        char buf[256], *p;
        
        
        p = cmd->arg ? cmd->arg : "";
        if (cmd->argp_opts) {
            snprintf(buf, sizeof(buf), "[OPTION...] %s", cmd->arg);
            p = buf;
        }
        printf("%-9s %-36s %s\n", cmd->name, p, cmd->doc);
    }
    printf("\nFor now \"search\" and \"desc\" commands doesn't work with "
           "installed packages\n");
    
    printf("\nType COMMAND -? for details\n");
    return 0;
}

static
int cmd_quit(struct cmdarg *cmdarg)
{
    cmdarg = cmdarg;
    done = 1;
    return 1;
}


void db_map_fn(unsigned int recno, void *header, void *shpkgs) 
{
    struct shpkg      *shpkg;
    struct pkg        *pkg;
    char              nevr[1024];
    int               len;

    recno = recno;
    pkg = pkg_ldhdr(header, "db", 0, PKG_LDNEVR);
    pkg_snprintf(nevr, sizeof(nevr), pkg);
    
    len = strlen(nevr);
    shpkg = malloc(sizeof(*shpkg) + len + 1);
    memcpy(shpkg->nevr, nevr, len + 1);
    shpkg->pkg = pkg;
    shpkg->flags = 0;
    shpkg->_ucnt = 0;
    n_array_push(shpkgs, shpkg);
    if (n_array_size(shpkg) % 100 == 0)
        msg(0, "_.");
}

static tn_array *load_installed_packages(tn_array **shpkgsp) 
{
    struct pkgdb *db;
    tn_array *shpkgs = *shpkgsp;

    n_array_clean(*shpkgsp);
    msg(0, "Loading installed packages...");
    db = pkgdb_open(shell_s.inst->rootdir, NULL, O_RDONLY);
    rpm_dbmap(db->dbh, db_map_fn, shpkgs);
    pkgdb_free(db);
    
    n_array_sort(shpkgs);
    msg(0, "_done.\n");
    msg(0, "%d packages loaded\n", n_array_size(shpkgs));
    
    return shpkgs;
}


static 
int cmd_reload(struct cmdarg *cmdarg,
               int argc, const char **argv, struct argp *argp)
{
    argp = argp;
    cmdarg = cmdarg;
    
    if (argv_is_help(argc, argv)) {
        printf("Just type \"reload\"\n");
        return 1;
    }

    if (shell_s.instpkgs == NULL)
        shell_s.instpkgs = n_array_new(1024, (tn_fn_free)shpkg_free,
                                       (tn_fn_cmp)shpkg_cmp);
    load_installed_packages(&shell_s.instpkgs);
    return 1;
}



static void shell_end(int sig) 
{
    sig = sig;
    
    if (histfile)
        write_history(histfile);
    done = 1;
}



static void sig_winch(int signo)
{
    n_assert(signo == SIGWINCH);
    winch_reached = 1;
    signal(SIGWINCH, sig_winch);
}

static void update_term_width(void) 
{
    struct winsize ws;

    if (winch_reached) {
        if (ioctl(1, TIOCGWINSZ, &ws) == 0) {
            term_width = ws.ws_col;
            term_height = ws.ws_row;

        } else {
            term_width  = DEFAULT_TERM_WIDTH;
            term_height = DEFAULT_TERM_HEIGHT;
        }
        

        winch_reached = 0;
    }
}

int get_term_width(void)
{
    update_term_width();
    return term_width;
}

int get_term_height(void)
{
    update_term_width();
    return term_height;
}

    

static void init_commands(void) 
{
    int n = 0;
    
    commands = n_array_new(16, NULL, (tn_fn_cmp)command_cmp);
    aliases  = n_array_new(16, NULL, (tn_fn_cmp)command_alias_cmp);
    all_commands = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    
    while (commands_tab[n] != NULL) {
        struct command *cmd = commands_tab[n++];

        n_array_push(commands, cmd);
        if (n_array_bsearch(all_commands, cmd->name)) {
            log(LOGERR, "Ambigous command %s\n", cmd->name);
            exit(EXIT_FAILURE);
        }
        n_array_push(all_commands, cmd->name);
        n_array_sort(all_commands);
        
        if (cmd->aliases) {
            int i = 0;

            while (cmd->aliases[i].name) {
                if (n_array_bsearch(aliases, &cmd->aliases[i])) {
                    log(LOGERR, "Ambigous alias %s\n", cmd->aliases[i].name);
                    exit(EXIT_FAILURE);
                }
                
                n_array_push(aliases, &cmd->aliases[i]);
                n_array_sort(aliases);
                
                if (n_array_bsearch(all_commands, cmd->aliases[i].name)) {
                    log(LOGERR, "Ambigous alias %s\n", cmd->aliases[i].name);
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

    

int shell_main(struct pkgset *ps, struct inst_s *inst, int skip_installed)
{
    char *line, *s, *home;
    int i;

    argp_program_bug_address = NULL;
    
    if (!isatty(1)) {
        log(LOGERR, "not a tty\n");
        return 1;
    }

    term_init();
    winch_reached = 1;
    get_term_width();
    
    signal(SIGWINCH, sig_winch);

    setlocale (LC_ALL, "");
    
    if (shell_s.pkgset != NULL) {
        log(LOGERR, "shell_main: not reentrant func\n");
        return 0;
    }
    
    if (inst->rootdir == NULL)
        inst->rootdir = "/";

    shell_s.pkgset = ps;
    shell_s.inst = inst;
    shell_s.inst_flags_orig = inst->flags;
    
    shell_s.avpkgs = n_array_new(n_array_size(ps->pkgs), (tn_fn_free)shpkg_free,
                                 (tn_fn_cmp)shpkg_cmp);
    
    
    for (i=0; i<n_array_size(ps->pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->pkgs, i);
        struct shpkg *shpkg;
        char buf[1024];


        pkg_snprintf(buf, sizeof(buf), pkg);
        shpkg = malloc(sizeof(*shpkg) + strlen(buf) + 1);
        memcpy(shpkg->nevr, buf, strlen(buf) + 1);
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
        load_installed_packages(&shell_s.instpkgs);
    }
    
    initialize_readline();
    histfile = NULL;

    if ((home = getenv("HOME"))) {
        histfile = alloca(strlen(home) + strlen("/.poldek_history") + 2);
        sprintf(histfile, "%s/.poldek_history", home);
        read_history(histfile);
    }
    
    switch_pkg_completion(COMPL_CTX_AV_PKGS);
    
    init_commands();
    
    signal(SIGINT,  shell_end);
    signal(SIGTERM, shell_end);
    signal(SIGQUIT, shell_end);
    done = 0;
    
    printf("\nWelcome to the poldek shell mode. Type \"help\" for help with commands\n\n");
    
    while (done == 0) {
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
    }

    shell_end(0);
    histfile = NULL;
    return 1;
}
