/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <argp.h>
#include <sys/types.h>
#include <sys/stat.h> 
#include <unistd.h>
#include <time.h>

#include <trurl/narray.h>
#include <trurl/nassert.h>
#include <vfile/vfile.h>

#include "i18n.h"
#include "i18n.h"
#include "log.h"
#include "pkgset.h"
#include "usrset.h"
#include "misc.h"
#include "rpm.h"
#include "install.h"
#include "conf.h"
#include "split.h"
#include "term.h"

#ifndef VERSION
# error "undefined VERSION"
#endif

#ifdef ENABLE_INTERACTIVE_MODE
extern
int shell_main(struct pkgset *ps, struct inst_s *inst, int skip_installed);
extern
int shell_exec(struct pkgset *ps, struct inst_s *inst, int skip_installed,
               const char *cmd);
#endif

static const char program_bug_address[] = "<mis@pld.org.pl>";

static const char *argp_program_version = PACKAGE " " VERSION " (" VERSION_STATUS ")";
const char *argp_program_bug_address = program_bug_address;

/* Program documentation. */
char poldek_banner[] = PACKAGE " " VERSION " (" VERSION_STATUS ")\n"
"Copyright (C) 2000-2002 - <mis@pld.org.pl>\n"
"This program may be freely redistributed under the terms of the GNU GPL v2\n";
/* A description of the arguments we accept. */
static char args_doc[] = N_("[PACKAGE...]");

#define MODE_NULL         0
#define MODE_VERIFY       1
#define MODE_MKIDX        2
#define MODE_INSTALLDIST  3
#define MODE_INSTALL      4
#define MODE_UPGRADEDIST  5
#define MODE_UPGRADE      6
#define MODE_SPLIT        7

#ifdef ENABLE_INTERACTIVE_MODE
# define MODE_SHELL       20
#endif

#define MODE_MNR_UPDATEIDX  (1 << 0)

#define INDEXTYPE_TXT     1
#define INDEXTYPE_TXTZ    2

struct split_conf {
    int   size;
    int   first_free_space;
    char  *conf;
    char  *prefix;
};

struct args {
    int       mjrmode;
    unsigned  mnrmode;

    int       update_op;
    
    char      *curr_src_path;
    int       curr_src_ldmethod;
    tn_array  *sources;
    
    int       idx_type;
    char      *idx_path;

    int       has_pkgdef;
    tn_array  *pkgdef_files;    /* foo.rpm      */
    tn_array  *pkgdef_defs;     /* -n "foo 1.2" or "foo" or "foo*" */
    tn_array  *pkgdef_sets;     /* -p ftp://ftp.zenek.net/PLD/tiny */
    
    unsigned   psflags;
    struct inst_s inst;
    
    struct usrpkgset  *ups;
    
    char        *conf_path;
    int         noconf;
    char        *log_path;
    
    unsigned    pkgdir_creat_flags; 
    
    int         shell_skip_installed;
    char        *shcmd;

    struct      split_conf split_conf;
} args;

tn_hash *htcnf = NULL;          /* config file values */

#define OPT_VERIFY_MERCY      'm'
#define OPT_VERIFY_DEPS       'V'
#define OPT_VERIFY_CNFLS      902
#define OPT_VERIFY_FILECNFLS  903
#define OPT_VERIFY_ALL        904

#define OPT_MKIDX        1001
#define OPT_MKIDXZ       1002
#define OPT_NODESC	 1004 /* don't put descriptions in package index */

#define OPT_SOURCETXT   1015
#define OPT_SOURCEDIR   1016
#define OPT_SOURCEHDR   1017
#define OPT_PKGPREFIX   1018
#define OPT_UPDATEIDX   1019
#define OPT_UPDATEIDX_WHOLE   1020
#define OPT_SOURCECACHE 1021

#ifdef ENABLE_INTERACTIVE_MODE
# define OPT_SHELLMODE             1031
# define OPT_SHELL_SKIPINSTALLED   'f'
# define OPT_SHELL_CMD             1032
#endif

#define OPT_INST_INSTDIST         1041
#define OPT_INST_UPGRDIST         1042
#define OPT_INST_NODEPS           1043
#define OPT_INST_FORCE            1044
#define OPT_INST_JUSTDB           1045
#define OPT_INST_TEST             1046
#define OPT_INST_MKDBDIR          1047
#define OPT_INST_RPMDEF           1049
#define OPT_INST_FETCH            1050
#define OPT_INST_MKSCRIPT         1051
#define OPT_INST_POLDEK_MKSCRIPT  1052
#define OPT_INST_NOFOLLOW         'N'
#define OPT_INST_FRESHEN          'F'
#define OPT_INST_HOLD             1053
#define OPT_INST_NOHOLD           1054
#define OPT_INST_GREEDY           1055

#define OPT_SPLITSIZE             1100
#define OPT_SPLITCONF             1101
#define OPT_SPLITOUTPATH          1102


#define OPT_CONF                  'c'
#define OPT_NOCONF                2002 

