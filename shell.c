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
#include <argp.h>

#include <readline/readline.h>
#include <readline/history.h>
#include <trurl/trurl.h>

#include "pkg.h"
#include "pkgset-def.h"
#include "pkgset.h"
#include "pkgdb.h"
#include "install.h"
#include "misc.h"
#include "rpm.h"
#include "log.h"


extern int shell_uninstall_pkgs(tn_array *pkgnevrs, struct inst_s *inst);

static unsigned argp_parse_flags = ARGP_NO_EXIT;


static int cmd_help(int argc, const char **argv, struct argp*);
static int cmd_quit(int argc, const char **argv, struct argp*);


/* ls */
static int cmd_ls(int argc, const char **argv, struct argp*);
static error_t parse_ls_opt(int key, const char *arg, struct argp_state *state);


#define OPT_LS_LONG          (1 << 0) /* cmd_state->flags */
#define OPT_LS_UPGRADEABLE   (1 << 1) /* cmd_state->flags */
#define OPT_LS_INSTALLED     (1 << 3) /* cmd_state->flags */

static struct argp_option options_ls[] = {
 { "long", 'l', 0, 0, "use a long listing format", 1},
 { "upgradeable", 'u', 0, 0, "show upgradeable packages only", 1},
 { "installed", 'I', 0, 0, "list installed packages", 1},
};


/* stat */
static int cmd_stat(int argc, const char **argv, struct argp*);
static error_t parse_stat_opt(int key, char *arg, struct argp_state *state);

#define OPT_STAT_ALL   (1 << 10) /* cmd_state->flags */
static struct argp_option options_stat[] = {
{0,0,0,0, "stat [OPTION...] [PACKAGE...]", 1 },
{"all", 'a', 0, 0, "print all packages", 1},
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
#define CMD_STAT       5
#define CMD_RELOAD     6

#define CMD_QUIT       20
#define CMD_HELP       21

struct command commands_tab[] = {
{ CMD_LS, "ls", "[OPTION...] [PACKAGE...]", options_ls, parse_ls_opt, cmd_ls,
  "List packages"},

/*{ CMD_INF0, "info", "[PACKAGE...]", NULL, NULL, cmd_ls, "display package(s) info"},*/
    
/*{"fetch", "[FILE...]", cmd_fetch, "fetch package(s)"},*/

/*{ CMD_STAT, "stat", "[OPTION...] [PACKAGE...]", options_stat, parse_stat_opt, cmd_stat,
  "Display given packages status"},*/

{ CMD_INSTALL, "install", "[OPTION...] PACKAGE...",
      options_install, parse_install_opt, cmd_install,
      "Install given packages." },
    
{ CMD_UNINSTALL, "uninstall", "[OPTION...] PACKAGE...", options_uninstall,
      parse_uninstall_opt, cmd_uninstall,
      "Uninstall given packages" },

{ CMD_RELOAD, "reload", NULL, NULL, NULL, cmd_reload,
      "Reload installed package list (if you modify rpm database externally)"},
    
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
int shpkg_cmp_str(struct shell_pkg *pkg, const char *name) 
{
    return strcmp(pkg->nevr, name);
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
    int rc;

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
                             NULL, cmd->doc, 0, 0, 0};
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
            compl_shpkgs = shell_s.instpkgs;
            break;
            
        default:
            n_assert(0);
    }
}

static
char *command_generator(char *text, int state)
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
            if (strcmp(name, "uninstall") == 0) {
                //printf("switch to uninstall\n");
                switch_pkg_completion(COMPL_CTX_INST_PKGS);
            } else
                switch_pkg_completion(COMPL_CTX_AV_PKGS);
            
	    return strdup(name);
        }
    }

    /* If no names matched, then return NULL. */
    return NULL;
}

static
char *pkgname_generator(char *text, int state)
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


