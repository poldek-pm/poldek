/*
  Copyright (C) 2000 - 2002 Pawel A. Gajda <mis@k2.net.pl>

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

#include <stdlib.h>
#include <string.h>

#include <argp.h>
#include <sys/types.h>
#include <sys/stat.h> 
#include <unistd.h>
#include <time.h>
#include <fnmatch.h>

#include <trurl/narray.h>
#include <trurl/nassert.h>
#include <vfile/vfile.h>

#include "i18n.h"
#include "log.h"
#include "source.h"
#include "pkgset.h"
#include "usrset.h"
#include "misc.h"
#include "rpm.h"
#include "install.h"
#include "conf.h"
#include "split.h"
#include "poldek_term.h"

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
"Copyright (C) 2000-2002 Pawel A. Gajda <mis@pld.org.pl>\n"
"This program may be freely redistributed under the terms of the GNU GPL v2\n";
/* A description of the arguments we accept. */
static char args_doc[] = N_("[PACKAGE...]");

#define MODE_NULL         0
#define MODE_VERIFY       (1 << 1)
#define MODE_MKIDX        (1 << 2)
#define MODE_INSTALLDIST  (1 << 3)
#define MODE_INSTALL      (1 << 4)
#define MODE_UPGRADEDIST  (1 << 5)
#define MODE_UPGRADE      (1 << 6)
#define MODE_SPLIT        (1 << 7)
#define MODE_SRCLIST      (1 << 8)
#define MODE_UNINSTALL    (1 << 9)
                           
#ifdef ENABLE_INTERACTIVE_MODE
# define MODE_SHELL       (1 << 10)
#endif

#define MODE_IS_NOSCORE      (MODE_VERIFY | MODE_MKIDX | MODE_SPLIT | MODE_SRCLIST) 

#define MODE_MNR_UPDATEIDX  (1 << 0)
#define MODE_MNR_CLEANIDX   (1 << 1)

#if 0                           /* NFY */
#define MODEFLAG_LD_PKGSET     (1 << 0)
#define MODEFLAG_LOCK_CACHEDIR (1 << 1)
#define MODEFLAG_LD_DBDEPS     (1 << 2)

struct mjrmode_conf {
    int modeid;
    const char *name;           /* --MODE */
    const char *short;          /* -M     */
    unsigned flags;
};

static
struct mjrmode_conf mjrmodes[] = {
    MODE_NULL,         0,                          /* 0 */
    MODE_VERIFY,       MODEFLAG_LD_PKGSET | MODEFLAG_LOCK_CACHEDIR,        /* 1 */
    MODE_MKIDX,        MODEFLAG_LD_PKGSET          /* 2 */
    MODE_INSTALLDIST,  MODEFLAG_NEEDS_PKGSET,      /* 3 */
    
    MODEFLAG_NEEDS_PKGSET,      /* 4 */
    MODEFLAG_NEEDS_PKGSET,      /* 5 */
    MODEFLAG_NEEDS_PKGSET,      /* 6 */
    MODEFLAG_NEEDS_PKGSET,      /* 7 */
    0,                          /* 8 */
    0,                          /* 9 */
    MODEFLAG_NEEDS_PKGSET,      /* 10 */
};
#endif

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

    unsigned  switches;
    int       update_op;
    int       clean_whole;
    
    char      *curr_src_path;
    int       curr_src_type;
    tn_array  *sources;         /* --source args */
    tn_array  *source_names;    /* --sn args */
    
    int       idx_type;
    char      *idx_path;
    
    int       has_pkgdef;
    tn_array  *pkgdef_files;    /* foo.rpm      */
    tn_array  *pkgdef_defs;     /* --nevr "foo 1.2" or "foo" or "foo*" */
    tn_array  *pkgdef_sets;     /* -p ftp://ftp.zenek.net/PLD/tiny */
    
    unsigned   psflags;
    struct inst_s inst;
    
    struct usrpkgset  *ups;
    
    char        *conf_path;
    int         noconf;
    char        *log_path;
    
    unsigned    pkgdir_creat_flags; 
    int         pkgdir_nodiff;
    
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
#define OPT_NODIFF	 1005 /* don't create diff */

#define OPT_SOURCETXT   1015
#define OPT_SOURCEDIR   1016
#define OPT_SOURCEHDL   1017
#define OPT_PKGPREFIX   1018
#define OPT_UPDATEIDX   1019

#define OPT_UPDATEIDX_WHOLE   1020

#define OPT_SOURCECACHE 1021
#define OPT_CLEANIDX    1022
#define OPT_CLEANIDX_WHOLE 1023

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
#define OPT_INST_IGNORE           1055
#define OPT_INST_NOIGNORE         1056

#define OPT_INST_GREEDY           'G'
#define OPT_INST_REINSTALL        1057
#define OPT_INST_DOWNGRADE        1058

#define OPT_UNINSTALL             'e'

#define OPT_SPLITSIZE             1100
#define OPT_SPLITCONF             1101
#define OPT_SPLITOUTPATH          1102


#define OPT_NEVR                  1110
#define OPT_PKGSET                1111


#define OPT_CONF                  'c'
#define OPT_NOCONF                2002 
#define OPT_LOG                   'L'
#define OPT_V016                  2004
#define OPT_BANNER                2005
#define OPT_ASK                   2006
#define OPT_NOASK                 2007

#define OPT_SW_V016               (1 << 0)
#define OPT_SW_NOASK              (1 << 1)
#define OPT_SW_NOCONF             (1 << 2)

