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

static volatile sig_atomic_t winch_reached = 0;
static int term_width = 80;

static void get_term_width();   /* updates term_width */

extern int shell_uninstall_pkgs(tn_array *pkgnevrs, struct inst_s *inst);




static unsigned argp_parse_flags = ARGP_NO_EXIT;

static int cmd_help(int argc, const char **argv, struct argp*);
static int cmd_quit(int argc, const char **argv, struct argp*);

/* ls */
static int cmd_ls(int argc, const char **argv, struct argp*);
static error_t parse_ls_opt(int key, char *arg, struct argp_state *state);


#define OPT_LS_LONG            (1 << 0) /* cmd_state->flags */
#define OPT_LS_UPGRADEABLE     (1 << 1) /* cmd_state->flags */
#define OPT_LS_UPGRADEABLE_VER (1 << 2) /* cmd_state->flags */
#define OPT_LS_INSTALLED       (1 << 3) /* cmd_state->flags */

static struct argp_option options_ls[] = {
 { "long", 'l', 0, 0, "Use a long listing format", 1},
 { "upgradeable", 'u', 0, 0, "Show upgradeable packages only", 1},
 { "upgradeablev", 'U', 0, 0, "Like above but omit packages with diffrent releases only", 1},
 { "installed", 'I', 0, 0, "List installed packages", 1},
 { 0, 0, 0, 0, 0, 0 },
};

/* desc */
static int cmd_desc(int argc, const char **argv, struct argp*);
static error_t parse_desc_opt(int key, char *arg, struct argp_state *state);

static struct argp_option options_desc[] = {
{0,0,0,0, "desc [PACKAGE...]", 1 },
{ 0, 0, 0, 0, 0, 0 },    
};


/* install */
static int cmd_install(int argc, const char **argv, struct argp*);
static
error_t parse_install_opt(int key, char *arg, struct argp_state *state);


#define OPT_INST_FETCH    1
#define OPT_INST_NODEPS   2
#define OPT_INST_FORCE    3
#define OPT_INST_INSTALL  1

static struct argp_option options_install[] = {
{"mercy", 'm', 0, OPTION_HIDDEN, "Be tolerant for bugs which RPM tolerates", 1},
{"force", OPT_INST_FORCE, 0, 0, "Be unconcerned", 1 },
{"test", 't', 0, 0, "Don't install, but tell if it would work or not", 1 },
{"freshen", 'F', 0, 0, "Upgrade packages, but only if an earlier version "
     "currently exists", 1 },
{0, 'I', 0, 0, "Install, not upgrade packages", 1 },

{"fetch", OPT_INST_FETCH, "DIR", OPTION_HIDDEN,
 "Do not install, only fetch packages", 1},

{"nodeps", OPT_INST_NODEPS, 0, 0,
 "Install packages with broken dependencies", 1 },


{0,  'v', "v...", OPTION_ARG_OPTIONAL, "Be more (and more) verbose.", 1 },
{ 0, 0, 0, 0, 0, 0 },
};


/* uninstall */
static int cmd_uninstall(int argc, const char **argv, struct argp*);
static
error_t parse_uninstall_opt(int key, char *arg, struct argp_state *state);


#define OPT_UNINST_NODEPS  2
#define OPT_UNINST_FORCE   3

static struct argp_option options_uninstall[] = {
{"mercy", 'm', 0, OPTION_HIDDEN, "Be tolerant for bugs which RPM tolerates", 1},
{"force", OPT_INST_FORCE, 0, 0, "Be unconcerned", 1 },
{"test", 't', 0, 0, "Don't uninstall, but tell if it would work or not", 1 },
{"nodeps", OPT_INST_NODEPS, 0, 0,
 "Ignore broken dependencies", 1 },
{0,  'v', "v...", OPTION_ARG_OPTIONAL, "Be more (and more) verbose.", 1 },
{ 0, 0, 0, 0, 0, 0 },
};


/* get */
static int cmd_get(int argc, const char **argv, struct argp*);
static error_t parse_get_opt(int key, char *arg, struct argp_state *state);


#define OPT_GET_VERBOSE      (1 << 0) /* cmd_state->flags */
#define OPT_GET_DIR          (1 << 1) /* cmd_state->flags */

static struct argp_option options_get[] = {
 { 0, 'v', 0, 0, "Be verbose", 1},
 { "dir", 'd', "DIR", 0, "Download to directory DIR instead to current one", 1},
 { 0, 0, 0, 0, 0, 0 },
};