static
char **poldek_completion(char *text, int start, int end)
{
    char **matches;
    
    end = end;
    matches = NULL;
    
    if (start == 0)
	matches = completion_matches(text, command_generator);
    else
        matches = completion_matches(text, pkgname_generator);
    
    return matches;
}


static
void initialize_readline(void)
{
    rl_readline_name = "poldek";
    rl_attempted_completion_function = (CPPFunction *) poldek_completion;
    rl_completion_entry_function = (Function *) pkgname_generator;
}


static 
void resolve_packages(tn_array *pkgnames, tn_array *avshpkgs, tn_array **pkgsp)
{
    tn_array *pkgs = NULL;
    int i, n = 0;

    *pkgsp = NULL;

    pkgs = n_array_new(16, NULL, (tn_fn_cmp)shpkg_cmp);

    for (i=0; i<n_array_size(pkgnames); i++) {
        int asterisk = 0, len;
        char *p, *name;

        name = n_array_nth(pkgnames, i);
        
        if (*name == '*' && *(name + 1) == '\0') {
            n_array_free(pkgs);
            *pkgsp = avshpkgs;
            return;
        }
        
            
        if ((p = strchr(name, '*')) && *(p+1) == '\0') {
            *p = '\0';
            n = n_array_bsearch_idx_ex(avshpkgs, name,
                                       (tn_fn_cmp)shpkg_ncmp_str);
            asterisk = 1;
            len = strlen(name);
                
        } else {
            n = n_array_bsearch_idx_ex(avshpkgs, name,
                                       (tn_fn_cmp)shpkg_cmp_str);
        }
            
        if (n == -1) {
            printf("%s: no such package\n", name);
            continue;
        }
        
        n_array_push(pkgs, n_array_nth(avshpkgs, n++));
        while (n < n_array_size(avshpkgs)) {
            struct shell_pkg *shpkg = n_array_nth(avshpkgs, n++);
                
            if (asterisk) {
                if (strncmp(shpkg->nevr, name, len) == 0)
                    n_array_push(pkgs, shpkg);
                    
            } else if (strcmp(shpkg->nevr, name) == 0)
                n_array_push(pkgs, shpkg);
        }
    }
    

    if (n_array_size(pkgs) == 0) {
        n_array_free(pkgs);
        pkgs = NULL;
    } else {
        n_array_sort(pkgs);
        n_array_uniq(pkgs);
    }
    
    *pkgsp = pkgs;
}


static int find_pkg(struct shell_pkg *lshpkg, tn_array *shpkgs, int *cmprc, 
                    char *evr, size_t size) 
{
    struct shell_pkg *shpkg;
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
    
    *cmprc = pkg_cmp_evr(lshpkg->pkg, shpkg->pkg);
    snprintf(evr, size, "%s-%s", shpkg->pkg->ver, shpkg->pkg->rel);
    
    return finded;
}