/* The options we understand. */
static struct argp_option options[] = {

{0,0,0,0, N_("Specifying source:"), 1 },    
{"source", 's', "SOURCE", 0, N_("Get packages info from SOURCE"), 1 },

{"sn", 'n', "SOURCE-NAME", 0, N_("Get packages info from source named SOURCE-NAME"), 1 },
    
{"sidx", OPT_SOURCETXT, "FILE", OPTION_HIDDEN,
 N_("Get packages info from package index file FILE"), 1 },

{"sdir", OPT_SOURCEDIR, "DIR", 0, 
 N_("Get packages info from directory DIR by scanning it"), 1 },

{"shdrl", OPT_SOURCEHDL, "FILE", 0, /* RH's hdlist,  PLD's tocfile */
 N_("Get packages info from package header list file (aka hdlist)"), 1 },    

{"prefix", 'P', "PREFIX", 0,
 N_("Get packages from PREFIX instead of SOURCE"), 1 },


{0,0,0,0, N_("Source options:"), 11 },        
{"sl", 'l', 0, 0, N_("List configured sources"), 11 },            

{"update", OPT_UPDATEIDX, 0, 0, 
 N_("Update index of source and verify it"), 11 },

{"up", OPT_UPDATEIDX, 0, OPTION_ALIAS, 0, 11 }, 

{"update-whole", OPT_UPDATEIDX_WHOLE, 0, 0, 
 N_("Update whole index of source"), 11 },

{"upa", OPT_UPDATEIDX_WHOLE, 0, OPTION_ALIAS, 0, 11 },

{"clean", OPT_CLEANIDX, 0, 0, 
 N_("Remove source index files from cache directory"), 11 },

{"clean-whole", OPT_CLEANIDX_WHOLE, 0, 0, 
 N_("Remove all source files from cache directory"), 11 },     

{"cleana", OPT_CLEANIDX_WHOLE, 0, OPTION_ALIAS, 0, 11 },


{0,0,0,0, N_("Verify options:"), 50 },        
{"verify",  OPT_VERIFY_DEPS, 0, 0, N_("Verify package dependencies"), 50 },
{"verify-conflicts",  OPT_VERIFY_CNFLS, 0, 0, N_("Verify package conflicts"), 50 },
{"verify-fileconflicts",  OPT_VERIFY_FILECNFLS, 0, 0,
     N_("Verify package file conflicts"), 50 },
{"verify-all",  OPT_VERIFY_ALL, 0, 0,
     N_("Verify dependencies, conflicts and file conflicts"), 50 },
{"mercy",   OPT_VERIFY_MERCY, 0, 0,
     N_("Be tolerant for bugs which RPM tolerates"), 50 },


{0,0,0,0, N_("Index creation:"), 60},
{"mkidx", OPT_MKIDX, "FILE", OPTION_ARG_OPTIONAL,
 N_("Create package index, SOURCE/packages.dir by default"), 60},

{"mkidxz", OPT_MKIDXZ, "FILE", OPTION_ARG_OPTIONAL,
 N_("Likewise, but gzipped file is created"), 60},

{"nodesc", OPT_NODESC, 0, 0,
 N_("Don't put packages user-level information (like Summary or Description)"
     " in created index."), 60 },

{"nodiff", OPT_NODIFF, 0, 0,
 N_("Don't create \"patch\""), 60 },

{0,0,0,0, N_("Packages spec:"), 65},
{"pset", OPT_PKGSET, "FILE", 0, N_("Take package set definition from FILE"), 65 },
{"pkgset", 0, 0, OPTION_ALIAS | OPTION_HIDDEN, 0, 65 }, /* backward compat */

// obsoleted by '#'    
{"nevr", OPT_NEVR, "\"NAME [[E:][V[-R]]]\"", OPTION_HIDDEN,
     "Specifies package by NAME and EVR", 65 },
    
{"pkgnevr", 0, 0, OPTION_ALIAS | OPTION_HIDDEN, 0,  65 }, /* backward compat */

{0,0,0,0, N_("Installation:"), 70},
{"install-dist", OPT_INST_INSTDIST, "DIR", 0,
     N_("Install package set under DIR as root directory"), 70 },

{"upgrade-dist", OPT_INST_UPGRDIST, "DIR", OPTION_ARG_OPTIONAL,
     N_("Upgrade all packages needs upgrade"), 70 },

    
{"install", 'i', 0, 0, N_("Install given packages"), 70 },
{"reinstall", OPT_INST_REINSTALL, 0, 0, N_("Reinstall given packages"), 70 },
{"downgrade", OPT_INST_DOWNGRADE, 0, 0, N_("Downgrade given packages"), 70 },
{"upgrade", 'u', 0, 0, N_("Upgrade given packages"), 70 },
    {0, 'U', 0, OPTION_ALIAS, 0, 70 },
{"erase", OPT_UNINSTALL, 0, 0, N_("Uninstall given packages"), 70 },

{0,0,0,0, N_("Installation switches:"), 71},
{"checksig", 'K', 0, OPTION_HIDDEN, /* not implemented yet */
     N_("Verify packages signatures before install"), 71 },    
{"root", 'r', "DIR", 0, N_("Set top directory to DIR"), 71 },
{"hold", OPT_INST_HOLD, "PACKAGE[,PACKAGE]...", 0,
N_("Prevent packages listed from being upgraded if they are already installed."), 71 },
{"nohold", OPT_INST_NOHOLD, 0, 0,
N_("Don't take held packages from config nor $HOME/.poldek_hold."), 71 },

{"ignore", OPT_INST_IGNORE, "PACKAGE[,PACKAGE]...", 0,
N_("Make packages listed invisible."), 71 },
    
{"noignore", OPT_INST_NOIGNORE, NULL, 0,
N_("Make invisibled packages visible."), 71 },

{"greedy", OPT_INST_GREEDY, 0, 0,
 N_("Automatically upgrade packages which dependencies are broken "
    "by unistalled ones"), 71 }, 
    
{"dump", OPT_INST_MKSCRIPT, "FILE", OPTION_ARG_OPTIONAL,
     N_("Just dump install marked package filenames to FILE (default stdout)"), 71 },

{"dumpn", OPT_INST_POLDEK_MKSCRIPT, "FILE", OPTION_ARG_OPTIONAL,
     N_("Just dump install marked package names to FILE (default stdout)"), 71 },

{"fresh", OPT_INST_FRESHEN, 0, 0, 
     N_("Upgrade packages, but only if an earlier version currently exists"), 71 },

{"nofollow", OPT_INST_NOFOLLOW, 0, 0, 
     N_("Don't automatically install packages required by installed ones"), 71 },    
    
{"fetch", OPT_INST_FETCH, "DIR", OPTION_ARG_OPTIONAL,
     N_("Do not install, only fetch packages"), 71 }, 
    
{"nodeps", OPT_INST_NODEPS, 0, 0,
     N_("Install packages with broken dependencies"), 71 },
    
{"force", OPT_INST_FORCE, 0, 0,
     N_("Be unconcerned"), 71 },
    
{"justdb", OPT_INST_JUSTDB, 0, 0,
     N_("Modify only the database"), 71 },
    
{"rpmdef", OPT_INST_RPMDEF, "RPMMACRO", 0,
     N_("Set up rpm macro (only simple definitions)"), 71 },
    
{"test", 't', 0, 0,
 N_("Don't install, but tell if it would work or not"), 71 },
    
{"mkdir", OPT_INST_MKDBDIR, 0, 0, 
     N_("make %{_dbpath} if not exists"), 71 },

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
     N_("Take package priorities from FILE"), 71 },
    