static int cmd_reload(int argc, const char **argv, struct argp*);


struct command {
    int no;
    char *name;
    char *arg;
    struct argp_option *argp_opts;
    error_t (*parse_opt_fn)(int, char*, struct argp_state*);
    int (*fn)(int argc, const char **argv, struct argp*);
    char *doc;
};

#define CMD_LS         1
#define CMD_INF0       2
#define CMD_INSTALL    3
#define CMD_UNINSTALL  4
#define CMD_DESC       5
#define CMD_RELOAD     6
#define CMD_GET        7

#define CMD_QUIT       20
#define CMD_HELP       21

struct command commands_tab[] = {
    { CMD_LS, "ls", "[PACKAGE...]",
      options_ls,
      parse_ls_opt,
      cmd_ls,
      "List packages"},

/*{ CMD_INF0, "info", "[PACKAGE...]", NULL, NULL, cmd_ls, "display package(s) info"},*/
    
/*{"fetch", "[FILE...]", cmd_fetch, "fetch package(s)"},*/

{ CMD_INSTALL, "install", "PACKAGE...",
      options_install, parse_install_opt, cmd_install,
      "Install given packages." },
    
{ CMD_UNINSTALL, "uninstall", "PACKAGE...", options_uninstall,
      parse_uninstall_opt, cmd_uninstall,
      "Uninstall given packages" },

{ CMD_RELOAD, "reload", NULL, NULL, NULL, cmd_reload,
      "Reload installed package list"},

{ CMD_DESC, "desc", "PACKAGE", options_desc, parse_desc_opt, cmd_desc, 
      "Print package description"},    

{ CMD_GET, "get", "PACKAGE...", options_get, parse_get_opt, cmd_get,
      "Just download given package list"},
    
{ CMD_QUIT, "quit", NULL, NULL, NULL, cmd_quit, "Quit poldek"},
{ CMD_HELP, "help", NULL, NULL, NULL, cmd_help, "Display this help"},
    
{ 0, NULL, NULL, NULL, NULL, NULL, NULL }

};

    

#define SHPKG_INSTALL   (1 << 0)
#define SHPKG_UNINSTALL (1 << 1)

struct shell_pkg {
    struct pkg  *pkg;
    unsigned    flags;
    int16_t     _ucnt;
    int8_t      free_pkg; 
    char        nevr[0];
};

struct shell_s {
    struct pkgset  *pkgset;
    struct inst_s  *inst;
    unsigned       inst_flags_orig;
    tn_array       *avpkgs;     /* array of shell_pkgs  */
    tn_array       *instpkgs;   /* array of shell_pkgs  */
    tn_array       *commands;
    char           *histfile;
    int            done;
};

static struct shell_s shell_s = {NULL, NULL, 0, NULL, NULL, NULL, NULL, 0};

static tn_array    *compl_shpkgs = NULL;

struct cmd_state {
    unsigned flags;
    tn_array *pkgnames;
};

static
int command_cmp(struct command *c1, struct command *c2) 
{
    return strcmp(c1->name, c2->name);
}

static
int shpkg_cmp(struct shell_pkg *p1, struct shell_pkg *p2) 
{
    return strcmp(p1->nevr, p2->nevr);
}

int shpkg_cmp_rm_uninstalled(struct shell_pkg *p1, struct shell_pkg *p2) 
{
    p2 = p2;
    
    if (p1->flags & SHPKG_UNINSTALL)
        return 0;
    
    return -1;
}

static
int shpkg_ncmp_str(struct shell_pkg *pkg, const char *name) 
{
    return strncmp(pkg->nevr, name, strlen(name));
}

static
struct shell_pkg *shpkg_link(struct shell_pkg *shpkg) 
{
    shpkg->_ucnt++;
    return shpkg;
}

static
void shpkg_free(struct shell_pkg *shpkg) 
{
    if (shpkg->_ucnt)
        shpkg->_ucnt--;

    else {
        if (shpkg->free_pkg) 
            pkg_free(shpkg->pkg);
        
        free(shpkg);
    }
}


