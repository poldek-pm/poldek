#include <stdio.h>
#include <stdlib.h>
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

static unsigned argp_parse_flags = ARGP_NO_EXIT;

static int cmd_ls();
static int cmd_stat();
static int cmd_help();
static int cmd_quit();
static int cmd_fetch();
static int cmd_install();
static
error_t parse_install_opt(int key, char *arg, struct argp_state *state);


static struct argp_option ls_options[] = {
{0,0,0,0, "ls [OPTION...] [PACKAGE...]", 1 },
{"long", 'l', 0, 0, "use a long listing format", 1},
};

#define OPT_INST_FETCH   100
#define OPT_INST_NODEPS  101
#define OPT_INST_FORCE   102

static struct argp_option install_options[] = {
{0,0,0,0, "install [OPTION...] PACKAGE...", 1 },

{"mercy",   'm', 0, 0, "Be tolerant for bugs which RPM tolerates", 1},

{"fetch", OPT_INST_FETCH, "DIR", 0,
 "Do not install, only fetch packages", 1},

{"nodeps", OPT_INST_NODEPS, 0, 0,
 "Install packages with broken dependencies", 1 },

{"force", OPT_INST_FORCE, 0, 0, "Be unconcerned", 70 },
{"test", 't', 0, 0, "Don't install, but tell if it would work or not", 1 },
{0,  'v', "v...", OPTION_ARG_OPTIONAL, "Be more (and more) verbose.", 1 },
{ 0, 0, 0, 0, 0, 0 },
};

struct command {
    int no;
    char *name;
    char *arg;
    struct argp_option *argp_opts;
    error_t (*parse_opt_fn)(int, char*, struct argp_state*);
    int (*fn)(const char **argv, struct command*);
    char *doc;
};

#define CMD_LS       1
#define CMD_INF0     2
#define CMD_INSTALL  3
#define CMD_STAT     4
#define CMD_QUIT     5
#define CMD_HELP     6

struct command commands_tab[] = {
{1, "ls", "[FILE...]", ls_options, NULL, cmd_ls, "list directory contents"},

{2, "info", "[FILE...]", NULL, NULL, cmd_ls, "display package(s) info"},
    
/*{"fetch", "[FILE...]", cmd_fetch, "fetch package(s)"},*/
    
{3,
     "install", "[FILE...]",
     install_options, parse_install_opt,  cmd_install,
     "install package(s)"
},
    
{4, "stat", "[FILE...]", NULL, NULL, cmd_stat, "display package(s) status"},
{5, "quit", NULL, NULL, NULL, cmd_quit, "quit poldek"},
{6, "help", "[COMMAND]", NULL, NULL, cmd_help, "display help"},
{0, NULL, NULL, NULL, NULL, NULL, NULL}
};

    

#define SHPKG_INSTALL (1 << 0)

struct shell_pkg {
    struct pkg *pkg;
    unsigned flags;
    char nevr[0];
};

struct shell_s {
    struct pkgset  *pkgset;
    struct inst_s  *inst;
    tn_array       *avpkgs;     /* array of shell_pkgs  */
    tn_array       *instpkgs;   /* array of shell_pkgs  */
    tn_array       *commands;
    char           *histfile;
    int            done;
};

static struct shell_s shell_s = {NULL, NULL, NULL, NULL, NULL, NULL, 0};


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
void shpkg_clean_flags(struct shell_pkg *shpkg) 
{
    shpkg->flags = 0;
}