{"split-out", OPT_SPLITOUTPATH, "PREFIX", 0,
     N_("Write chunks to PREFIX.XX, default PREFIX is packages.chunk"), 90 },    

{0,0,0,0, N_("Other:"), 500},
{"cachedir", OPT_SOURCECACHE, "DIR", 0, 
 N_("Store downloaded files under DIR"), 500 },

{"ask", OPT_ASK, 0, 0, N_("Confirm packages installation and "
                          "let user choose among equivalent packages"), 500 },
{"noask", OPT_NOASK, 0, 0, N_("Don't ask about anything"), 500 }, 

{"conf", OPT_CONF, "FILE", 0, N_("Read configuration from FILE"), 500 }, 
{"noconf", OPT_NOCONF, 0, 0, N_("Do not read configuration"), 500 }, 

{"version", OPT_BANNER, 0, 0, N_("Display program version information and exit"), 500 },    
{"log", OPT_LOG, "FILE", 0, N_("Log program messages to FILE"), 500 },
{"v016", OPT_V016, 0, 0, N_("Read indexes created by versions < 0.17"), 500 },
{0,  'v', 0, 0, N_("Be verbose."), 500 },
{0,  'q', 0, 0, N_("Do not produce any output."), 500 },
{ 0, 0, 0, 0, 0, 0 },
};


void check_mjrmode(struct args *argsp) 
{
    if (argsp->mjrmode) {
        logn(LOGERR, _("only one major mode may be specified; "
                       "available modes are: mkidx*,\n"
                       "update*, verify*, install*, reinstall, "
                       "upgrade*, split, sl and shell."));
        exit(EXIT_FAILURE);
    }
}

static void print_source_list(tn_array *sources);

/* buggy glibc argp... */
static inline void chkarg(int key, char *arg) 
{
    n_assert(key);
    if (*arg == '-') {
        int n = 0;
        while (1) {
            struct argp_option *opt = &options[n++];
            if (opt->name == NULL && opt->key == 0 && opt->doc == NULL)
                break;
            
            if (key == opt->key) {
                char skey[2] = { key, '\0' };
                logn(LOGERR, _("option requires an argument -- %s"),
                     isascii(key) ? skey : opt->name);
                exit(EXIT_FAILURE);
            }
            
        }
        exit(EXIT_FAILURE);
    }
}

static char *prepare_path(char *pathname) 
{
    char path[PATH_MAX];
    const char *ppath;

    
    pathname = trimslash(pathname);

    if (pathname == NULL)
        return pathname;
        
    if ((ppath = abs_path(path, sizeof(path), pathname)))
        return n_strdup(ppath);
    
    return pathname;
}