static
void shpkg_clean_flags(struct shell_pkg *shpkg) 
{
    shpkg->flags = 0;
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
    if ((cmd = n_array_bsearch(shell_s.commands, &tmpcmd)) == NULL) {
	fprintf(stderr, "%s: no such command\n", line);
	return 0;
    }

    if (p)
        *p = ' ';
    
    if ((args = n_str_tokl(line, " \t"))) {
        struct argp argp = { cmd->argp_opts, cmd->parse_opt_fn,
                             cmd->arg, cmd->doc, 0, 0, 0};
        int argc = 0;

        while (args[argc])
            argc++;
        
        rc = cmd->fn(argc, args, &argp);
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
    static int list_index, len;
    char *name;

    if (!state) {
	list_index = 0;
	len = strlen(text);
    }

    while ((name = commands_tab[list_index].name)) {
	list_index++;

	if (strncmp(name, text, len) == 0) {
            if (strcmp(name, "uninstall") == 0) 
                switch_pkg_completion(COMPL_CTX_INST_PKGS);
            else 
                switch_pkg_completion(COMPL_CTX_AV_PKGS);
            
	    return strdup(name);
        }
    }

    /* If no names matched, then return NULL. */
    return NULL;
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
        struct shell_pkg *shpkg = n_array_nth(compl_shpkgs, i++);
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
    
    end = end;
    matches = NULL;

    if (strncmp(rl_line_buffer, "un", 2) == 0) /* uninstall cmd */
        switch_pkg_completion(COMPL_CTX_INST_PKGS);
    else 
        switch_pkg_completion(COMPL_CTX_AV_PKGS);
    
    if (start == 0)
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
    rl_completion_entry_function = pkgname_generator;
}


static 
void resolve_packages(tn_array *pkgnames, tn_array *avshpkgs, tn_array **pkgsp, int strict)
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
        struct shell_pkg *shpkg = n_array_nth(avshpkgs, i);
        
        for (j=0; j<n_array_size(pkgnames); j++) {
            if (fnmatch(n_array_nth(pkgnames, j), shpkg->nevr, 0) == 0) {
                n_array_push(pkgs, shpkg);
                matches[j]++;
            }
        }
    }

    
    for (j=0; j<n_array_size(pkgnames); j++) {
        if (matches[j] == 0) {
            printf("%s: no such package\n", (char*)n_array_nth(pkgnames, j));
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


static int find_pkg(struct shell_pkg *lshpkg, tn_array *shpkgs, int compare_ver, 
                    int *cmprc, char *evr, size_t size) 
{
    struct shell_pkg *shpkg = NULL;
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


/*----------------------------------------------------------------------*/
/* ls                                                                   */
/*----------------------------------------------------------------------*/
static
error_t parse_ls_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_state *cmdst = state->input;
    
/*    if (arg)
      chkarg(key, arg);*/
    
    switch (key) {
        case 'l':
            cmdst->flags |= OPT_LS_LONG;
            break;

        case 'u':
            if (shell_s.instpkgs == NULL)
                printf("installed packages not loaded\n");
            else 
                cmdst->flags |= OPT_LS_UPGRADEABLE;
            break;

        case 'U':
            if (shell_s.instpkgs == NULL)
                printf("installed packages not loaded\n");
            else 
                cmdst->flags |= OPT_LS_UPGRADEABLE_VER | OPT_LS_UPGRADEABLE;
            break;

        case 'I':
            if (shell_s.instpkgs == NULL)
                printf("installed packages not loaded\n");
            else 
                cmdst->flags |= OPT_LS_INSTALLED;
            break;

        case ARGP_KEY_ARG:
            n_array_push(cmdst->pkgnames, strdup(arg));

        case ARGP_KEY_END:
            //argp_usage (state);
            break;
            
        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int cmd_ls(int argc, const char **argv, struct argp *argp)
{
    struct cmd_state     cmdst = { 0, NULL};
    tn_array             *shpkgs = NULL, *av_shpkgs;
    char                 hdr[128], fmt_hdr[256], fmt_pkg[256];
    int                  i, size, err = 0, npkgs = 0, is_help = 0;
    int                  compare_ver = 0;
    int                  term_width_div2;
    
    if (argv == NULL)
        return 0;

    /* argp workaround */
    for (i=0; i<argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-?") == 0) {
            is_help = 1;
            break;
        }
    }
            
    cmdst.pkgnames = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    argp_parse(argp, argc, (char**)argv, argp_parse_flags, 0, (void*)&cmdst);

    if (is_help) {
        n_array_free(cmdst.pkgnames);
        return 1;
    }
    
    if (cmdst.flags & OPT_LS_INSTALLED && shell_s.instpkgs) 
        av_shpkgs = shell_s.instpkgs;
    else
        av_shpkgs = shell_s.avpkgs;
    
    if (n_array_size(cmdst.pkgnames)) 
        resolve_packages(cmdst.pkgnames, av_shpkgs, &shpkgs, 0);
    else 
        shpkgs = av_shpkgs;

    n_array_free(cmdst.pkgnames);
    
    if (shpkgs == NULL) 
        return 0;

    if (n_array_size(shpkgs) == 0) {
        n_array_free(shpkgs);
        return 0;
    }

    get_term_width();
    term_width_div2 = term_width/2;

    *hdr = '\0';
    if (cmdst.flags & OPT_LS_LONG) {
        if ((cmdst.flags & OPT_LS_UPGRADEABLE) == 0) {
            snprintf(fmt_hdr, sizeof(fmt_hdr), "%%-%ds%%-%ds%%%ds\n",
                     term_width_div2 + term_width_div2/10, (term_width/7), 15);

            snprintf(fmt_pkg, sizeof(fmt_pkg), "%%-%ds%%%ds%%%ds\n",
                     term_width_div2 + term_width_div2/10, (term_width/7), (term_width/8));
       
            snprintf(hdr, sizeof(hdr), fmt_hdr, "package", "build date", "size");

            
        } else {
            snprintf(fmt_hdr, sizeof(fmt_hdr), "%%-%ds%%-%ds%%-%ds%%%ds\n",
                     (term_width/2) - 1, (term_width/6) - 1,
                     (term_width/6) - 1, (term_width/5) - 1);

            snprintf(fmt_pkg, sizeof(fmt_pkg), "%%-%ds%%-%ds%%-%ds%%%ds\n",
                     (term_width/2) - 1, (term_width/6) - 1,
                     (term_width/6) - 1, (term_width/6) - 1);

            //printf("fmt = %s, %s\n", fmt_hdr, fmt_pkg);
            
            if (cmdst.flags & OPT_LS_INSTALLED) 
                snprintf(hdr, sizeof(hdr), fmt_hdr, "installed",
                         "available", "build date", "size");
            else
                snprintf(hdr, sizeof(hdr), fmt_hdr, "available",
                         "installed", "build date", "size");
        }
    }
    
    compare_ver = cmdst.flags & OPT_LS_UPGRADEABLE_VER;
    
    size = 0;
    for (i=0; i<n_array_size(shpkgs); i++) {
        struct shell_pkg *shpkg = n_array_nth(shpkgs, i);
        struct pkg *pkg = shpkg->pkg;
        char evr[128]; 
        int cmprc = 0;
        
        
        if (cmdst.flags & OPT_LS_UPGRADEABLE && shell_s.instpkgs) {
            int finded;
            
            if (cmdst.flags & OPT_LS_INSTALLED) {
                finded = find_pkg(shpkg, shell_s.avpkgs, compare_ver, 
                                  &cmprc, evr, sizeof(evr));
                
            } else {
                finded = find_pkg(shpkg, shell_s.instpkgs, compare_ver,
                                  &cmprc, evr, sizeof(evr));
                cmprc = -cmprc;
            }
            
            if (!finded || cmprc >= 0)
                continue;
        }
        
        if (npkgs == 0)
            printf("%s", hdr);
        
        if (cmdst.flags & OPT_LS_LONG) {
            char timbuf[30];
            char sizbuf[30];
            char unit = 'K';
            double pkgsize = pkg->size/1024;

            if (pkgsize > 1000) {
                pkgsize /= 1024;
                unit = 'M';
            }

            snprintf(sizbuf, sizeof(sizbuf), "%.1f%c", pkgsize, unit);
            
            if (pkg->btime) 
                strftime(timbuf, sizeof(timbuf), "%Y/%m/%d %H:%M",
                         localtime((time_t*)&pkg->btime));
            else
                *timbuf = '\0';
            
            if (cmdst.flags & OPT_LS_UPGRADEABLE) 
                printf(fmt_pkg, shpkg->nevr, evr, timbuf, sizbuf);
            else
                printf(fmt_pkg, shpkg->nevr, timbuf, sizbuf);
            
            size += pkg->size/1024;
            
        } else {
            printf("%s\n", shpkg->nevr);
        }
        npkgs++;
    }

    if (cmdst.flags & OPT_LS_LONG && n_array_size(shpkgs)) {
        char *unit;
        int val;
        
        if (size > 1000) {
            unit = "MB";
            val = size/1000;
        } else {
            unit = "kB";
            val = size;
        }

        if (npkgs > 1)
            printf("%d packages, %d %s\n", npkgs, val, unit);
    }
    
    if (shpkgs != av_shpkgs)
        n_array_free(shpkgs);

    return err == 0;
}

/*----------------------------------------------------------------------*/
/* install                                                              */
/*----------------------------------------------------------------------*/
static
error_t parse_install_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_state *cmdst = state->input;

/*    if (arg)
      chkarg(key, arg);*/
    
    switch (key) {
        case 'q':
            verbose = -1;
            break;
            
        case 'v': {
            if (arg == NULL)
                verbose = 1;
            else  {
                char *p = arg;
                while (*p == 'v')
                    p++;
                if (*p != '\0')
                    argp_usage (state);
                else 
                    verbose = p - arg + 1;
            }
        }
        break;

        case 'm':
            /*argsp->psflags |= PSVERIFY_MERCY;*/
            break;

        case OPT_INST_NODEPS:
            shell_s.inst->instflags  |= PKGINST_NODEPS;
            break;
            
        case OPT_INST_FORCE:
            shell_s.inst->instflags |= PKGINST_FORCE;
            break;
            
        case 't':
            shell_s.inst->instflags |= PKGINST_TEST;
            break;

        case 'F':
            shell_s.inst->flags |= INSTS_FRESHEN;
            break;

        case 'I':
            shell_s.pkgset->flags |= PSMODE_INSTALL;
            shell_s.pkgset->flags &= ~PSMODE_UPGRADE;
            break;
            
        case ARGP_KEY_ARG:
            n_array_push(cmdst->pkgnames, arg);
            
        case ARGP_KEY_END:
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int cmd_install(int argc, const char **argv, struct argp *argp)
{
    struct cmd_state cmdst = { 0, NULL};
    tn_array *shpkgs = NULL;
    int i, err = 0;

    shell_s.inst->flags = shell_s.inst_flags_orig;
    shell_s.inst->instflags = 0;
    shell_s.pkgset->flags &= ~PSMODE_INSTALL;
    shell_s.pkgset->flags |= PSMODE_UPGRADE;
    
    cmdst.pkgnames = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    
    argp_parse(argp, argc, (char**)argv, argp_parse_flags, 0, &cmdst);

    resolve_packages(cmdst.pkgnames, shell_s.avpkgs, &shpkgs, 1);
    
    if (shpkgs == NULL) {
        err++;
        goto l_end;
    }
        
    if (n_array_size(shpkgs) == 0) {
        printf("install: specify what packages you want to install\n");
        err++;
        goto l_end;
    }
    
    if (err) 
        goto l_end;

    pkgset_unmark(shell_s.pkgset, PS_MARK_UNMARK_ALL);
    
    for (i=0; i<n_array_size(shpkgs); i++) {
        struct shell_pkg *shpkg = n_array_nth(shpkgs, i);
        pkg_hand_mark(shpkg->pkg);
    }

    if (install_pkgs(shell_s.pkgset, shell_s.inst) && shell_s.instpkgs) {
        for (i=0; i<n_array_size(shpkgs); i++)
            n_array_push(shell_s.instpkgs, shpkg_link(n_array_nth(shpkgs, i)));
        n_array_sort(shell_s.instpkgs);
        
    } else {
        printf("Installation failed\n");
    }
    
    
 l_end:

    if (cmdst.pkgnames)
        n_array_free(cmdst.pkgnames);
    
    if (shpkgs != shell_s.avpkgs)
        n_array_free(shpkgs);
    n_array_map(shell_s.avpkgs, (tn_fn_map1)shpkg_clean_flags);
    return err == 0;
}


static
error_t parse_uninstall_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_state *cmdst = state->input;

/*    if (arg)
      chkarg(key, arg);*/
    
    switch (key) {
        case 'q':
            verbose = -1;
            break;
            
        case 'v': {
            if (arg == NULL)
                verbose = 1;
            else  {
                char *p = arg;
                while (*p == 'v')
                    p++;
                if (*p != '\0')
                    argp_usage (state);
                else 
                    verbose = p - arg + 1;
            }
        }
        break;

        case 'm':
            /*argsp->psflags |= PSVERIFY_MERCY;*/
            break;

        case OPT_INST_NODEPS:
            shell_s.inst->instflags  |= PKGINST_NODEPS;
            break;
            
        case OPT_INST_FORCE:
            shell_s.inst->instflags |= PKGINST_FORCE;
            break;
            
        case 't':
            shell_s.inst->instflags |= PKGINST_TEST;
            break;

        case ARGP_KEY_ARG:
            n_array_push(cmdst->pkgnames, arg);
            
        case ARGP_KEY_END:
            //argp_usage (state);
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int cmd_uninstall(int argc, const char **argv, struct argp *argp)
{
    struct cmd_state cmdst = { 0, NULL};
    tn_array *shpkgs = NULL;
    int i, err = 0;
    tn_array *pkgnevrs;

    if (shell_s.instpkgs == NULL) {
        printf("uninstall: installed packages not loaded\n");
        return 0;
    }
    
    
    shell_s.inst->flags = shell_s.inst_flags_orig;
    shell_s.inst->instflags = 0;
    cmdst.pkgnames = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    argp_parse(argp, argc, (char**)argv, argp_parse_flags, 0, &cmdst);
    resolve_packages(cmdst.pkgnames, shell_s.instpkgs, &shpkgs, 1);
    n_array_free(cmdst.pkgnames);
    
    if (shpkgs == NULL)
        return 0;

    if (n_array_size(shpkgs) == 0) {
        printf("uninstall: specify what packages you want to uninstall\n");
        err++;
        goto l_end;
    }

    if (shpkgs == shell_s.instpkgs) {
        printf("uninstall: better do \"rm -rf /\"\n");
        return 0;
    }
    
    if (err) 
        goto l_end;
    
    pkgnevrs = n_array_new(n_array_size(shpkgs), NULL, (tn_fn_cmp)strcmp);
    
    for (i=0; i<n_array_size(shpkgs); i++) {
        struct shell_pkg *shpkg = n_array_nth(shpkgs, i);
        
        shpkg->flags |= SHPKG_UNINSTALL;
        n_array_push(pkgnevrs, shpkg->nevr);
    }
    
    if (shell_uninstall_pkgs(pkgnevrs, shell_s.inst))
        n_array_remove_ex(shell_s.instpkgs, NULL,
                          (tn_fn_cmp)shpkg_cmp_rm_uninstalled);
    else
        err = 1;
    
 l_end:
    return err == 0;
}


/*----------------------------------------------------------------------*/
/* get                                                                   */
/*----------------------------------------------------------------------*/
static
error_t parse_get_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_state *cmdst = state->input;
    
/*    if (arg)
      chkarg(key, arg);*/
    
    switch (key) {
        case 'd':
            shell_s.inst->fetchdir = trimslash(arg);
            break;

        case 'v':
            verbose = 1;
            break;

        case ARGP_KEY_ARG:
            n_array_push(cmdst->pkgnames, strdup(arg));

        case ARGP_KEY_END:
            //argp_usage (state);
            break;
            
        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int cmd_get(int argc, const char **argv, struct argp *argp)
{
    struct cmd_state cmdst = { 0, NULL};
    tn_array *shpkgs = NULL, *av_shpkgs, *pkgs;
    char destdir[PATH_MAX], *destdirp;
    int i, err = 0;
    
    
    if (argv == NULL)
        return 0;

    shell_s.inst->flags = shell_s.inst_flags_orig | INSTS_JUSTFETCH;
    shell_s.inst->instflags = 0;
    shell_s.inst->fetchdir = NULL;
    
    cmdst.pkgnames = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    argp_parse(argp, argc, (char**)argv, argp_parse_flags, 0, (void*)&cmdst);

    av_shpkgs = shell_s.avpkgs;
    
    if (n_array_size(cmdst.pkgnames)) 
        resolve_packages(cmdst.pkgnames, av_shpkgs, &shpkgs, 1);
    else 
        return 0;

    if (shpkgs == NULL || n_array_size(shpkgs) == 0)
        return 0;
    
   /* build array if struct pkg */
    pkgs = n_array_new(n_array_size(shpkgs), NULL, NULL);
    for (i=0; i<n_array_size(shpkgs); i++) {
        struct shell_pkg *shpkg = n_array_nth(shpkgs, i);
        n_array_push(pkgs, shpkg->pkg);
    }


    if (shell_s.inst->fetchdir != NULL) {
        destdirp = (char*)shell_s.inst->fetchdir;
        
    } else {
        if (getcwd(destdir, sizeof(destdir)) == NULL) {
            log(LOGERR, "getcwd: %m\n");
            err = 1;
            goto l_end;
        }
        destdirp = destdir;
    }
    
    if (!pkgset_fetch_pkgs(destdirp, pkgs))
        err++;
    
 l_end:
    if (pkgs)
        n_array_free(pkgs);

    shell_s.inst->fetchdir = NULL;
    return err == 0;
}


/*----------------------------------------------------------------------*/
/* status                                                               */
/*----------------------------------------------------------------------*/
static
error_t parse_desc_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_state *cmdst = state->input;
    
    switch (key) {
        case ARGP_KEY_ARG:
            n_array_push(cmdst->pkgnames, strdup(arg));

        case ARGP_KEY_END:
            //argp_usage (state);
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int cmd_desc(int argc, const char **argv, struct argp *argp)
{
    struct cmd_state cmdst = { 0, NULL};
    tn_array *shpkgs = NULL;
    int i, err = 0;

    shell_s.inst->flags = shell_s.inst_flags_orig;
    shell_s.inst->instflags = 0;
    
    cmdst.pkgnames = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    
    argp_parse(argp, argc, (char**)argv, argp_parse_flags, 0, &cmdst);
    resolve_packages(cmdst.pkgnames, shell_s.avpkgs, &shpkgs, 0);
    n_array_free(cmdst.pkgnames);
    
    if (shpkgs == NULL)
        return 0;

    if (n_array_size(shpkgs) == 0) {
        printf("desc: no package given\n");
        err++;
        goto l_end;
    }
    
    for (i=0; i<n_array_size(shpkgs); i++) {
        struct pkguinf *pkgu;
        struct shell_pkg *shpkg;

        shpkg = n_array_nth(shpkgs, i);
        if ((pkgu = pkg_info(shpkg->pkg))) {
            char timbuf[30];

            if (shpkg->pkg->btime) 
                strftime(timbuf, sizeof(timbuf), "%Y/%m/%d %H:%M",
                         localtime((time_t*)&shpkg->pkg->btime));
            else
                *timbuf = '\0';
            
            printf("\n"
                   "Name:\t\t%s\n", pkg_snprintf_s(shpkg->pkg));
            if (pkgu->summary) 
                printf("Summary:\t%s\n", pkgu->summary);

            if (pkgu->license) 
                printf("License:\t%s\n", pkgu->license);

            if (pkgu->url)
                printf("URL:\t\t%s\n", pkgu->url);

            if (*timbuf)
                printf("Built:\t\t%s\n", timbuf);

            if (shpkg->pkg->pkgdir && shpkg->pkg->pkgdir->path)
                printf("Path:\t\t%s\n", shpkg->pkg->pkgdir->path);
            
            if (pkgu->description) 
                printf("Description:\n%s\n", pkgu->description);
            
            pkguinf_free(pkgu);
        }
    }
    
 l_end:
    
    if (shpkgs != shell_s.avpkgs)
        n_array_free(shpkgs);
    return err == 0;
}


#if 0
static int cmd_fetch(char **argv)
{
    printf("stat");
    return (0);
}
#endif



static
int cmd_help(int argc, const char **argv, struct argp* argp)
{
    int i = 0;
    
    argc = argc;
    argv = argv;
    argp = argp;

    while (commands_tab[i].name) {
        struct command *cmd = &commands_tab[i++];
        char buf[256], *p;

        p = cmd->arg ? cmd->arg : "";
        if (cmd->argp_opts) {
            snprintf(buf, sizeof(buf), "[OPTION...] %s", cmd->arg);
            p = buf;
        }
        printf("%-12s %-28s %s\n", cmd->name, p, cmd->doc);
    }
    printf("\nType COMMAND -? for details\n");
    return 0;
}

static
int cmd_quit(int argc, const char **argv, struct argp* argp)
{
    argc = argc;
    argv = argv;
    argp = argp;
    shell_s.done = 1;
    return 1;
}

#if 0
static
int valid_argument(char *caller, char *arg)
{
    if (!arg || !*arg) {
	fprintf(stderr, "%s: an argument required.\n", caller);
	return (0);
    }
    
    return (1);
}
#endif

static 
void shell_end(int sig) 
{
    sig = sig;
    
    if (shell_s.histfile)
        write_history(shell_s.histfile);
    shell_s.done = 1;
}


void db_map_fn(unsigned int recno, void *header, void *shpkgs) 
{
    struct shell_pkg  *shpkg;
    struct pkg        *pkg;
    char              buf[1024];

    recno = recno;
    pkg = pkg_ldhdr(header, NULL, PKG_LDNEVR);
    
    pkg_snprintf(buf, sizeof(buf), pkg);
    shpkg = malloc(sizeof(*shpkg) + strlen(buf) + 1);
    memcpy(shpkg->nevr, buf, strlen(buf) + 1);
    shpkg->pkg = pkg;
    shpkg->flags = 0;
    shpkg->_ucnt = 0;
    shpkg->free_pkg = 1;
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
int cmd_reload(int argc, const char **argv, struct argp* argp)
{
    argc = argc;
    argv = argv;
    argp = argp; 
    load_installed_packages(&shell_s.instpkgs);
    return 1;
}



static void sig_winch(int signo)
{
    n_assert(signo == SIGWINCH);
    winch_reached = 1;
    signal(SIGWINCH, sig_winch);
}


static void get_term_width(void) 
{
    struct winsize ws;

    if (winch_reached) {
        if (ioctl(1, TIOCGWINSZ, &ws) == 0)
            term_width = ws.ws_col;
        else
            term_width = 80;

        winch_reached = 0;
    }
    
}

int shell_main(struct pkgset *ps, struct inst_s *inst, int skip_installed)
{
    char *line, *s, *histfile = NULL, *home;
    int n, i;


    argp_program_bug_address = NULL;
    
    if (!isatty(1)) {
        log(LOGERR, "not a tty\n");
        return 1;
    }

    winch_reached = 1;
    get_term_width();
    signal(SIGWINCH, sig_winch);
    
    
    if (shell_s.pkgset != NULL) {
        log(LOGERR, "shell_main: not reentrant func\n");
        return 0;
    }
    
    if (inst->rootdir == NULL)
        inst->rootdir = "/";
    
    shell_s.avpkgs = n_array_new(n_array_size(ps->pkgs), (tn_fn_free)shpkg_free,
                                 (tn_fn_cmp)shpkg_cmp);

    
    
    for (i=0; i<n_array_size(ps->pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->pkgs, i);
        struct shell_pkg *shpkg;
        char buf[1024];

        pkg_snprintf(buf, sizeof(buf), pkg);
        shpkg = malloc(sizeof(*shpkg) + strlen(buf) + 1);
        memcpy(shpkg->nevr, buf, strlen(buf) + 1);
        shpkg->pkg = pkg;
        shpkg->flags = 0;
        shpkg->_ucnt = 0;
        shpkg->free_pkg = 0;
        
        n_array_push(shell_s.avpkgs, shpkg);
    }
    n_array_ctl(shell_s.avpkgs, TN_ARRAY_AUTOSORTED);
    n_array_sort(shell_s.avpkgs);
    
    shell_s.pkgset = ps;
    shell_s.inst = inst;
    shell_s.inst_flags_orig = inst->flags;


    shell_s.instpkgs = NULL;
    if (skip_installed == 0) {
        shell_s.instpkgs = n_array_new(1024, (tn_fn_free)shpkg_free,
                                   (tn_fn_cmp)shpkg_cmp);
        load_installed_packages(&shell_s.instpkgs);
    }
    
    
    initialize_readline();
    switch_pkg_completion(COMPL_CTX_AV_PKGS);
    
    shell_s.histfile = NULL;
    if ((home = getenv("HOME"))) {
        histfile = alloca(strlen(home) + strlen("/.poldek_history") + 2);
        sprintf(histfile, "%s/.poldek_history", home);
        read_history(histfile);
        shell_s.histfile = histfile;
    }
    
    
    n = 0;
    shell_s.commands = n_array_new(16, NULL, (tn_fn_cmp)command_cmp);
    while (commands_tab[n].name)
        n_array_push(shell_s.commands, &commands_tab[n++]);
    n_array_sort(shell_s.commands);

    signal(SIGINT, shell_end);
    signal(SIGTERM, shell_end);
    signal(SIGQUIT, shell_end);

    
    shell_s.done = 0;
    while (shell_s.done == 0) {
	if ((line = readline("poldek> ")) == NULL)
            break;
        
	s = stripwhite(line);
	if (*s) {
            /*if (strncmp(s, "uninstall", sizeof("uninstall")) != 0)*/
            add_history(s);
	    execute_line(s);
	}
	free(line);
    }

    shell_end(0);
    shell_s.histfile = NULL;
    shell_s.pkgset = NULL;
    return 1;
}