static
int execute_line(char *line)
{
    struct command *command, tmpcmd;
    char *p;
    const char **args;
    int rc;
    
    
    p = line;
    while (*p && !isspace(*p))
	p++;
    
    if (*p)
        *p++ = '\0';

    tmpcmd.name = line;
    if ((command = n_array_bsearch(shell_s.commands, &tmpcmd)) == NULL) {
	fprintf(stderr, "%s: no such command.\n", line);
	return 0;
    }
    
    while (isspace(*p))
	p++;
    
    args = n_str_tokl(p, " \t");
    rc = command->fn(args, command);
    n_str_tokl_free(args);
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

	if (strncmp(name, text, len) == 0)
	    return strdup(name);
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
            i = n_array_bsearch_idx_ex(shell_s.avpkgs, text,
                                       (tn_fn_cmp)shpkg_ncmp_str);
    }

    if (i > -1 && i < n_array_size(shell_s.avpkgs)) {
        struct shell_pkg *shpkg = n_array_nth(shell_s.avpkgs, i++);
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
void resolve_packages(tn_array *pkgnames, tn_array **pkgsp)
{
    tn_array *pkgs = NULL;
    char *arg;
    int i, n = 0;

    *pkgsp = NULL;

    pkgs = n_array_new(16, NULL, (tn_fn_cmp)shpkg_cmp);

    for (i=0; i<n_array_size(pkgnames); i++) {
        int asterisk = 0, len;
        char *p, *name;

        name = n_array_nth(pkgnames, i);
        if ((p = strchr(name, '*')) && *(p+1) == '\0') {
            *p = '\0';
            n = n_array_bsearch_idx_ex(shell_s.avpkgs, name,
                                       (tn_fn_cmp)shpkg_ncmp_str);
            asterisk = 1;
            len = strlen(name);
                
        } else {
            n = n_array_bsearch_idx_ex(shell_s.avpkgs, name,
                                       (tn_fn_cmp)shpkg_cmp_str);
        }
            
        if (n == -1) {
            printf("%s: no such package\n", name);
            continue;
        }
            
        n_array_push(pkgs, n_array_nth(shell_s.avpkgs, n++));
        while (n < n_array_size(shell_s.avpkgs)) {
            struct shell_pkg *shpkg = n_array_nth(shell_s.avpkgs, n++);
                
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



static 
void prepare_argv(char **argv, tn_array **optsp, tn_array **pkgsp)
{
    tn_array *pkgs = NULL, *opts = NULL;
    char *arg;
    int i, n = 0;

    *optsp = NULL;
    *pkgsp = NULL;

    opts = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    pkgs = n_array_new(16, NULL, (tn_fn_cmp)shpkg_cmp);
    
    if (argv == NULL) 
        return;

    n = 0;
    while (argv[n]) {
        if (*argv[n] != '-')
            break;

        n_array_push(opts, argv[n++]);
    }

    *optsp = opts;
    if (argv[n] == NULL)
        return;
    
    while ((arg = argv[n++])) {
        int asterisk = 0, len;
        char *p;
        
        if ((p = strchr(arg, '*')) && *(p+1) == '\0') {
            *p = '\0';
            i = n_array_bsearch_idx_ex(shell_s.avpkgs, arg,
                                       (tn_fn_cmp)shpkg_ncmp_str);
            asterisk = 1;
            len = strlen(arg);
                
        } else {
            i = n_array_bsearch_idx_ex(shell_s.avpkgs, arg,
                                       (tn_fn_cmp)shpkg_cmp_str);
        }
            
        if (i == -1) {
            printf("%s: no such package\n", arg);
            continue;
        }
            
        n_array_push(pkgs, n_array_nth(shell_s.avpkgs, i++));
        while (i < n_array_size(shell_s.avpkgs)) {
            struct shell_pkg *shpkg = n_array_nth(shell_s.avpkgs, i++);
                
            if (asterisk) {
                if (strncmp(shpkg->nevr, arg, len) == 0)
                    n_array_push(pkgs, shpkg);
                    
            } else {
                if (strcmp(shpkg->nevr, arg) == 0)
                    n_array_push(pkgs, shpkg);
            }
        }
    }
    

    if (n_array_size(pkgs) == 0) {
        n_array_free(pkgs);
        n_array_free(opts);
        pkgs = NULL;
    } else {
        n_array_sort(pkgs);
        n_array_uniq(pkgs);
    }

    
    *pkgsp = pkgs;
}



#define LSFMT_BRIEF 0
#define LSFMT_LONG  1

static int cmd_ls(char **argv)
{
    tn_array *pkgs = NULL, *opts = NULL;
    int lsfmt = LSFMT_BRIEF;
    int i, size, err = 0;
    
    if (argv == NULL)
        return 0;

    prepare_argv(argv, &opts, &pkgs);

    if (pkgs == NULL) 
        return 0;

    if (n_array_size(pkgs) == 0) {
        n_array_free(pkgs);
        pkgs = shell_s.avpkgs;
    }

    lsfmt = LSFMT_BRIEF;
    
    for (i=0; i<n_array_size(opts); i++) {
        char *opt = n_array_nth(opts, i); 
        if (strcmp(opt, "-l") == 0) {
            lsfmt = LSFMT_LONG;
            printf("%-42s%-12s%16s\n", "package", "build date", "size (kB)");

        } else {
            printf("ls: %s: unknown option\n", opt);
            err++;
        }
    }

    if (err) 
        goto l_end;
    
    size = 0;
    for (i=0; i<n_array_size(pkgs); i++) {
        struct shell_pkg *shpkg = n_array_nth(pkgs, i);
        struct pkg *pkg = shpkg->pkg;

        switch (lsfmt) {
            case LSFMT_BRIEF:
                printf("%s\n", shpkg->nevr);
                break;
                
            case LSFMT_LONG: {
                char timbuf[30];
                if (pkg->btime) 
                    strftime(timbuf, sizeof(timbuf), "%Y/%m/%d %H:%M",
                             localtime((time_t*)&pkg->btime));
                else
                    *timbuf = '\0';
                printf("%-42s%12s%8d\n", shpkg->nevr,
                       timbuf, pkg->size/1024);
                size += pkg->size/1024;
            }
            break;
            
            default:
                n_assert(0);
        }
    }

    if (lsfmt != LSFMT_BRIEF && n_array_size(pkgs)) {
        char *unit;
        int val;
        
        if (size > 1000) {
            unit = "MB";
            val = size/1000;
        } else {
            unit = "kB";
            val = size;
        }
        
        printf("%d package%s, %d %s\n", n_array_size(pkgs),
               n_array_size(pkgs) > 1 ? "s":"", val, unit);
    }

 l_end:
    if (opts)
        n_array_free(opts);
    
    if (pkgs != shell_s.avpkgs)
        n_array_free(pkgs);
    
    return err == 0;
}

/* Parse a single option. */
static
error_t parse_install_opt(int key, char *arg, struct argp_state *state)
{
    tn_array *pkgnames = state->input;
    
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

        case ARGP_KEY_ARG:
            n_array_push(pkgnames, strdup(arg));

        case ARGP_KEY_END:
            //argp_usage (state);
            break;


            
        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static
void parse_options(int argc, char **argv, struct command *cmd) 
{
    struct argp argp = { cmd->argp_opts, cmd->parse_opt_fn, 
                         NULL, cmd->doc, 0, 0, 0};
    verbose = 1;
    
    argp_parse(&argp, argc, argv, 0, 0, 0);
}



static int cmd_install(char **argv, struct command *cmd)
{
    tn_array *shpkgs = NULL, *pkgnames;
    int i, n, err = 0, argc = 0;
    char **args;
    
    
    struct argp argp = {
        cmd->argp_opts, cmd->parse_opt_fn, NULL, cmd->doc, 0, 0, 0
     };

    
    shell_s.inst->instflags = 0;
    shell_s.inst->instflags |= PKGINST_UPGRADE;
    pkgnames = n_array_new(16, free, (tn_fn_cmp)strcmp);

    argc = 0;
    while (argv[argc] != NULL)
        argc++;
    argc++;
    
    args = malloc(argc * sizeof(*args));
    for (i=1; i<argc; i++) {
        args[i] = strdup(argv[i-1]);
    }
    args[0] = "install";
    

    argp_parse(&argp, argc, args, argp_parse_flags, 0, pkgnames);
    resolve_packages(pkgnames, &shpkgs);
    n_array_free(pkgnames);
    
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
        printf("%s\n", shpkg->nevr);
    }

    install_pkgs(shell_s.pkgset, shell_s.inst);
    
 l_end:

    if (shpkgs != shell_s.avpkgs)
        n_array_free(shpkgs);
    n_array_map(shell_s.avpkgs, (tn_fn_map1)shpkg_clean_flags);
    return err == 0;
}



static int cmd_fetch(char **argv)
{
    printf("stat");
    return (0);
}




static
int cmd_stat(char **argv)
{
    printf("stat");
    return (0);
}

static
int cmd_help(char **argv)
{
    int i;
    
    argv = argv;

    for (i=0; i<n_array_size(shell_s.commands); i++) {
        char buf[60];
        struct command *cmd = n_array_nth(shell_s.commands, i);

        snprintf(buf, sizeof(buf), "%s %s", cmd->name,
                 cmd->arg ? cmd->arg : "");
        printf("%-30s %s\n", buf, cmd->doc);
    }
    return 0;
}

static
int cmd_quit(char *arg)
{
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
    if (shell_s.histfile) {
        printf("write history %s\n", shell_s.histfile);
        write_history(shell_s.histfile);
    }
}



int shell_main(struct pkgset *ps, struct inst_s *inst)
{
    char *line, *s, *histfile = NULL, *home;
    int n, i;

    
    if (shell_s.pkgset != NULL) {
        log(LOGERR, "shell_main: not reentrant func\n");
        return 0;
    }
    
    rpm_initlib(inst->rpmacros);
    if (inst->rootdir == NULL)
        inst->rootdir = "/";
    
    shell_s.avpkgs = n_array_new(n_array_size(ps->pkgs), free,
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
        
        n_array_push(shell_s.avpkgs, shpkg);
    }

    shell_s.pkgset = ps;
    shell_s.inst = inst;
    
    
    initialize_readline();
    shell_s.histfile = NULL;
    if ((home = getenv("HOME"))) {
        histfile = alloca(strlen(home) + strlen("/.poldek_history") + 2);
        sprintf(histfile, "%s/.poldek_history", home);
        printf("reading history file %s\n", histfile);
        read_history(histfile);
    }
    
    
    n = 0;
    shell_s.commands = n_array_new(16, NULL, (tn_fn_cmp)command_cmp);
    while(commands_tab[n].name)
        n_array_push(shell_s.commands, &commands_tab[n++]);
    n_array_sort(shell_s.commands);

    signal(SIGINT, shell_end);

    
    shell_s.done = 0;
    while (shell_s.done == 0) {
	if ((line = readline("poldek> ")) == NULL)
            break;
        
	s = stripwhite(line);
	if (*s) {
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