/*----------------------------------------------------------------------*/
/* ls                                                                   */
/*----------------------------------------------------------------------*/
static
error_t parse_ls_opt(int key, const char *arg, struct argp_state *state)
{
    struct cmd_state *cmdst = state->input;
    
/*    if (arg)
      chkarg(key, arg);*/
    
    switch (key) {
        case 'l':
            cmdst->flags |= OPT_LS_LONG;
            break;

        case 'u':
            cmdst->flags |= OPT_LS_UPGRADEABLE;
            break;

        case 'I':
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
    struct cmd_state cmdst = { 0, NULL};
    tn_array *shpkgs = NULL, *av_shpkgs;
    int i, size, err = 0, npkgs = 0;
    char hdr[128];
    
    if (argv == NULL)
        return 0;

    cmdst.pkgnames = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    argp_parse(argp, argc, (char**)argv, argp_parse_flags, 0, (void*)&cmdst);

    if (cmdst.flags & OPT_LS_INSTALLED)
        av_shpkgs = shell_s.instpkgs;
    else
        av_shpkgs = shell_s.avpkgs;

    if (n_array_size(cmdst.pkgnames)) 
        resolve_packages(cmdst.pkgnames, av_shpkgs, &shpkgs);
    else
        shpkgs = av_shpkgs;
        
    n_array_free(cmdst.pkgnames);

    if (shpkgs == NULL || n_array_size(shpkgs) == 0) {
        n_array_free(shpkgs);
        shpkgs = av_shpkgs;
    }

    if (cmdst.flags & OPT_LS_LONG) {
        if (cmdst.flags & OPT_LS_UPGRADEABLE) {
            if (cmdst.flags & OPT_LS_INSTALLED) 
                snprintf(hdr, sizeof(hdr), "%-42s%-12s%-12s%16s\n", "installed",
                         "available", "build date", "size (kB)");
            else
                snprintf(hdr, sizeof(hdr), "%-42s%-12s%-12s%16s\n", "available",
                         "installed", "build date", "size (kB)");
        } else
            snprintf(hdr, sizeof(hdr), "%-42s%-12s%16s\n", "package",
                     "build date", "size (kB)");
    }
    
    
    size = 0;
    for (i=0; i<n_array_size(shpkgs); i++) {
        struct shell_pkg *shpkg = n_array_nth(shpkgs, i);
        struct pkg *pkg = shpkg->pkg;
        char evr[128]; 
        int cmprc = 0;
        
        if (cmdst.flags & OPT_LS_UPGRADEABLE) {
            int finded;

            if (cmdst.flags & OPT_LS_INSTALLED) {
                finded = find_pkg(shpkg, shell_s.avpkgs, &cmprc,
                                  evr, sizeof(evr));
                
            } else {
                finded = find_pkg(shpkg, shell_s.instpkgs, &cmprc,
                                  evr, sizeof(evr));
                cmprc = -cmprc;
            }
            
            if (!finded || cmprc >= 0)
                continue;
        }
        
        if (npkgs == 0)
            printf("%s", hdr);
        
        if (cmdst.flags & OPT_LS_LONG) {
            char timbuf[30];
            if (pkg->btime) 
                strftime(timbuf, sizeof(timbuf), "%Y/%m/%d %H:%M",
                         localtime((time_t*)&pkg->btime));
            else
                *timbuf = '\0';
            if (cmdst.flags & OPT_LS_UPGRADEABLE) 
                printf("%-42s%-12s%12s%8d\n", shpkg->nevr, evr, timbuf,
                       pkg->size/1024);
            else
                printf("%-42s%12s%8d\n", shpkg->nevr, timbuf,
                       pkg->size/1024);
            
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

        case OPT_INST_FETCH:
            shell_s.inst->flags |= INSTS_JUSTFETCH;
            shell_s.inst->fetchdir = trimslash(arg);
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
    shell_s.pkgset->flags |= PSMODE_UPGRADE;
    
    cmdst.pkgnames = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    
    argp_parse(argp, argc, (char**)argv, argp_parse_flags, 0, &cmdst);

    resolve_packages(cmdst.pkgnames, shell_s.avpkgs, &shpkgs);
    n_array_free(cmdst.pkgnames);
    
    if (shpkgs == NULL)
        return 0;

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

    if (install_pkgs(shell_s.pkgset, shell_s.inst)) {
        for (i=0; i<n_array_size(shpkgs); i++)
            n_array_push(shell_s.instpkgs, shpkg_link(n_array_nth(shpkgs, i)));
        n_array_sort(shell_s.instpkgs);
    } 
    
 l_end:

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

    shell_s.inst->flags = shell_s.inst_flags_orig;
    shell_s.inst->instflags = 0;
    cmdst.pkgnames = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    argp_parse(argp, argc, (char**)argv, argp_parse_flags, 0, &cmdst);
    resolve_packages(cmdst.pkgnames, shell_s.instpkgs, &shpkgs);
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
/* status                                                               */
/*----------------------------------------------------------------------*/
static
error_t parse_stat_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_state *cmdst = state->input;
    
    switch (key) {
        case ARGP_KEY_ARG:
            n_array_push(cmdst->pkgnames, strdup(arg));

        case 'a':
            

        case ARGP_KEY_END:
            //argp_usage (state);
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int cmd_stat(int argc, const char **argv, struct argp *argp)
{
    struct cmd_state cmdst = { 0, NULL};
    tn_array *shpkgs = NULL;
    int i, j, err = 0;

    shell_s.inst->flags = shell_s.inst_flags_orig;
    shell_s.inst->instflags = 0;
    
    cmdst.pkgnames = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    
    argp_parse(argp, argc, (char**)argv, argp_parse_flags, 0, &cmdst);
    resolve_packages(cmdst.pkgnames, shell_s.avpkgs, &shpkgs);
    n_array_free(cmdst.pkgnames);
    
    if (shpkgs == NULL)
        return 0;

    if (n_array_size(shpkgs) == 0) {
        printf("stat: specify what packages you want to install\n");
        err++;
        goto l_end;
    }
    
    if (err) 
        goto l_end;

    pkgset_unmark(shell_s.pkgset, PS_MARK_UNMARK_ALL);

    shell_s.inst->db = pkgdb_open(shell_s.inst->rootdir, NULL, O_RDONLY);
    if (shell_s.inst->db == NULL) {
        log(LOGERR, "could not open database\n");
        goto l_end;
    }
    
    for (i=0; i<n_array_size(shpkgs); i++) {
        struct shell_pkg *shpkg = n_array_nth(shpkgs, i);
        tn_array *dbpkgs;
        
        dbpkgs = rpm_get_packages(shell_s.inst->db->dbh, shpkg->pkg, PKG_LDNEVR);
        if (dbpkgs == NULL) {
            if (cmdst.flags & OPT_STAT_ALL) 
                msg(0, "%-32s not installed\n", pkg_snprintf_s(shpkg->pkg));
            continue;
        }
        
        msg(0, "%-32s", pkg_snprintf_s(shpkg->pkg));    
        for (j=0; j<n_array_size(dbpkgs); j++) {
            struct dbpkg *dbpkg = n_array_nth(dbpkgs, j);
            int cmprc, c;
            char *p = ", ", *msg;
            
            cmprc = pkg_cmp_evr(dbpkg->pkg, shpkg->pkg);
            
            if (cmprc == 0)
                msg = "up to date";
            else if (cmprc < 0)
                msg = "upgradable";
            else
                msg = "up to date;";
            

            if (j == n_array_size(dbpkgs) - 1)
                p = "";
            
            msg(0, "_%s %s", msg, pkg_snprintf_s(dbpkg->pkg));
        }
        msg(0, "_\n");
        if (dbpkgs)
            n_array_free(dbpkgs);
    }
    
 l_end:

    if (shell_s.inst->db) {
        pkgdb_free(shell_s.inst->db);
        shell_s.inst->db = NULL;
    }
    
    if (shpkgs != shell_s.avpkgs)
        n_array_free(shpkgs);
    n_array_map(shell_s.avpkgs, (tn_fn_map1)shpkg_clean_flags);
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
        printf("%-12s %-28s %s\n", cmd->name, cmd->arg ? cmd->arg : "", cmd->doc);
    }
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
    msg(0, "_done. %d packages loaded\n", n_array_size(shpkgs));
    
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


int shell_main(struct pkgset *ps, struct inst_s *inst)
{
    char *line, *s, *histfile = NULL, *home;
    int n, i;

    
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

    shell_s.pkgset = ps;
    shell_s.inst = inst;
    shell_s.inst_flags_orig = inst->flags;
    
    shell_s.instpkgs = n_array_new(1024, (tn_fn_free)shpkg_free,
                                   (tn_fn_cmp)shpkg_cmp);
    load_installed_packages(&shell_s.instpkgs);
    
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