/* The options we understand. */
static struct argp_option options[] = {

{0,0,0,0, N_("Source options:"), 1 },    
{"source", 's', "SOURCE", 0, N_("Get packages info from SOURCE"), 1 },
    
{"sidx", OPT_SOURCETXT, "FILE", 0,
 N_("Get packages info from package index file FILE"), 1 },

{"sdir", OPT_SOURCEDIR, "DIR", 0,
 N_("Get packages info from directory DIR"), 1 },

{"prefix", 'P', "PREFIX", 0,
 N_("Get packages from PREFIX instead of SOURCE"), 1 },

{"update", OPT_UPDATEIDX, 0, 0, 
 N_("Update sources (for remote indexes) and verify its integrity"), 1 },

{"up", OPT_UPDATEIDX, 0, OPTION_ALIAS, 0, 1 }, 

{"update-whole", OPT_UPDATEIDX_WHOLE, 0, 0, 
 N_("Update whole sources (for remote indexes) and verify its checksum"), 1 },

{"upa", OPT_UPDATEIDX_WHOLE, 0, OPTION_ALIAS, 0, 1}, 

{"cachedir", OPT_SOURCECACHE, "DIR", 0, 
 N_("Store downloaded files under DIR"), 1 },
  
{0,0,0,0, N_("Verify options:"), 50 },        
{"verify",  OPT_VERIFY_DEPS, 0, 0, N_("Verify package dependencies"), 50 },
{"verify-conflicts",  OPT_VERIFY_CNFLS, 0, 0, N_("Verify package conflicts"), 50 },
{"verify-fileconflicts",  OPT_VERIFY_FILECNFLS, 0, 0,
     N_("Verify package file conflicts"), 50 },
{"verify-all",  OPT_VERIFY_ALL, 0, 0,
     N_("Verify dependencies, conflicts and file conflicts"), 50 },
{"mercy",   OPT_VERIFY_MERCY, 0, 0,
     N_("Be tolerant for bugs which RPM tolerates"), 50 },


{0,0,0,0, "Indexes creation:", 60},
{"mkidx", OPT_MKIDX, "FILE", OPTION_ARG_OPTIONAL,
 N_("Create package index, SOURCE/packages.dir by default"), 60},

{"mkidxz", OPT_MKIDXZ, "FILE", OPTION_ARG_OPTIONAL,
 N_("Like above, but gzipped file is created"), 60},

{"nodesc", OPT_NODESC, 0, 0,
 N_("Don't put packages user-level information (like Summary or Description)"
     " in created index."), 60 },
    

{0,0,0,0, N_("Installation:"), 70},
{"pkgset", 'p',  "FILE", 0, N_("Take package set definition from FILE"), 70 },
{"pkgnevr",'n',  "\"NAME [[E:][V[-R]]]\"", 0,
     N_("Specifies package by NAME and EVR"), 70 },

{"install-dist", OPT_INST_INSTDIST, "DIR", 0,
     N_("Install package set under DIR as root directory"), 70 },

{"upgrade-dist", OPT_INST_UPGRDIST, "DIR", OPTION_ARG_OPTIONAL,
     N_("Upgrade all packages needs upgrade"), 70 },

{"install", 'i', 0, 0, N_("Install given packages"), 70 },    
{"upgrade", 'U', 0, 0, N_("Upgrade given packages"), 70 },
{"root", 'r', "DIR", 0, N_("Set top directory to DIR"), 70 },
{"hold", OPT_INST_HOLD, "PACKAGE[,PACKAGE]...", 0,
N_("Prevent packages listed from being upgraded if they are already installed."), 70 },

{"nohold", OPT_INST_NOHOLD, 0, 0,
N_("Don't take held packages from $HOME/.poldek_hold."), 70 },

{"greedy", OPT_INST_GREEDY, 0, 0,
 N_("Automatically upgrade packages which dependencies are broken "
    "by unistalled ones"), 70 }, 
    
{"dump", OPT_INST_MKSCRIPT, "FILE", OPTION_ARG_OPTIONAL,
     N_("Just dump install marked package filenames to FILE (default stdout)"), 70 },

{"dumpn", OPT_INST_POLDEK_MKSCRIPT, "FILE", OPTION_ARG_OPTIONAL,
     N_("Just dump install marked package names to FILE (default stdout)"), 70 },

{"fresh", OPT_INST_FRESHEN, 0, 0, 
     N_("Upgrade packages, but only if an earlier version currently exists"), 70 },

{"nofollow", OPT_INST_NOFOLLOW, 0, 0, 
     N_("Don't automatically install packages required by installed ones"), 70 },    
    
{"fetch", OPT_INST_FETCH, "DIR", 0,
     N_("Do not install, only fetch packages"), 70}, 
    
{"nodeps", OPT_INST_NODEPS, 0, 0,
     N_("Install packages with broken dependencies"), 70 },
    
{"force", OPT_INST_FORCE, 0, 0,
     N_("Be unconcerned"), 70 },
    
{"justdb", OPT_INST_JUSTDB, 0, 0,
     N_("Modify only the database"), 70 },
    
{"rpmdef", OPT_INST_RPMDEF, "RPMMACRO", 0,
     N_("Set up rpm macro (only simple definitions)"), 70 },
    
{"test", 't', 0, 0,
 N_("Don't install, but tell if it would work or not"), 70 },
    
{"mkdir", OPT_INST_MKDBDIR, 0, 0, 
     N_("make %{_dbpath} if not exists"), 70 },

#ifdef ENABLE_INTERACTIVE_MODE
{0,0,0,0, N_("Interactive mode:"), 80},
{"shell", OPT_SHELLMODE, 0, 0, N_("Run in interactive mode"), 80 },
{"fast", OPT_SHELL_SKIPINSTALLED, 0, 0,
     N_("Don't load installed packages at startup"), 80 },
{"shcmd", OPT_SHELL_CMD, "COMMAND", 0,
     N_("Run poldek shell COMMAND and exit"), 80 },
#endif

{0,0,0,0, N_("Splitting:"), 90},
{"split", OPT_SPLITSIZE, "SIZE[:FIRST_FREE_SPACE]", 0,
     N_("Split packages to SIZE MB size chunks, the first chunk will "
           "be FIRST_FREE_SPACE MB smaller"), 90 },
    
{"split-conf", OPT_SPLITCONF, "FILE", 0,
     N_("Take package priorities from FILE"), 90 },
    
{"priconf", OPT_SPLITCONF, "FILE", 0,
     N_("Take package priorities from FILE"), 70 },
    
{"split-out", OPT_SPLITOUTPATH, "PREFIX", 0,
     N_("Write chunks to PREFIX.XX, default PREFIX is packages.chunk"), 90 },    

{0,0,0,0, N_("Other:"), 500},    
{"conf", OPT_CONF, "FILE", 0, N_("Read configuration from FILE"), 500 }, 
{"noconf", OPT_NOCONF, 0, 0, N_("Do not read configuration"), 500 }, 


{"log", 'l', "FILE", OPTION_ARG_OPTIONAL, N_("Log program messages"
" to FILE ($TMPDIR/poldek.log if not given)"), 500 },    
{0,  'v', 0, 0,
     N_("Be verbose."), 500 },
{0,  'q', 0, 0,
     N_("Do not produce any output."), 500 },
{ 0, 0, 0, 0, 0, 0 },
};