/* Parse a single option. */
static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct args *argsp = state->input;
    struct source *src = NULL;
    int source_type = PKGSRCT_NIL;

    if (key && arg)
        chkarg(key, arg);
    
    switch (key) {
        case OPT_V016:
            argsp->switches |= OPT_SW_V016;
            argsp->update_op = OPT_UPDATEIDX_WHOLE;
            break;

        case OPT_LOG:
            argsp->log_path = arg;
            break;
            
        case 'q':
            verbose = -1;
            break;
            
        case 'v': 
            verbose++;
            break;

        case OPT_BANNER:
            printf("%s\n", poldek_banner);
            exit(EXIT_SUCCESS);
            break;

        case OPT_NEVR:
            n_array_push(argsp->pkgdef_defs, arg);
            break;

        case OPT_PKGSET:
            n_array_push(argsp->pkgdef_sets, arg);
            break;

        case 'n':
            n_array_push(argsp->source_names, arg);
            break;

        case 'l':
            if (argsp->mjrmode != MODE_SRCLIST)
                check_mjrmode(argsp);
            argsp->mjrmode = MODE_SRCLIST;
            break;
            
        case OPT_SOURCETXT:     /* no break */
            source_type = PKGSRCT_IDX;
            
        case OPT_SOURCEDIR:     /* no break */
            if (source_type == PKGSRCT_NIL)
                source_type = PKGSRCT_DIR;

        case OPT_SOURCEHDL:     /* no break */
            if (source_type == PKGSRCT_NIL)
                source_type = PKGSRCT_HDL;
            
        case 's':
            if (argsp->curr_src_path) { /* no prefix for curr_src_path */
                src = source_new(argsp->curr_src_path, NULL);
                src->type = argsp->curr_src_type;
                n_array_push(argsp->sources, src);
            }
            
            argsp->curr_src_path = prepare_path(arg);
            argsp->curr_src_type = source_type;
            break;

        case 'P':
            if (argsp->curr_src_path == NULL) {
                logn(LOGERR, _("prefix option should be preceded by source one"));
                exit(EXIT_FAILURE);
            }
            
            if (argsp->curr_src_type == PKGSRCT_DIR) {
                logn(LOGERR, _("prefix for directory source makes no sense"));
                exit(EXIT_FAILURE);
            }
            
            src = source_new(argsp->curr_src_path, trimslash(arg));
            src->type = argsp->curr_src_type;
            n_array_push(argsp->sources, src);
            argsp->curr_src_path = NULL;
            argsp->curr_src_type = PKGSRCT_NIL;
            break;

            
        case OPT_UPDATEIDX:
            argsp->mnrmode |= MODE_MNR_UPDATEIDX;
            break;

        case OPT_UPDATEIDX_WHOLE:
            argsp->mnrmode |= MODE_MNR_UPDATEIDX;
            argsp->update_op = OPT_UPDATEIDX_WHOLE;
            break;

        case OPT_CLEANIDX:
            argsp->mnrmode |= MODE_MNR_CLEANIDX;
            break;

        case OPT_CLEANIDX_WHOLE:
            argsp->mnrmode |= MODE_MNR_CLEANIDX;
            argsp->clean_whole = 1;
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
            argsp->inst.flags |= INSTS_UPGRADE;
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
            argsp->idx_path = prepare_path(arg);
            argsp->idx_type = INDEXTYPE_TXT;
            break;

        case OPT_MKIDXZ:
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_MKIDX;
            argsp->psflags |= PSMODE_MKIDX;
            argsp->idx_path = prepare_path(arg);
            argsp->idx_type = INDEXTYPE_TXTZ;
            break;
            
        case OPT_NODESC:
	    argsp->pkgdir_creat_flags |= PKGDIR_CREAT_NODESC;
	    break;

        case OPT_NODIFF:
	    argsp->pkgdir_nodiff = 1;
	    break;

        case OPT_UNINSTALL:
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_UNINSTALL;
            break;
            
        case OPT_INST_INSTDIST:
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_INSTALLDIST;
            argsp->inst.rootdir = prepare_path(arg);
            argsp->psflags |= PSMODE_INSTALL | PSMODE_INSTALL_DIST;
            argsp->inst.flags |= INSTS_INSTALL;
            break;
            
        case OPT_INST_UPGRDIST:
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_UPGRADEDIST;
            if (arg) 
                argsp->inst.rootdir = prepare_path(arg);
            else if (argsp->inst.rootdir == NULL)
                argsp->inst.rootdir = "/";
            
            argsp->psflags |= PSMODE_UPGRADE | PSMODE_UPGRADE_DIST;
            argsp->inst.flags |= INSTS_UPGRADE;
            break;

        case OPT_INST_HOLD:
            if (strchr(arg, ',') == NULL) {
                n_array_push(argsp->inst.hold_patterns, n_strdup(arg));
                
            } else {
                const char **pkgs, **p;
            
                p = pkgs = n_str_tokl(arg, ",");
                while (*p) {
                    n_array_push(argsp->inst.hold_patterns, n_strdup(*p));
                    p++;
                }
                n_str_tokl_free(pkgs);
            }
            break;
            
        case OPT_INST_NOHOLD:
            argsp->inst.flags |= INSTS_NOHOLD;
            break;


        case OPT_INST_IGNORE:
            if (strchr(arg, ',') == NULL) {
                n_array_push(argsp->inst.ign_patterns, n_strdup(arg));
                
            } else {
                const char **pkgs, **p;
            
                p = pkgs = n_str_tokl(arg, ",");
                while (*p) {
                    n_array_push(argsp->inst.ign_patterns, n_strdup(*p));
                    p++;
                }
                n_str_tokl_free(pkgs);
            }
            break;

        case OPT_INST_NOIGNORE:
            argsp->inst.flags |= INSTS_NOIGNORE;
            break;

        case OPT_INST_GREEDY:
            argsp->inst.flags |= INSTS_GREEDY;
            break;
            
        case 'i':
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_INSTALL;
            argsp->psflags |= PSMODE_INSTALL;
            argsp->inst.flags |= INSTS_INSTALL;
            break;

            
        case OPT_INST_DOWNGRADE:
            argsp->inst.flags |= INSTS_DOWNGRADE;
                                /* no break */
            
        case OPT_INST_REINSTALL:
            if ((argsp->inst.flags & INSTS_DOWNGRADE) == 0)
                argsp->inst.flags |= INSTS_REINSTALL;
                                /* no break */
        case 'U':
        case 'u':
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_UPGRADE;
            argsp->psflags |= PSMODE_UPGRADE;
            argsp->inst.flags |= INSTS_UPGRADE;
            break;

        case 'r':
            argsp->inst.rootdir = trimslash(arg);
            break;
            
        case OPT_INST_RPMDEF:
            n_assert(argsp->inst.rpmacros);
            n_array_push(argsp->inst.rpmacros, arg);
            break;
            
            
        case OPT_INST_FETCH:
            if (arg)
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
            argsp->inst.flags  |= INSTS_NODEPS;
            break;

        case OPT_INST_FORCE:
            argsp->inst.flags |= INSTS_FORCE;
            break;
            
        case OPT_INST_JUSTDB:
            argsp->inst.flags |= INSTS_JUSTDB;
            break;

        case 't':
            if (argsp->inst.flags & INSTS_TEST)
                argsp->inst.flags |= INSTS_RPMTEST;
            else
                argsp->inst.flags |= INSTS_TEST;
            break;

        case OPT_INST_MKDBDIR:
            argsp->inst.flags |= INSTS_MKDBDIR;
            break;

        case OPT_ASK:
            argsp->inst.flags |= (INSTS_CONFIRM_INST | INSTS_EQPKG_ASKUSER);
            break;

        case OPT_NOASK:
            argsp->switches |= OPT_SW_NOASK;
            argsp->inst.flags &= ~(INSTS_CONFIRM_INST | INSTS_CONFIRM_UNINST |
                                   INSTS_EQPKG_ASKUSER);
            break;
            
        case OPT_CONF:
            argsp->conf_path = prepare_path(arg);
            break;
            
        case OPT_NOCONF:
            argsp->switches |= OPT_SW_NOCONF;
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

static void n_malloc_fault(void) 
{
    printf("Something wrong, something not quite right...\n"
           "Memory exhausted\n");
    exit(EXIT_FAILURE);
}


static void n_assert_hook(const char *expr, const char *file, int line) 
{
    printf("Something wrong, something not quite right.\n"
           "Assertion '%s' failed, %s:%d\n"
           "Please report this bug to %s.\n\n",
           expr, file, line, argp_program_bug_address);
    abort();
}

#undef POLDEK_MEM_DEBUG
#if defined HAVE_MALLOC_H && defined POLDEK_MEM_DEBUG
#include <malloc.h>

void *old_malloc_hook = NULL;

void *Fnn(size_t SIZE, const void *CALLER) 
{
    void *p, *v;
    __malloc_hook = old_malloc_hook;
    
    v = n_malloc(SIZE);
    //printf("malloc %d\n", SIZE);
    __malloc_hook = Fnn;
    return v;
}
#endif /* POLDEK_MEM_DEBUG */
     
void poldek_init(void) 
{
#ifdef HAVE_MALLOPT
# include <malloc.h>

#if defined HAVE_MALLOC_H && defined POLDEK_MEM_DEBUG
    old_malloc_hook = __malloc_hook;
    __malloc_hook = Fnn;
#endif
    mallopt(M_MMAP_THRESHOLD, 1024);
    //mallopt(M_MMAP_MAX, 0);
    
#endif /* HAVE_MALLOPT */
    
    n_assert_sethook(n_assert_hook);
    n_malloc_set_failhook(n_malloc_fault);
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

void poldek_reinit(void)
{
    pkgsetmodule_destroy();
    pkgflmodule_destroy();

    poldek_init();
}


static
int addsource(tn_array *sources, struct source *src,
              tn_array *src_names, int *matches) 
{
    int rc = 0;
    
    if (n_array_size(src_names) == 0) {
        n_array_push(sources, src);
        rc = 1;
                
    } else {
        int i;
        
        for (i=0; i<n_array_size(src_names); i++) {
            char *sn = n_array_nth(src_names, i);
            
            if (fnmatch(sn, src->name, 0) == 0) {
                /* given by name -> clear flags */
                src->flags &= ~(PKGSOURCE_NOAUTO | PKGSOURCE_NOAUTOUP);
                n_array_push(sources, src);
                matches[i]++;
                rc = 1;
                break;
            }
        }
    }
    
    return rc;
}

        
static
int get_conf_sources(tn_array *sources, tn_array *src_names, tn_hash *htcnf)
{
    struct source   *src;
    int             i, nerr;
    int             *matches = NULL, is_multi = 0;
    char            *v;
    

    if (n_array_size(src_names) > 0) {
        matches = alloca(n_array_size(src_names) * sizeof(int));
        memset(matches, 0, n_array_size(src_names) * sizeof(int));
    }
    
    if ((v = conf_get(htcnf, "source", &is_multi))) {
        if (is_multi == 0) {
            src = source_new(v, NULL);
            if (!addsource(sources, src, src_names, matches))
                source_free(src);
            
        } else {
            tn_array *paths = NULL;
            if ((paths = conf_get_multi(htcnf, "source"))) {
                while (n_array_size(paths)) {
                    src = source_new(n_array_shift(paths), NULL);
                    if (!addsource(sources, src, src_names, matches))
                        source_free(src);
                }
            }
        }
    }
    
    /* source\d+, prefix\d+ pairs  */
    for (i=0; i < 100; i++) {
        char opt[64], *src_val;
        
        snprintf(opt, sizeof(opt), "source%d", i);
        if ((src_val = conf_get(htcnf, opt, NULL))) {
            snprintf(opt, sizeof(opt), "prefix%d", i);
            src = source_new(src_val, conf_get(htcnf, opt, NULL));
            
            if (!addsource(sources, src, src_names, matches))
                source_free(src);
        }
    }


    nerr = 0;
    for (i=0; i < n_array_size(src_names); i++) {
        if (matches[i] == 0) {
            logn(LOGERR, _("%s: no such source"),
                 (char*)n_array_nth(src_names, i));
            nerr++;
        }
    }

    return nerr == 0;
}


static void get_conf_opt_list(const char *name, tn_array *tolist) 
{
    int is_multi = 0;
    char *v;
    
    if ((v = conf_get(htcnf, name, &is_multi))) {
        tn_array *list = NULL;
        
        if (is_multi) {
            list = conf_get_multi(htcnf, name);
            while (n_array_size(list)) 
                n_array_push(tolist, n_array_shift(list));
            
        } else {
            n_array_push(tolist, v);
        }
    }
}


static
void parse_options(int argc, char **argv) 
{
    struct argp argp = { options, parse_opt, args_doc, poldek_banner, 0, 0, 0};
    int vfile_cnflags = 0;
    char *v;


    verbose = 0;
    
    memset(&args, 0, sizeof(args));

    args.sources = n_array_new(4, (tn_fn_free)source_free, (tn_fn_cmp)source_cmp);
    args.source_names = n_array_new(4, NULL, (tn_fn_cmp)strcmp);
    args.curr_src_path = NULL;
    args.curr_src_type = PKGSRCT_NIL;
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

    pkgdir_v016compat = (args.switches & OPT_SW_V016);

    
    if ((args.switches & OPT_SW_NOCONF) && args.conf_path) {
        logn(LOGERR, _("--noconf and --conf are exclusive, aren't they?"));
        exit(EXIT_FAILURE);
    }

    if (args.curr_src_path) { 
        struct source *src = source_new(args.curr_src_path, NULL);
        src->type = args.curr_src_type;
        n_array_push(args.sources, src);
    }
    
#if 0
    if (n_array_size(args.sources) && n_array_size(args.source_names)) {
        logn(LOGERR, _("--source and --sn are exclusive"));
        exit(EXIT_FAILURE);
    }
#endif
    
    if (args.conf_path != NULL)
        htcnf = ldconf(args.conf_path);
    else if (args.noconf == 0)
        htcnf = ldconf_deafult();

    
    if (args.mjrmode == MODE_SRCLIST)
        n_array_clean(args.source_names);
    else {
        n_array_sort(args.source_names);
        n_array_uniq(args.source_names);
    }

    
    if (n_array_size(args.sources) == 0 || n_array_size(args.source_names) > 0) {
        if (!get_conf_sources(args.sources, args.source_names, htcnf))
            exit(EXIT_FAILURE);
    }
    

    if (n_array_size(args.sources) == 0) {
        logn(LOGERR, _("no source specified"));
        exit(EXIT_FAILURE);
    }
    
    if (args.mjrmode == MODE_SRCLIST) {
        print_source_list(args.sources);
        exit(EXIT_SUCCESS);
        
    } else {
        int i, nsources = 0;

        n_array_sort(args.sources);
        n_array_uniq(args.sources);
        
        for (i=0; i < n_array_size(args.sources); i++) {
            struct source *src = n_array_nth(args.sources, i);
            
            if (args.mnrmode & MODE_MNR_UPDATEIDX) {
                if ((src->flags & PKGSOURCE_NOAUTOUP) == 0)
                    nsources++;
                
            } else if ((src->flags & PKGSOURCE_NOAUTO) == 0)
                nsources++;
        }
        
        if (nsources == 0) {
            logn(LOGERR, _("nothing to do (no source selected?)"));
            exit(EXIT_FAILURE);
        }
    }
    
    if (args.mjrmode == MODE_NULL && args.mnrmode == MODE_NULL) {
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
    

    if (conf_get_bool(htcnf, "use_sudo", 0))
        args.inst.flags |= INSTS_USESUDO;

    if (conf_get_bool(htcnf, "mercy", 0))
        args.psflags |= PSVERIFY_MERCY;

    if (conf_get_bool(htcnf, "keep_downloads", 0))
        args.inst.flags |= INSTS_KEEP_DOWNLOADS;
    
    if (conf_get_bool(htcnf, "confirm_installation", 0))
        args.inst.flags |= INSTS_CONFIRM_INST;
    /* backward compat */
    if (conf_get_bool(htcnf, "confirm_installs", 0))
        args.inst.flags |= INSTS_CONFIRM_INST;

    if (conf_get_bool(htcnf, "confirm_removal", 1))
        args.inst.flags |= INSTS_CONFIRM_UNINST;

    if (conf_get_bool(htcnf, "choose_equivalents_manually", 0))
        args.inst.flags |= INSTS_EQPKG_ASKUSER;

    if (args.switches & OPT_SW_NOASK)
        args.inst.flags &= ~(INSTS_CONFIRM_INST | INSTS_CONFIRM_UNINST |
                             INSTS_EQPKG_ASKUSER);
    
    else if ((args.inst.flags & INSTS_CONFIRM_INST) && verbose < 1)
        verbose = 1;

    if (conf_get_bool(htcnf, "particle_install", 1))
        args.inst.flags |= INSTS_PARTICLE;

    
    if ((args.inst.flags & INSTS_GREEDY) == 0) { /* no --greedy specified */
        if (conf_get_bool(htcnf, "greedy", 0))
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
    
    get_conf_opt_list("rpmdef", args.inst.rpmacros);
    get_conf_opt_list("hold", args.inst.hold_patterns);
    get_conf_opt_list("ignore", args.inst.ign_patterns);
    
    vfile_verbose = &verbose;
    n_assert(args.inst.cachedir);

    if ((conf_get_bool(htcnf, "ftp_sysuser_as_anon_passwd", 0)))
        vfile_cnflags |= VFILE_REALUSERHOST_AS_ANONPASSWD;
    
    vfile_configure(args.inst.cachedir, vfile_cnflags);
    
    if (args.inst.rootdir) {
        char path[PATH_MAX];
        const char *ppath;
        
        if ((ppath = abs_path(path, sizeof(path), args.inst.rootdir)))
            args.inst.rootdir = n_strdup(ppath);
    }
    
    vfile_msg_fn = log_msg;
    vfile_msgtty_fn = log_tty;
    vfile_err_fn = log_err;
}


static void print_source_list(tn_array *sources) 
{
    int i;

    n_array_sort_ex(sources, (tn_fn_cmp)source_cmp_pri_name);
    for (i=0; i<n_array_size(sources); i++)
        source_printf(n_array_nth(sources, i));
    n_array_sort(sources);
}

void load_db_depdirs(const char *rootdir, int mjrmode, struct pkgset *ps) 
{
    switch (mjrmode) {
#ifdef ENABLE_INTERACTIVE_MODE
        case MODE_SHELL:
#endif
            
        case MODE_INSTALL:
        case MODE_UNINSTALL:
        case MODE_UPGRADE:
        case MODE_UPGRADEDIST:
            if (rpm_get_dbdepdirs(rootdir, ps->depdirs) >= 0)
                ps->flags |= PSDBDIRS_LOADED;
            break;
            
    }
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

    if (ps == NULL)
        return ps;

    
    if ((args.mjrmode & MODE_IS_NOSCORE) == 0) {
        if ((args.inst.flags & INSTS_NOHOLD) == 0) {
            packages_score(ps->pkgs, args.inst.hold_patterns, PKG_HELD);
            
            if (n_array_size(args.inst.hold_patterns) == 0) {
                n_array_free(args.inst.hold_patterns);
                args.inst.hold_patterns = NULL;
            }
        }

        if ((args.inst.flags & INSTS_NOIGNORE) == 0) {
            packages_score(ps->pkgs, args.inst.ign_patterns, PKG_IGNORED);
            n_array_free(args.inst.ign_patterns);
            args.inst.ign_patterns = NULL;
        }
    }
        
    pkgset_setup(ps, args.split_conf.conf);
    
    return ps;
}


static int clean_idx(void)
{
    unsigned flags = PKGSOURCE_CLEAN;

    if (args.clean_whole > 0)
        flags |= PKGSOURCE_CLEANA;

    return sources_clean(args.sources, flags);
}


static int update_idx(void)
{
    unsigned flags = PKGSOURCE_UP;
    
    if (args.update_op == OPT_UPDATEIDX_WHOLE)
        flags |= PKGSOURCE_UPA;

    return sources_update(args.sources, flags);
}


static int make_idx(struct pkgset *ps) 
{
    struct source   *src;
    struct pkgdir   *pkgdir;
    char            path[PATH_MAX], *idx_path = NULL;
    int             nerr = 0;
    time_t          ts;
    unsigned        cr_flags = args.pkgdir_creat_flags;
    
    n_assert(ps);
    if (n_array_size(args.sources) > 1) {
        logn(LOGERR, _("multiple sources are not allowed for mkidx option"));
        return 0;
    }

    src = n_array_nth(args.sources, 0);
    
    if (strstr(src->path, "://")) {
        logn(LOGERR, _("mkidx requested for remote source without destination "
            "specified"));
        return 0;
    }

    trimslash(src->path);

    if (args.idx_path != NULL) {
        idx_path = args.idx_path;
        
    } else {
        switch (args.idx_type) {
            case INDEXTYPE_TXTZ:
                snprintf(path, sizeof(path), "%s/%s.gz", src->path, 
                         default_pkgidx_name);
                break;
                
            case INDEXTYPE_TXT:
                snprintf(path, sizeof(path), "%s/%s", src->path, 
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

    
    if (args.pkgdir_nodiff == 0 && access(idx_path, R_OK) == 0) {
        struct pkgdir *orig, *diff;
        
        msgn(1, _("Loading previous %s..."), vf_url_slim_s(idx_path, 0));
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

int is_package_file(const char *path)
{
    struct stat st;
    
    if (strstr(path, ".rpm") == 0)
        return 0;

    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

int prepare_given_packages(void) 
{
    int i, rc = 1;
    
    if (args.ups == NULL)
        args.ups = usrpkgset_new();

    for (i=0; i < n_array_size(args.pkgdef_sets); i++) {
        char *path = n_array_nth(args.pkgdef_sets, i);
        
        if (!usrpkgset_add_list(args.ups, path))
            rc = 0;
    }

    for (i=0; i < n_array_size(args.pkgdef_defs); i++) {
        char *str = n_array_nth(args.pkgdef_defs, i);

        if (!usrpkgset_add_str(args.ups, str, strlen(str)))
            rc = 0;
    }

    for (i=0; i<n_array_size(args.pkgdef_files); i++) {
        char *path = n_array_nth(args.pkgdef_files, i);

        if (is_package_file(path)) 
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
int verify_args(void) 
{
    int i, rc = 1;

#ifdef ENABLE_INTERACTIVE_MODE
    if (args.mjrmode == MODE_NULL && args.mnrmode == MODE_NULL) 
        args.mjrmode = MODE_SHELL; /* default is shell */
#endif    

    switch (args.mjrmode) {
        case MODE_NULL:
            if (args.mnrmode == MODE_NULL) {
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

            if (args.switches & OPT_SW_V016) {
                logn(LOGWARN, _("--v016 has no effect in this mode"));
                args.switches &= ~OPT_SW_V016;
            }

            n_assert(args.sources);

            if (n_array_size(args.sources) > 1) {
                logn(LOGERR, _("multiple sources are not allowed "
                               "for mkidx option"));
                exit(EXIT_FAILURE);
            }
            
            for (i=0; i<n_array_size(args.sources); i++) {
                struct source *src = n_array_nth(args.sources, i);
                src->type = PKGSRCT_DIR;
            }
            break;

            
        case MODE_INSTALLDIST:
            if (getuid() != 0) {
                logn(LOGERR, _("root privileges are required in install-dist mode"));
                exit(EXIT_FAILURE);
            }
                                /* no break */
        case MODE_INSTALL:
        case MODE_UPGRADE:
        case MODE_UNINSTALL:
            if (prepare_given_packages() > 0) {
                rc = check_install_flags();
                
            } else {
                logn(LOGERR, _("no packages to install"));
                rc = 0;
            }
                
            break;
            
        case MODE_UPGRADEDIST:
            if (args.has_pkgdef) {
                logn(LOGERR, _("this option upgrades whole system, not given packages"));
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
            logn(LOGERR, "unknown mode %d", args.mjrmode);
            n_assert(0);
            exit(EXIT_FAILURE);
    }
    return rc;
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


static
int uninstall(struct usrpkgset *ups, struct inst_s *inst) 
{
    int rc;

    verbose = 1;
    if (n_array_size(ups->pkgdefs) == 0) {
        logn(LOGERR, _("no packages specified"));
        exit(EXIT_FAILURE);
    }
    
    rc = uninstall_usrset(ups, inst, NULL);
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
#if 0
    if (uid == 0) {
        logn(LOGWARN, _("Running me as root is not a good habit"));
        sleep(1);
    }
#endif    
}


int main(int argc, char **argv)
{
    struct pkgset   *ps = NULL;
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

    
    if (args.log_path) {
        int i;
        
        log_init(args.log_path, stdout, logprefix);
        
        msg_f(0, "\n");
        msg_f(0, "Start (%s", argv[0]);
        for (i=1; i < argc; i++)
            msg_f(0, "_ %s", argv[i]);
        msg_f(0, "_)\n");
    }
    
    if (!mklock(args.inst.cachedir))
        exit(EXIT_FAILURE);
    
    if (!verify_args())
        exit(EXIT_FAILURE);

    poldek_init();
    rpm_initlib(args.inst.rpmacros);

    if (args.mnrmode & MODE_MNR_CLEANIDX) {
        rc = clean_idx();
        if (args.mjrmode == MODE_NULL)
            exit(rc ? EXIT_SUCCESS : EXIT_FAILURE);
    }
    
    if (args.mnrmode & MODE_MNR_UPDATEIDX) {
        int v = verbose;

        if (verbose == 0) 
            verbose = 1;
        
        if (!update_idx())
            exit(EXIT_FAILURE);

        if (args.mjrmode == MODE_NULL) 
            goto l_end;
        verbose = v;

        poldek_reinit();
    }

    if (args.mjrmode == MODE_UNINSTALL) {
        if ((rc = usrpkgset_size(args.ups)))
            rc = uninstall(args.ups, &args.inst);
        goto l_end;
    }
    
    if (args.mjrmode == MODE_VERIFY && args.has_pkgdef == 0 && verbose != -1)
        verbose += 2;

    ldflags = 0;
    if (args.mjrmode == MODE_MKIDX)
        ldflags = PKGDIR_LD_RAW;
    
    else if (args.mjrmode == MODE_VERIFY) 
        ldflags = PKGDIR_LD_VERIFY;

    if ((ps = load_pkgset(ldflags)) == NULL)
        exit(EXIT_FAILURE);

    
    switch (args.mjrmode) {
        case MODE_NULL:
            if (args.mnrmode != MODE_NULL)
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
            rc = make_idx(ps);
            break;
            
        case MODE_INSTALLDIST:
            if (args.has_pkgdef == 0)
                rc = 0;
            
            else if ((rc = usrpkgset_size(args.ups))) {
                rc = mark_usrset(ps, args.ups, &args.inst, args.mjrmode);
                if (rc || (args.inst.flags & INSTS_FORCE)) 
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

 l_end:
    if (ps)
        pkgset_free(ps);
    mem_info(1, "MEM at the end");
    poldek_destroy();
    mem_info(1, "MEM at the *real* end");

    return rc ? EXIT_SUCCESS : EXIT_FAILURE;
}