void check_mjrmode(struct args *argsp) 
{
    if (argsp->mjrmode) {
        logn(LOGERR, _("only one major mode may be specified "
        "(available modes are: mkidx, update, verify*, install*, "
                       "upgrade*, split and shell)"));
        exit(EXIT_FAILURE);
    }
}

/* buggy glibc argp... */
static inline void chkarg(int key, char *arg) 
{
    if (*arg == '-') {
        int n = 0;
        while (options[n].doc) {
            if (key == options[n].key) {
                char skey[2] = { key, '\0' };
                logn(LOGERR, _("option requires an argument -- %s"),
                     isascii(key) ? skey : options[n].name);
                exit(EXIT_FAILURE);
            }
            n++;
        }
        exit(EXIT_FAILURE);
    }
}

/* Parse a single option. */
static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct args *argsp = state->input;
    struct source *src = NULL;
    int ldmethod = PKGSET_LD_NIL;

    if (arg)
        chkarg(key, arg);
    
    switch (key) {
        case 'l':
            if (arg) {
                argsp->log_path = arg;
                
            } else {
                char path[PATH_MAX];
                snprintf(path, sizeof(path), "%s/poldek.log", tmpdir());
                argsp->log_path = strdup(path);
            }
            break;
            
        case 'q':
            verbose = -1;
            break;
            
        case 'v': 
            verbose++;
            break;

        case 'n': 
            n_array_push(argsp->pkgdef_defs, arg);
            break;

        case 'p':
            n_array_push(argsp->pkgdef_sets, arg);
            break;
            
        case OPT_SOURCETXT:     /* no break */
            ldmethod = PKGSET_LD_IDX;
            
        case OPT_SOURCEDIR:     /* no break */
            if (ldmethod == PKGSET_LD_NIL)
                ldmethod = PKGSET_LD_DIR;
            
            
        case 's':
            if (argsp->curr_src_path) { /* no prefix for curr_src_path */
                src = source_new(argsp->curr_src_path, NULL);
                src->ldmethod = argsp->curr_src_ldmethod;
                n_array_push(argsp->sources, src);
            }
            
            argsp->curr_src_path = arg;
            argsp->curr_src_ldmethod = ldmethod;
            break;

        case 'P':
            if (argsp->curr_src_path == NULL) {
                logn(LOGERR, _("prefix should be preceded by source one"));
                exit(EXIT_FAILURE);
            }
            
            if (argsp->curr_src_ldmethod == PKGSET_LD_DIR) {
                logn(LOGERR, _("prefix for directory source makes no sense"));
                exit(EXIT_FAILURE);
            }
            
            src = source_new(argsp->curr_src_path, trimslash(arg));
            src->ldmethod = argsp->curr_src_ldmethod;
            n_array_push(argsp->sources, src);
            argsp->curr_src_path = NULL;
            argsp->curr_src_ldmethod = PKGSET_LD_NIL;
            break;

            
        case OPT_UPDATEIDX:
            argsp->mnrmode |= MODE_MNR_UPDATEIDX;
            break;

        case OPT_UPDATEIDX_WHOLE:
            argsp->mnrmode |= MODE_MNR_UPDATEIDX;
            argsp->update_op = OPT_UPDATEIDX_WHOLE;
            break;

        case OPT_SOURCECACHE:
            argsp->inst.cachedir = trimslash(arg);
            break;

        case OPT_VERIFY_MERCY:
            argsp->psflags |= PSVERIFY_MERCY;
            break;

            
        case OPT_VERIFY_DEPS:
            argsp->psflags |= PSVERIFY_DEPS;
            if (argsp->mjrmode != MODE_VERIFY)
                check_mjrmode(argsp);
            argsp->mjrmode = MODE_VERIFY;
            break;

        case OPT_VERIFY_CNFLS:
            argsp->psflags |= PSVERIFY_CNFLS;
            if (argsp->mjrmode != MODE_VERIFY)
                check_mjrmode(argsp);
            argsp->mjrmode = MODE_VERIFY;
            break;

        case OPT_VERIFY_FILECNFLS:
            argsp->psflags |= PSVERIFY_FILECNFLS;
            if (argsp->mjrmode != MODE_VERIFY)
                check_mjrmode(argsp);
            argsp->mjrmode = MODE_VERIFY;
            break;

        case OPT_VERIFY_ALL:
            argsp->psflags |= PSVERIFY_DEPS | PSVERIFY_CNFLS |
                PSVERIFY_FILECNFLS;
            
            if (argsp->mjrmode != MODE_VERIFY)
                check_mjrmode(argsp);
            argsp->mjrmode = MODE_VERIFY;
            break;
            
#ifdef ENABLE_INTERACTIVE_MODE
        case OPT_SHELLMODE:
            if (argsp->mjrmode != MODE_SHELL)
                check_mjrmode(argsp);
            argsp->mjrmode = MODE_SHELL;
            argsp->psflags |= PSMODE_UPGRADE;
            break;

        case OPT_SHELL_SKIPINSTALLED:
            argsp->shell_skip_installed = 1;
            break;

        case OPT_SHELL_CMD:
            if (argsp->mjrmode != MODE_SHELL)
                check_mjrmode(argsp);
            argsp->mjrmode = MODE_SHELL;
            argsp->shcmd = arg;
            break;
#endif            
        
        case OPT_MKIDX:
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_MKIDX;
            argsp->psflags |= PSMODE_MKIDX;
            argsp->idx_path = trimslash(arg);
            argsp->idx_type = INDEXTYPE_TXT;
            break;

        case OPT_MKIDXZ:
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_MKIDX;
            argsp->psflags |= PSMODE_MKIDX;
            argsp->idx_path = trimslash(arg);
            argsp->idx_type = INDEXTYPE_TXTZ;
            break;
            
        case OPT_NODESC:
	    argsp->pkgdir_creat_flags |= PKGDIR_CREAT_NODESC;
	    break;
            
        case OPT_INST_INSTDIST:
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_INSTALLDIST;
            argsp->inst.rootdir = trimslash(arg);
            argsp->psflags |= PSMODE_INSTALL | PSMODE_INSTALL_DIST;
            break;
            
        case OPT_INST_UPGRDIST:
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_UPGRADEDIST;
            if (arg) 
                argsp->inst.rootdir = trimslash(arg);
            else if (argsp->inst.rootdir == NULL)
                argsp->inst.rootdir = "/";
            
            argsp->psflags |= PSMODE_UPGRADE | PSMODE_UPGRADE_DIST;
            break;

        case OPT_INST_HOLD:
            if (strchr(arg, ',') == NULL) {
                n_array_push(argsp->inst.hold_pkgnames, strdup(arg));
                
            } else {
                const char **pkgs, **p;
            
                p = pkgs = n_str_tokl(arg, ",");
                while (*p) {
                    n_array_push(argsp->inst.hold_pkgnames, strdup(*p));
                    p++;
                }
                n_str_tokl_free(pkgs);
            }
            break;
            
        case OPT_INST_NOHOLD:
            argsp->inst.flags |= INSTS_NOHOLD;
            break;

        case OPT_INST_GREEDY:
            argsp->inst.flags |= INSTS_GREEDY;
            break;
            
        case 'i':
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_INSTALL;
            argsp->psflags |= PSMODE_INSTALL;
            break;
            
        case 'U':
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_UPGRADE;
            argsp->psflags |= PSMODE_UPGRADE;
            break;

        case 'r':
            argsp->inst.rootdir = trimslash(arg);
            break;
            
        case OPT_INST_RPMDEF:
            n_assert(argsp->inst.rpmacros);
            n_array_push(argsp->inst.rpmacros, arg);
            break;
            
            
        case OPT_INST_FETCH:
            argsp->inst.fetchdir = trimslash(arg);
            argsp->inst.flags |= INSTS_JUSTFETCH;
            break;

        case OPT_INST_MKSCRIPT:
            argsp->inst.flags |= INSTS_JUSTPRINT;
            argsp->inst.dumpfile = trimslash(arg);
            break;

        case OPT_INST_POLDEK_MKSCRIPT:
            argsp->inst.flags |= INSTS_JUSTPRINT_N;
            argsp->inst.dumpfile = trimslash(arg);
            break;

        case OPT_INST_FRESHEN:
            argsp->inst.flags |= INSTS_FRESHEN;
            argsp->inst.dumpfile = trimslash(arg);
            break;

        case OPT_INST_NOFOLLOW:
            argsp->inst.flags &= ~(INSTS_FOLLOW);
            break;
            
        case OPT_INST_NODEPS:
            argsp->inst.instflags  |= PKGINST_NODEPS;
            break;

        case OPT_INST_FORCE:
            argsp->inst.instflags |= PKGINST_FORCE;
            break;
            
        case OPT_INST_JUSTDB:
            argsp->inst.instflags |= PKGINST_JUSTDB;
            break;

        case 't':
            if (argsp->inst.flags & INSTS_TEST)
                argsp->inst.instflags |= PKGINST_TEST;
            else
                argsp->inst.flags |= INSTS_TEST;
            break;

        case OPT_INST_MKDBDIR:
            argsp->inst.flags |= INSTS_MKDBDIR;
            break;

        case OPT_CONF:
            argsp->conf_path = arg;
            break;
            
        case OPT_NOCONF:
            argsp->noconf = 1;
            break;

        case OPT_SPLITSIZE: {
            char *p, rc;
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_SPLIT;

            if ((p = strrchr(arg, ':'))) {
                rc = sscanf(arg, "%d:%d", &argsp->split_conf.size,
                            &argsp->split_conf.first_free_space);
                rc = (rc == 2);
            } else {
                rc = sscanf(arg, "%d", &argsp->split_conf.size);
                rc = (rc == 1);
            }
            if (!rc) {
                logn(LOGERR, _("split: bad option argument"));
                exit(EXIT_FAILURE);
            }
        }
        break;
            
        case OPT_SPLITCONF:
            argsp->split_conf.conf = arg;
            break;

        case OPT_SPLITOUTPATH:
            argsp->split_conf.prefix = arg;
            break;
            
        case ARGP_KEY_ARG:
            if (strncmp(arg, "--rpm-", 6) != 0) 
                n_array_push(argsp->pkgdef_files, arg);
            
            else if (strlen(arg) > 8) {
                char *optname;
                arg += strlen("--rp");
                *arg = '-';
                
                optname = arg + 2;
                if (strncmp(optname, "force", 5) == 0 ||
                    strncmp(optname, "install", 7) == 0 ||
                    strncmp(optname, "upgrade", 7) == 0 ||
                    strncmp(optname, "nodeps", 6) == 0  ||
                    strncmp(optname, "justdb", 6) == 0  ||
                    strncmp(optname, "test", 4) == 0    ||
                    strncmp(optname, "root", 4) == 0)
                 {
                     logn(LOGERR, _("'%s' option should be set by --%s"),
                         optname, optname);
                     exit(EXIT_FAILURE);
                 }
                
                n_assert(argsp->inst.rpmopts != NULL);
                n_array_push(argsp->inst.rpmopts, arg);
                
            } else {
                argp_usage (state);
            }
                    
            break;
     
        case ARGP_KEY_END:
            //argp_usage (state);
            break;
           
        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static void n_assert_hook(const char *expr, const char *file, int line) 
{
    printf("Something wrong, something not quite right.\n"
           "Assertion '%s' failed, %s:%d\n"
           "Please report this bug to %s.\n\n",
           expr, file, line, argp_program_bug_address);
    abort();
}

     
void poldek_init(void) 
{
#ifdef HAVE_MALLOPT
# include <malloc.h>
    //mallopt(M_MMAP_THRESHOLD, 1);
    //mallopt(M_MMAP_MAX, 0);
#endif /* HAVE_MALLOPT */
    
    n_assert_sethook(n_assert_hook);
    pkgflmodule_init();
    pkgsetmodule_init();
}

void poldek_destroy(void) 
{
    pkgsetmodule_destroy();
    pkgflmodule_destroy();
    
    if (htcnf)
        n_hash_free(htcnf);
}

static
void parse_options(int argc, char **argv) 
{
    struct argp argp = { options, parse_opt, args_doc, poldek_banner, 0, 0, 0};
    int vfile_cnflags = 0, is_multi;
    char *v;


    verbose = 0;
    
    memset(&args, 0, sizeof(args));

    args.sources = n_array_new(4, (tn_fn_free)source_free, (tn_fn_cmp)source_cmp);
    args.curr_src_path = NULL;
    args.curr_src_ldmethod = PKGSET_LD_NIL;
    args.idx_path = NULL;
    args.pkgdef_files = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    args.pkgdef_defs  = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    args.pkgdef_sets  = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    args.split_conf.size = 0;
    args.split_conf.first_free_space = 0;
    args.split_conf.conf = NULL;
    args.split_conf.prefix = NULL;
    args.shcmd = NULL;
    inst_s_init(&args.inst);

    argp_parse(&argp, argc, argv, 0, 0, &args);

    if (args.noconf && args.conf_path) {
        logn(LOGERR, _("--noconf and --conf are exclusive, aren't they?"));
        exit(EXIT_FAILURE);
    }

    if (args.curr_src_path) { 
        struct source *src = source_new(args.curr_src_path, NULL);
        src->ldmethod = args.curr_src_ldmethod;
        n_array_push(args.sources, src);
    }
    
    if (args.conf_path != NULL)
        htcnf = ldconf(args.conf_path);
    else if (args.noconf == 0)
        htcnf = ldconf_deafult();
    
    if (n_array_size(args.sources) == 0) {
        int i;
        
        if ((v = conf_get(htcnf, "source", &is_multi))) {
            if (is_multi == 0) {
                n_array_push(args.sources, source_new(v, NULL));
            } else {
                tn_array *paths = NULL;
                if ((paths = conf_get_multi(htcnf, "source"))) {
                    while (n_array_size(paths)) 
                        n_array_push(args.sources,
                                     source_new(n_array_shift(paths), NULL));
                }
            }
        }

        /* source\d+, prefix\d+ pairs  */
        for (i=0; i<100; i++) {
            char opt[64], *src;
            
            snprintf(opt, sizeof(opt), "source%d", i);
            if ((src = conf_get(htcnf, opt, NULL))) {
                snprintf(opt, sizeof(opt), "prefix%d", i);
                n_array_push(args.sources,
                             source_new(src, conf_get(htcnf, opt, NULL)));
            }
        }
    }
    

    if (n_array_size(args.sources) == 0) {
        logn(LOGERR, _("no source specified"));
        exit(EXIT_FAILURE);
    }


    if (args.mjrmode == 0 && args.mnrmode == 0) {
#ifdef ENABLE_INTERACTIVE_MODE
        args.mjrmode = MODE_SHELL;
#else         
        logn(LOGERR, _("so what?"));
        exit(EXIT_FAILURE);
#endif        
    }
    
    if (args.mjrmode == MODE_VERIFY && args.has_pkgdef == 0)
        args.psflags |= PSMODE_VERIFY;

    args.has_pkgdef = n_array_size(args.pkgdef_sets) +
        n_array_size(args.pkgdef_defs) +
        n_array_size(args.pkgdef_files);
    
    
    if ((v = conf_get(htcnf, "use_sudo", NULL)) != NULL &&
        strcmp(v, "yes") == 0)
        args.inst.flags |= INSTS_USESUDO;

    if ((v = conf_get(htcnf, "mercy", NULL)) != NULL &&
        strcmp(v, "yes") == 0)
        args.psflags |= PSVERIFY_MERCY;

    if ((v = conf_get(htcnf, "keep_downloads", NULL)) != NULL &&
        strcmp(v, "yes") == 0)
        args.inst.flags |= INSTS_KEEP_DOWNLOADS;

    if ((args.inst.flags & INSTS_GREEDY) == 0) { /* no --greedy specified */
        if ((v = conf_get(htcnf, "greedy", NULL)) && strcmp(v, "yes") == 0)
            args.inst.flags |= INSTS_GREEDY;
    }

    if ((args.inst.flags & INSTS_GREEDY) &&
        (args.inst.flags & INSTS_FOLLOW) == 0) {
        logn(LOGERR, _("--greedy and --nofollow are exclusive"));
        exit(EXIT_FAILURE);
    }
        
    if (args.inst.flags & INSTS_FOLLOW) { /* no --nofollow specified */
        if ((v = conf_get(htcnf, "follow", NULL)) && strcmp(v, "no") == 0) {
            if (args.inst.flags & INSTS_GREEDY) 
                logn(LOGWARN, _("ignore config's follow - greedy implies "
                    "it to \"yes\""));
            else 
                args.inst.flags &= ~INSTS_FOLLOW;
        }
    }  
    
    if ((v = conf_get(htcnf, "cachedir", NULL)))
        args.inst.cachedir = v;
    
    if ((v = conf_get(htcnf, "ftp_http_get", NULL))) {
        vfile_cnflags |= VFILE_USEXT_FTP | VFILE_USEXT_HTTP;
        vfile_register_ext_handler(VFURL_FTP | VFURL_HTTP, v);
    }
    
    if ((v = conf_get(htcnf, "ftp_get", NULL))) {
        vfile_cnflags |= VFILE_USEXT_FTP;
        vfile_register_ext_handler(VFURL_FTP, v);
    }
    
    if ((v = conf_get(htcnf, "http_get", NULL))) {
        vfile_cnflags |= VFILE_USEXT_HTTP;
        vfile_register_ext_handler(VFURL_HTTP, v);
    }
    
    if ((v = conf_get(htcnf, "https_get", NULL))) {
        vfile_cnflags |= VFILE_USEXT_HTTPS;
        vfile_register_ext_handler(VFURL_HTTPS, v);
    }
        
    if ((v = conf_get(htcnf, "rsync_get", NULL))) 
        vfile_register_ext_handler(VFURL_RSYNC, v);
    
    if ((v = conf_get(htcnf, "cdrom_get", NULL)))
        vfile_register_ext_handler(VFURL_CDROM, v);
    
    if ((v = conf_get(htcnf, "rpmdef", &is_multi))) {
        tn_array *macros = NULL;
        
        if (is_multi) {
            macros = conf_get_multi(htcnf, "rpmdef");
            while (n_array_size(macros))
                n_array_push(args.inst.rpmacros,
                             strdup(n_array_shift(macros)));
        } else {
            n_array_push(args.inst.rpmacros, v);
        }
    }

    if ((v = conf_get(htcnf, "hold", &is_multi))) {
        tn_array *holds = NULL;
        
        if (is_multi) {
            holds = conf_get_multi(htcnf, "hold");
            while (n_array_size(holds)) 
                n_array_push(args.inst.hold_pkgnames, n_array_shift(holds));
            
        } else {
            n_array_push(args.inst.hold_pkgnames, v);
        }
    }
    
    vfile_verbose = &verbose;
    n_assert(args.inst.cachedir); 
    vfile_configure(args.inst.cachedir, vfile_cnflags);
    
    vfile_msg_fn = log_msg;
    vfile_err_fn = log_err;
}


static struct pkgset *load_pkgset(int ldflags) 
{
    struct pkgset *ps;
    
    if ((ps = pkgset_new(args.psflags)) == NULL)
        return NULL;
    
    if (!pkgset_load(ps, ldflags, args.sources)) {
        logn(LOGERR, _("no packages loaded"));
        pkgset_free(ps);
        ps = NULL;
    }
    mem_info(1, "MEM after load");

    if (ps) {
        pkgset_setup(ps, args.split_conf.conf);
        if ((args.inst.flags & INSTS_NOHOLD) == 0) {
            if (n_array_size(args.inst.hold_pkgnames) == 0) 
                read_holds(NULL, args.inst.hold_pkgnames);

            if (n_array_size(args.inst.hold_pkgnames) > 0) {
                pkgset_mark_holds(ps, args.inst.hold_pkgnames);
                
            } else {
                n_array_free(args.inst.hold_pkgnames);
                args.inst.hold_pkgnames = NULL;
            }
        }
    }
    

    return ps;
}

static int update_whole_idx(void)
{
    int i, nerr = 0;
    
    for (i=0; i<n_array_size(args.sources); i++)
        if (!source_update(n_array_nth(args.sources, i)))
            nerr++;
    
    return nerr == 0;
}


static int update_idx(void)
{
    int i, nerr = 0;
    
    if (args.update_op == OPT_UPDATEIDX_WHOLE)
        return update_whole_idx();
    
    for (i=0; i < n_array_size(args.sources); i++) {
        struct source *src = n_array_nth(args.sources, i);
        struct pkgdir *pkgdir;
        int n;
        
        pkgdir = pkgdir_new(src->source_name, src->source_path,
                            src->pkg_prefix, PKGDIR_NEW_VERIFY);
        
        if (pkgdir == NULL) {
            if (n_array_size(args.sources) > 1)
                logn(LOGERR, _("%s: load failed, skipped"), src->source_path);
            nerr++;
            continue;
        }

        if (pkgdir->vf->vf_flags & VF_FETCHED) /* already downloaded */
            continue;

        n = 0;
        if (pkgdir_update(pkgdir, &n) && n)
            if (!pkgdir_create_idx(pkgdir, NULL, args.pkgdir_creat_flags))
                nerr++;
        pkgdir_free(pkgdir);
    }
    return nerr == 0;
}


static int mkidx(struct pkgset *ps) 
{
    struct source   *src;
    struct pkgdir   *pkgdir, *orig;
    char            path[PATH_MAX], *idx_path = NULL;
    int             nerr = 0;
    time_t          ts;
    unsigned        cr_flags = args.pkgdir_creat_flags;
    
    n_assert(ps);
    if (n_array_size(args.sources) > 1) {
        logn(LOGERR, _("multiple sources not allowed for mkidx option"));
        return 0;
    }

    src = n_array_nth(args.sources, 0);
    
    if (strstr(src->source_path, "://")) {
        logn(LOGERR, _("mkidx requested for remote source without destination "
            "specified"));
        return 0;
    }

    trimslash(src->source_path);

    if (args.idx_path != NULL) {
        idx_path = args.idx_path;
        
    } else {
        switch (args.idx_type) {
            case INDEXTYPE_TXTZ:
                snprintf(path, sizeof(path), "%s/%s.gz", src->source_path, 
                         default_pkgidx_name);
                break;
                
            case INDEXTYPE_TXT:
                snprintf(path, sizeof(path), "%s/%s", src->source_path, 
                         default_pkgidx_name);
                break;
                
            default:
                n_assert(0);
                exit(EXIT_FAILURE);
        }
        
        idx_path = path;
    }
    
    n_assert(idx_path != NULL);

    ts = time(0);
    pkgdir = n_array_nth(ps->pkgdirs, 0);
    pkgdir->ts = ts;
    
    if (access(idx_path, R_OK) == 0) {
        struct pkgdir *diff;
        
        msgn(1, _("Loading previous %s..."), idx_path);
        orig = pkgdir_new("", idx_path, NULL, PKGDIR_NEW_VERIFY);
        
        
        if (orig != NULL && pkgdir_load(orig, NULL, 0)) {
            if (orig->ts > pkgdir->ts) {
                logn(LOGWARN, _("clock skew detected; create index with fake "
                                "timestamp"));
            }
            if (orig->ts >= pkgdir->ts) 
                pkgdir->ts = orig->ts + 1;
            
            if ((diff = pkgdir_diff(orig, pkgdir))) {
                diff->ts = pkgdir->ts;
                if (!pkgdir_create_idx(diff, idx_path, cr_flags))
                    nerr++;
            }
        }
    }

    if (nerr == 0 && !pkgdir_create_idx(pkgdir, idx_path, cr_flags))
        nerr++;
    return nerr == 0;
}


int prepare_given_packages(void) 
{
    int i, rc = 1;
    
    if (args.ups == NULL)
        args.ups = usrpkgset_new();

    for (i=0; i<n_array_size(args.pkgdef_sets); i++) {
        char *path = n_array_nth(args.pkgdef_sets, i);
        
        if (!usrpkgset_add_list(args.ups, path))
            rc = 0;
    }

    for (i=0; i<n_array_size(args.pkgdef_defs); i++) {
        char *str = n_array_nth(args.pkgdef_defs, i);

        if (!usrpkgset_add_str(args.ups, str, strlen(str)))
            rc = 0;
    }

    for (i=0; i<n_array_size(args.pkgdef_files); i++) {
        char *path = n_array_nth(args.pkgdef_files, i);

        if (access(path, R_OK) == 0) 
            rc = usrpkgset_add_file(args.ups, path);
        else
            rc = usrpkgset_add_str(args.ups, path, strlen(path));
    }
    
    usrpkgset_setup(args.ups);
    return usrpkgset_size(args.ups);
}

static int check_install_flags(void) 
{
    if ((args.inst.flags & INSTS_GREEDY))
        args.inst.flags |= INSTS_FOLLOW;
    return 1;
}

static
int check_args(void) 
{
    int i, rc = 1;

#ifdef ENABLE_INTERACTIVE_MODE
    if (args.mjrmode == MODE_NULL && args.mnrmode == MODE_NULL) 
        args.mjrmode = MODE_SHELL;
#endif    

    switch (args.mjrmode) {
        case MODE_NULL:
            if (args.mnrmode == 0) {
                logn(LOGERR, _("so what?"));
                exit(EXIT_FAILURE);
            }
            break;

#ifdef ENABLE_INTERACTIVE_MODE
        case MODE_SHELL:
            if (verbose == 0)
                verbose = 1;
#endif            
        case MODE_VERIFY:
            if (args.has_pkgdef)
                rc = prepare_given_packages();
            break;
            
        case MODE_MKIDX:
            if (verbose >= 0)
                verbose += 1;
            
            n_assert(args.sources);
            for (i=0; i<n_array_size(args.sources); i++) {
                struct source *src = n_array_nth(args.sources, i);
                src->ldmethod = PKGSET_LD_DIR;
            }
            break;

            
        case MODE_INSTALLDIST:
        case MODE_INSTALL:
        case MODE_UPGRADE:
            if (prepare_given_packages() > 0) {
                rc = check_install_flags();
                
            } else {
                logn(LOGERR, _("no packages to install"));
                rc = 0;
            }
                
            break;
            
        case MODE_UPGRADEDIST:
            if (args.has_pkgdef) {
                logn(LOGERR, _("this option upgrades whole dist, not given packages"));
                exit(EXIT_FAILURE);
            }
            rc = check_install_flags();
            break;

        case MODE_SPLIT:
            if (args.split_conf.size == 0) {
                logn(LOGERR, _("missing split size"));
                exit(EXIT_FAILURE);
            }
            
            if (args.split_conf.size < 50) {
                logn(LOGERR, _("split size too small"));
                exit(EXIT_FAILURE);
            }
            
            if (args.split_conf.size < args.split_conf.first_free_space) {
                logn(LOGERR, _("first free space bigger than chunk size"));
                exit(EXIT_FAILURE);
            }

            args.split_conf.size *= 1024 * 1000;
            args.split_conf.first_free_space *= 1024 * 1000;
            if (args.split_conf.prefix == NULL) 
                args.split_conf.prefix = "packages.chunk";
            
            break;
        default:
            n_assert(0);
            exit(EXIT_FAILURE);
    }
    return rc;
}


int mklock(void) 
{
    char path[PATH_MAX];
    int rc;
    
    n_assert(args.inst.cachedir);

    snprintf(path, sizeof(path), "%s/poldek..lck", args.inst.cachedir);

    rc = lockfile(path);
    
    if (rc == 0) {
        char buf[64];
        pid_t pid = readlockfile(path);
        
        if (pid > 0) 
            snprintf(buf, sizeof(buf), " (%d)", pid);
        else
            *buf = '\0';
            
        logn(LOGERR, _("There seems another poldek%s uses %s"),
            buf, args.inst.cachedir);
    }

    return rc > 0; 
}


int mark_usrset(struct pkgset *ps, struct usrpkgset *ups,
                struct inst_s *inst, int mjrmode) 
{
    int rc;
    int markflag = MARK_USET;
    
    if (mjrmode == MODE_VERIFY && verbose < 2 && verbose != -1) 
        verbose = 1;

    if (mjrmode == MODE_VERIFY || mjrmode == MODE_INSTALLDIST)
        markflag = MARK_DEPS;
    
    if (n_array_size(ups->pkgdefs) == 0) {
        logn(LOGERR, _("no packages specified"));
        exit(EXIT_FAILURE);
    }

    rc = pkgset_mark_usrset(ps, ups, inst, markflag);
    usrpkgset_free(ups);
    return rc;
}

void self_init(void) 
{
    uid_t uid;

    uid = getuid();
    if (uid != geteuid() || getgid() != getegid()) {
        logn(LOGERR, _("I'm set*id'ed, give up"));
        exit(EXIT_FAILURE);
    }

    if (uid == 0) {
        logn(LOGWARN, _("Running me as root is not a good habbit"));
        sleep(1);
    }
}
    

int main(int argc, char **argv)
{
    struct pkgset   *ps;
    char            *logprefix = "poldek";
    int             rc = 1, ldflags;
    
    
    mem_info_verbose = -1;

    log_init(NULL, stdout, logprefix);
    self_init();

    setlocale(LC_MESSAGES, "");
    setlocale(LC_CTYPE, "");
    bindtextdomain(PACKAGE, NULL);
    textdomain(PACKAGE);
    translate_argp_options(options);
    
    
    term_init();
    log_init(NULL, stdout, logprefix);
    parse_options(argc, argv);

    if (args.log_path) 
        log_init(args.log_path, stdout, logprefix);

    
    if (!mklock())
        exit(EXIT_FAILURE);
    
    if (!check_args())
        exit(EXIT_FAILURE);

    poldek_init();
    rpm_initlib(args.inst.rpmacros);
    
    if (args.mnrmode & MODE_MNR_UPDATEIDX) {
        int v = verbose;

        if (verbose >= 0) 
            verbose++;
        //if (verbose < 1 && verbose != -1)
        //verbose = 1;
        
        if (!update_idx())
            exit(EXIT_FAILURE);
        
        if (args.mjrmode == MODE_NULL)
            exit(EXIT_SUCCESS);
        verbose = v;
    }

    if (args.mjrmode == MODE_VERIFY && args.has_pkgdef == 0 && verbose != -1)
        verbose += 2;

    ldflags = 0;
    if (args.mjrmode == MODE_MKIDX)
        ldflags = PKGDIR_LD_RAW;
    
    else if (args.mjrmode == MODE_VERIFY) 
        ldflags = PKGDIR_LD_VERIFY;

#ifdef ENABLE_INTERACTIVE_MODE        
    if (args.mjrmode == MODE_SHELL && args.shcmd == NULL)
        printf("%s\n", poldek_banner);        
#endif
    
    if ((ps = load_pkgset(ldflags)) == NULL)
        exit(EXIT_FAILURE);

    
    switch (args.mjrmode) {
        case MODE_NULL:
            if (args.mnrmode != 0)
                break;
#ifndef ENABLE_INTERACTIVE_MODE
            n_assert(0);
#endif
            
#ifdef ENABLE_INTERACTIVE_MODE
        case MODE_SHELL:
            if (args.shcmd != NULL) 
                rc = shell_exec(ps, &args.inst, args.shell_skip_installed,
                                args.shcmd);
            else
                rc = shell_main(ps, &args.inst, args.shell_skip_installed);
            
            
            break;
#endif            
        case MODE_VERIFY:
            if (args.has_pkgdef)
                if ((rc = usrpkgset_size(args.ups)))
                    rc = mark_usrset(ps, args.ups, &args.inst, args.mjrmode);
                    
            break;
            
        case MODE_MKIDX:
            rc = mkidx(ps);
            break;
            
        case MODE_INSTALLDIST:
            if (args.has_pkgdef == 0)
                rc = 0;
            
            else if ((rc = usrpkgset_size(args.ups))) {
                rc = mark_usrset(ps, args.ups, &args.inst, args.mjrmode);
                if (rc) 
                    rc = install_dist(ps, &args.inst);
            }
            break;

        case MODE_INSTALL:
        case MODE_UPGRADE:
            if ((rc = usrpkgset_size(args.ups))) {
                if ((rc = mark_usrset(ps, args.ups, &args.inst, args.mjrmode))) 
                    rc = install_pkgs(ps, &args.inst, NULL);
            }
            break;
            
        case MODE_UPGRADEDIST:
            rc = upgrade_dist(ps, &args.inst);
            break;

        case MODE_SPLIT:
            rc = packages_split(ps->pkgs,
                                args.split_conf.size,
                                args.split_conf.first_free_space, 
                                args.split_conf.conf, args.split_conf.prefix);
            break;

            
        default:
            n_assert(0);
            exit(EXIT_FAILURE);
    }

    pkgset_free(ps);
    mem_info(1, "MEM at the end");
    poldek_destroy();
    mem_info(1, "MEM at the *real* end");

    return rc ? EXIT_SUCCESS : EXIT_FAILURE;
}
