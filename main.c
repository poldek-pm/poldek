/*
  Copyright (C) 2000 - 2002 Pawel A. Gajda <mis@pld.org.pl>

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
#include <trurl/nmalloc.h>

#include <vfile/vfile.h>
#include <sigint/sigint.h>

#include "i18n.h"
#include "log.h"
#include "pkgdir/source.h"
#include "pkgset.h"
#include "usrset.h"
#include "misc.h"
#include "conf.h"
#include "split.h"
#include "poldek.h"

#ifndef VERSION
# error "undefined VERSION"
#endif

#ifdef ENABLE_INTERACTIVE_MODEX
extern
int shell_main(struct pkgset *ps, struct inst_s *inst, int skip_installed);
extern
int shell_exec(struct pkgset *ps, struct inst_s *inst, int skip_installed,
               const char *cmd);
#endif

static const char *argp_program_version = poldek_VERSION_BANNER;
const char *argp_program_bug_address = poldek_BANNER;

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
    char  *prefix;
};

struct args {
    int       mjrmode;
    unsigned  mnrmode;

    unsigned  switches;
    int       update_op;
    int       clean_whole;
    
    char      *curr_src_path;
    char      *curr_src_type;

    struct source *src_mkidx;

    struct poldek_ctx *ctx;
    struct poldek_ts  *ts;
    
    char        *conf_path;
    char        *log_path;
    
    unsigned    pkgdir_creat_flags; 
    
    int         shell_skip_installed;
    char        *shcmd;

    struct      split_conf split_conf;
} args;

static struct poldek_ctx poldek_ctx;

#define OPT_VERIFY_MERCY      'm'
#define OPT_VERIFY_DEPS       'V'
#define OPT_VERIFY_CNFLS      902
#define OPT_VERIFY_FILECNFLS  903
#define OPT_VERIFY_ALL        904

#define OPT_MKIDX        1001
#define OPT_MKIDXZ       1002   /* obsoleted */
#define OPT_NODESC	 1004 /* don't put descriptions in package index */
#define OPT_NODIFF	 1005 /* don't create diff */
#define OPT_NOCOMPR	 1006 /* create uncompressed index */
#define OPT_EMPTYOK	 1007 /* don't create diff */

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
#define OPT_INST_UNIQNAMES        'Q'

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

{"mkidxz", OPT_MKIDXZ, "FILE", OPTION_ARG_OPTIONAL | OPTION_HIDDEN,
 N_("Likewise, but gzipped file is created"), 60},

{"nodesc", OPT_NODESC, 0, 0,
 N_("Don't put package user-level information (like Summary or Description)"
     " into created index."), 60 },

{"nodiff", OPT_NODIFF, 0, 0,
 N_("Don't create \"patch\""), 60 },

{"nocompress", OPT_NOCOMPR, 0, OPTION_HIDDEN,
 N_("Create uncompressed index"), 60 },

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

{"unique-pkg-names", OPT_INST_UNIQNAMES, 0, 0, 
N_("Do sort | uniq on available package list"), 71 },        

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
//{"v016", OPT_V016, 0, 0, N_("Read indexes created by versions < 0.17"), 500 },
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

/* Parse a single option. */
static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct args *argsp = state->input;
    static struct source *src = NULL;
    char *source_type = NULL;
    int source_type_isset = 0;
        
    if (key && arg)
        chkarg(key, arg);
    
    switch (key) {
        case OPT_V016:
            argsp->switches |= OPT_SW_V016;
            argsp->update_op = OPT_UPDATEIDX_WHOLE;
            break;

        case OPT_LOG:
            poldek_configure(argsp->ctx, POLDEK_CONF_LOGFILE, arg);
            break;
            
        case 'q':
            verbose = -1;
            break;
            
        case 'v': 
            verbose++;
            break;

        case OPT_BANNER:
            printf("%s\n", poldek_BANNER);
            exit(EXIT_SUCCESS);
            break;

        case OPT_NEVR:
            n_array_push(argsp->pkgdef_defs, arg);
            break;

        case OPT_PKGSET:
            n_array_push(argsp->pkgdef_sets, arg);
            break;
            
        case 'l':
            if (argsp->mjrmode != MODE_SRCLIST)
                check_mjrmode(argsp);
            argsp->mjrmode = MODE_SRCLIST;
            break;

        case 'n':
            src = source_malloc();
            src->name = n_strdup(arg);
            src->flags |= PKGSOURCE_NAMED;
            poldek_configure(argsp->ctx, POLDEK_CONF_SOURCE, src);
            break;
            
        case OPT_SOURCETXT:     /* no break */
            source_type = NULL; /* guess */
            source_type_isset = 1;
            
        case OPT_SOURCEDIR:     /* no break */
            if (source_type_isset == 0)
                source_type = n_strdup("dir");
            
        case OPT_SOURCEHDL:     /* no break */
            if (source_type_isset == 0)
                source_type = n_strdup("hdrl");

        case 's':
            argsp->curr_src_path = arg;
            argsp->curr_src_type = source_type;
            
            src = source_new(source_type, arg, NULL);
			poldek_configure(argsp->ctx, POLDEK_CONF_SOURCE, src);
            break;

        case 'P':
            if (argsp->curr_src_path == NULL) {
                logn(LOGERR, _("prefix option should be preceded by source one"));
                exit(EXIT_FAILURE);
                
            } else if (strcmp(argsp->curr_src_type, "dir") == 0) {
                logn(LOGERR, _("prefix for directory source makes no sense"));
                exit(EXIT_FAILURE);
                
            } else {
                if (src->flags & PKGSOURCE_NAMED)
                    logn(LOGERR | LOGDIE, _("poldek's panic"));
                
                if (!source_set_pkg_prefix(src, trimslash(arg)))
                    exit(EXIT_FAILURE);
                
                argsp->curr_src_path = NULL;
                argsp->curr_src_type = NULL;
            }
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
            poldek_configure(argsp->ctx, POLDEK_CONF_CACHEDIR, arg);
            break;

        case OPT_VERIFY_MERCY:
            poldek_configure(argsp->ctx, POLDEK_CONF_PSFLAGS,
                             (unsigned)PSVERIFY_MERCY);
            break;

            
        case OPT_VERIFY_DEPS:
            poldek_configure(argsp->ctx, POLDEK_CONF_PS_SETUP_FLAGS,
                             (unsigned)PSET_VERIFY_DEPS);
            if (argsp->mjrmode != MODE_VERIFY)
                check_mjrmode(argsp);
            argsp->mjrmode = MODE_VERIFY;
            break;

        case OPT_VERIFY_CNFLS:
            poldek_configure(argsp->ctx, POLDEK_CONF_PS_SETUP_FLAGS,
                             (unsigned)PSET_VERIFY_CNFLS);
            if (argsp->mjrmode != MODE_VERIFY)
                check_mjrmode(argsp);
            argsp->mjrmode = MODE_VERIFY;
            break;

        case OPT_VERIFY_FILECNFLS:
            poldek_configure(argsp->ctx, POLDEK_CONF_PS_SETUP_FLAGS,
                             (unsigned)PSET_VERIFY_FILECNFLS);
            if (argsp->mjrmode != MODE_VERIFY)
                check_mjrmode(argsp);
            argsp->mjrmode = MODE_VERIFY;
            break;

        case OPT_VERIFY_ALL:
            poldek_configure(argsp->ctx, POLDEK_CONF_PS_SETUP_FLAGS,
                             (unsigned)(PSET_VERIFY_DEPS | PSET_VERIFY_CNFLS |
                             PSET_VERIFY_FILECNFLS));
            
            if (argsp->mjrmode != MODE_VERIFY)
                check_mjrmode(argsp);
            argsp->mjrmode = MODE_VERIFY;
            break;
            
#ifdef ENABLE_INTERACTIVE_MODE
        case OPT_SHELLMODE:
            if (argsp->mjrmode != MODE_SHELL)
                check_mjrmode(argsp);
            argsp->mjrmode = MODE_SHELL;
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

        case OPT_MKIDXZ:
        case OPT_MKIDX:
            if (arg)
                argsp->src_mkidx = source_new(NULL, arg, NULL);
            break;
            
        case OPT_NODESC:
            argsp->pkgdir_creat_flags |= PKGDIR_CREAT_NODESC;
            break;

        case OPT_NODIFF:
            argsp->pkgdir_creat_flags |= PKGDIR_CREAT_NOPATCH;
            break;
            
        case OPT_NOCOMPR:
            argsp->pkgdir_creat_flags |= PKGDIR_CREAT_NOCOMPR;
            break;
            
        case OPT_UNINSTALL:
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_UNINSTALL;
            break;
            
        case OPT_INST_INSTDIST:
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_INSTALLDIST;
            poldek_configure(argsp->ctx, POLDEK_CONF_ROOTDIR, arg);
            poldek_configure_f(argsp->ctx, INSTS_INSTALL);
            break;
            
        case OPT_INST_UPGRDIST:
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_UPGRADEDIST;
            if (arg)
                poldek_configure(argsp->ctx, POLDEK_CONF_ROOTDIR, arg);
            poldek_configure_f(argsp->ctx, INSTS_UPGRADE);
            break;

        case OPT_INST_HOLD:
            poldek_configure(argsp->ctx, POLDEK_CONF_HOLD, arg);
            break;
            
        case OPT_INST_NOHOLD:
            poldek_configure_f(argsp->ctx, INSTS_NOHOLD);
            break;


        case OPT_INST_IGNORE:
            poldek_configure(argsp->ctx, POLDEK_CONF_IGNORE, arg);
            break;

        case OPT_INST_NOIGNORE:
            poldek_configure_f(argsp->ctx, INSTS_NOIGNORE);
            break;

        case OPT_INST_GREEDY:
            poldek_configure_f(argsp->ctx, INSTS_GREEDY);
            break;
            
        case 'i':
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_INSTALL;
            poldek_configure_f(argsp->ctx, INSTS_INSTALL);
            break;

        case OPT_INST_UNIQNAMES:
            poldek_configure(argsp->ctx, POLDEK_CONF_PS_SETUP_FLAGS,
                             PSET_DO_UNIQ_PKGNAME);
            break;
            
        case OPT_INST_DOWNGRADE:
        case OPT_INST_REINSTALL:
        case 'U':
        case 'u':
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_UPGRADE;

            poldek_configure_f(argsp->ctx, INSTS_UPGRADE);
            
            if (key == OPT_INST_DOWNGRADE)
                poldek_configure_f(argsp->ctx, INSTS_DOWNGRADE);
            else if (key == OPT_INST_REINSTALL)
                poldek_configure_f(argsp->ctx, INSTS_REINSTALL);
            break;

        case 'r':
            poldek_configure(argsp->ctx, POLDEK_CONF_ROOTDIR, arg);
            break;
#if 0
DUPA            
        case OPT_INST_RPMDEF:
            n_assert(argsp->inst.rpmacros);
            n_array_push(argsp->inst.rpmacros, arg);
            break;
#endif            
            
        case OPT_INST_FETCH:
            if (arg)
                poldek_configure(argsp->ctx, POLDEK_CONF_FETCHDIR, arg);
            poldek_configure_f(argsp->ctx, INSTS_JUSTFETCH);
            break;

        case OPT_INST_MKSCRIPT:
            if (arg)
                poldek_configure(argsp->ctx, POLDEK_CONF_DUMPFILE, arg);
            poldek_configure_f(argsp->ctx, INSTS_JUSTPRINT);
            break;

        case OPT_INST_POLDEK_MKSCRIPT:
            if (arg)
                poldek_configure(argsp->ctx, POLDEK_CONF_DUMPFILE, arg);
            poldek_configure_f(argsp->ctx, INSTS_JUSTPRINT_N);
            break;

        case OPT_INST_FRESHEN:
            poldek_configure_f(argsp->ctx, INSTS_FRESHEN);
            break;

        case OPT_INST_NOFOLLOW:
            poldek_configure_f_clr(argsp->ctx, INSTS_FOLLOW);
            break;
            
        case OPT_INST_NODEPS:
            poldek_configure_f(argsp->ctx, INSTS_NODEPS);
            break;

        case OPT_INST_FORCE:
            poldek_configure_f(argsp->ctx, INSTS_FORCE);
            break;
            
        case OPT_INST_JUSTDB:
            poldek_configure_f(argsp->ctx, INSTS_JUSTDB);
            break;

        case 't':
            if (poldek_configure_f_isset(argsp->ctx, INSTS_TEST))
                poldek_configure_f(argsp->ctx, INSTS_RPMTEST);
            else
                poldek_configure_f(argsp->ctx, INSTS_TEST);
            break;

        case OPT_INST_MKDBDIR:
            poldek_configure_f(argsp->ctx, INSTS_MKDBDIR);
            break;

        case OPT_ASK:
            poldek_configure_f(argsp->ctx, INSTS_CONFIRM_INST | INSTS_EQPKG_ASKUSER);
            break;

        case OPT_NOASK:
            argsp->switches |= OPT_SW_NOASK;
            poldek_configure_f_clr(argsp->ctx, INSTS_CONFIRM_INST |
                                   INSTS_CONFIRM_UNINST | INSTS_EQPKG_ASKUSER);
            break;
            
        case OPT_CONF:
            argsp->conf_path = arg;
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
            poldek_configure(argsp->ctx, POLDEK_CONF_PRIFILE, arg);
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
                
                //n_assert(argsp->inst.rpmopts != NULL);
                //n_array_push(argsp->inst.rpmopts, arg);
                
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


static
void parse_options(int argc, char **argv) 
{
    struct argp argp = { options, parse_opt, args_doc, poldek_BANNER, 0, 0, 0};

    verbose = 0;
    
    memset(&args, 0, sizeof(args));

    args.ctx = &poldek_ctx;
    args.curr_src_path = NULL;
    args.curr_src_type = NULL;
    args.src_mkidx = NULL;
    args.pkgdef_files = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    args.pkgdef_defs  = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    args.pkgdef_sets  = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    args.split_conf.size = 0;
    args.split_conf.first_free_space = 0;
    args.split_conf.prefix = NULL;
    args.shcmd = NULL;

    argp_parse(&argp, argc, argv, 0, 0, &args);

    if ((args.switches & OPT_SW_NOCONF) && args.conf_path) {
        logn(LOGERR, _("--noconf and --conf are exclusive, aren't they?"));
        exit(EXIT_FAILURE);
    }

    if ((args.switches & OPT_SW_NOCONF) == 0) 
        if (!poldek_load_config(args.ctx, args.conf_path))
            exit(EXIT_FAILURE);
            


    if (args.mjrmode == MODE_NULL && args.mnrmode == MODE_NULL) {
#ifdef ENABLE_INTERACTIVE_MODE
        args.mjrmode = MODE_SHELL;
#else         
        logn(LOGERR, _("so what?"));
        exit(EXIT_FAILURE);
#endif        
    }

    args.has_pkgdef = n_array_size(args.pkgdef_sets) +
        n_array_size(args.pkgdef_defs) +
        n_array_size(args.pkgdef_files);
    
    if (args.switches & OPT_SW_NOASK) 
        poldek_configure_f_clr(args.ctx, INSTS_INTERACTIVE_ON);
    
    else if (poldek_configure_f_isset(args.ctx, INSTS_CONFIRM_INST) && verbose < 1)
        verbose = 1;

}


static void print_source_list(tn_array *sources) 
{
    int i;

    n_array_sort_ex(sources, (tn_fn_cmp)source_cmp_pri_name);
    for (i=0; i < n_array_size(sources); i++)
        source_printf(n_array_nth(sources, i));
    n_array_sort(sources);
}


static int clean_idx(void)
{
    unsigned flags = PKGSOURCE_CLEAN;

    if (args.clean_whole > 0)
        flags |= PKGSOURCE_CLEANA;

    return sources_clean(args.ctx->sources, flags);
}


static int update_idx(void)
{
    unsigned flags = PKGSOURCE_UP;
    
    if (args.update_op == OPT_UPDATEIDX_WHOLE)
        flags |= PKGSOURCE_UPA;

    return sources_update(args.ctx->sources, flags);
}


static int make_idx(void) 
{
    struct source   *src;
    const char      *type = NULL, *path = NULL;
    int i, nerr = 0;

    if (n_array_size(args.ctx->sources) > 1 && args.src_mkidx) {
        logn(LOGERR, _("multiple sources not allowed if index path is specified"));
        return 0;
    }

    if (args.src_mkidx) {
        type = args.src_mkidx->type;
        path = args.src_mkidx->path;
    }
    
    if (type == NULL)
        type = PKGDIR_DEFAULT_TYPE;
    
    for (i=0; i < n_array_size(args.ctx->sources); i++) {
        src = n_array_nth(args.ctx->sources, i);
        if (src->type == NULL)
            source_set_type(src, "dir");
        msgn(0, "Preparing %s...", src->path);
        if (!source_make_idx(src, type, path, args.pkgdir_creat_flags))
            nerr++;
    }

    if (args.src_mkidx) {
        source_free(args.src_mkidx);
        args.src_mkidx = NULL;
    }
    
    return nerr == 0;
}


int prepare_given_packages(void) 
{
    int i, rc = 1;
    
    if (args.ups == NULL)
        args.ups = arg_packages_new();

    for (i=0; i < n_array_size(args.pkgdef_sets); i++) {
        char *path = n_array_nth(args.pkgdef_sets, i);
        
        if (!arg_packages_add_list(args.ups, path))
            rc = 0;
    }

    for (i=0; i < n_array_size(args.pkgdef_defs); i++) {
        char *str = n_array_nth(args.pkgdef_defs, i);

        if (!arg_packages_add_str(args.ups, str, strlen(str)))
            rc = 0;
    }

    for (i=0; i<n_array_size(args.pkgdef_files); i++) {
        char *path = n_array_nth(args.pkgdef_files, i);

        if (is_package_file(path)) 
            rc = arg_packages_add_pkgfile(args.ups, path);
        else
            rc = arg_packages_add_str(args.ups, path, strlen(path));
    }
    
    arg_packages_setup(args.ups);
    return arg_packages_size(args.ups);
}

static
int verify_args(void) 
{
    int rc = 1;

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

            n_assert(args.ctx->sources);

#if 0            
            if (n_array_size(args.ctx->sources) > 1) {
                logn(LOGERR, _("multiple sources are not allowed "
                               "for mkidx option"));
                exit(EXIT_FAILURE);
            }
#endif            
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
            if (prepare_given_packages() == 0) {
                logn(LOGERR, _("no packages to install"));
                rc = 0;
            }
            
            break;
            
        case MODE_UPGRADEDIST:
            if (args.has_pkgdef) {
                logn(LOGERR, _("this option upgrades whole system, not given packages"));
                exit(EXIT_FAILURE);
            }
            rc = 1;
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

int mark_usrset(struct arg_packages *ups, int mjrmode) 
{
    int rc;
    int with_deps = 0;
    
    if (mjrmode == MODE_VERIFY && verbose < 2 && verbose != -1) 
        verbose = 1;

    if (mjrmode == MODE_VERIFY || mjrmode == MODE_INSTALLDIST)
        with_deps = 1;
    
    if (n_array_size(ups->pkgdefs) == 0) {
        logn(LOGERR, _("no packages specified"));
        exit(EXIT_FAILURE);
    }

    rc = poldek_mark_usrset(&poldek_ctx, ups, with_deps);
    return rc;
}


static
int uninstall(struct arg_packages *ups) 
{
    int rc;

    verbose = 1;
    if (n_array_size(ups->pkgdefs) == 0) {
        logn(LOGERR, _("no packages specified"));
        exit(EXIT_FAILURE);
    }
    
    rc = uninstall_usrset(ups, poldek_ctx.inst, NULL);
    return rc;
}


int main(int argc, char **argv)
{
    int                i, rc = 1, load_dbdepdirs = 0;
    struct poldek_ctx  *ctx;

    mem_info_verbose = -1;

    poldek_init(&poldek_ctx, 0);
    ctx = &poldek_ctx;
    
    translate_argp_options(options);
    parse_options(argc, argv);

    msg_f(0, "\n");
    msg_f(0, "Start (%s", argv[0]);
    for (i = 1; i < argc; i++)
        msg_f(0, "_ %s", argv[i]);
    msg_f(0, "_)\n");

    if (!poldek_setup(ctx))
        exit(EXIT_FAILURE);
    
    if (args.mjrmode == MODE_SRCLIST) {
        print_source_list(ctx->sources);
        exit(EXIT_SUCCESS);
        
    } else {
        unsigned srcflags_excl = 0;
        
        if (args.mnrmode & MODE_MNR_UPDATEIDX) {
            srcflags_excl = PKGSOURCE_NOAUTOUP;
                
        } else
            srcflags_excl = PKGSOURCE_NOAUTO;
        
        if (poldek_selected_sources(args.ctx, srcflags_excl) == 0) {
            logn(LOGERR, _("nothing to do (no source selected?)"));
            exit(EXIT_FAILURE);
        }
    }

    
    load_dbdepdirs = 0;
    switch (args.mjrmode) {
#ifdef ENABLE_INTERACTIVE_MODE
        case MODE_SHELL:
#endif
        case MODE_INSTALL:
        case MODE_UNINSTALL:
        case MODE_UPGRADE:
        case MODE_UPGRADEDIST:
            load_dbdepdirs = 1;
            break;
    }
    
    
    if (!verify_args())
        exit(EXIT_FAILURE);

    
    
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
    }

    if (args.mjrmode == MODE_UNINSTALL) {
        if ((rc = arg_packages_size(args.ups)))
            rc = uninstall(args.ups);
        goto l_end;
    }
    
    if (args.mjrmode == MODE_VERIFY && args.has_pkgdef == 0 && verbose != -1)
        verbose += 2;

    if (args.mjrmode == MODE_MKIDX) {
        rc = make_idx();
        goto l_end;
    }
    
    if (!poldek_load_sources(ctx, load_dbdepdirs))
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
                rc = shell_exec(ctx->ps, ctx->inst, args.shell_skip_installed,
                                args.shcmd);
            else
                rc = shell_main(ctx->ps, ctx->inst, args.shell_skip_installed);
            
            
            break;
#endif            
        case MODE_VERIFY:
            if (args.has_pkgdef)
                if ((rc = arg_packages_size(args.ups)))
                    rc = mark_usrset(args.ups, args.mjrmode);
                    
            break;
            
        case MODE_MKIDX:
            break;
            
        case MODE_INSTALLDIST:
            if (args.has_pkgdef == 0)
                rc = 0;
            
            else if ((rc = arg_packages_size(args.ups))) {
                rc = mark_usrset(args.ups, args.mjrmode);
                if (rc || (poldek_configure_f_isset(ctx, INSTS_FORCE)))
                    rc = poldek_install_dist(ctx);
            }
            break;


        case MODE_INSTALL:
        case MODE_UPGRADE:
            if ((rc = arg_packages_size(args.ups))) {
                if ((rc = mark_usrset(args.ups, args.mjrmode))) 
                    rc = poldek_install(ctx, NULL);
            }
            break;
            
        case MODE_UPGRADEDIST:
            rc = poldek_upgrade_dist(ctx);
            break;

        case MODE_SPLIT:
            rc = packages_split(ctx->ps->pkgs,
                                args.split_conf.size,
                                args.split_conf.first_free_space, 
                                args.split_conf.prefix);
            break;

            
        default:
            n_assert(0);
            exit(EXIT_FAILURE);
    }

 l_end:
    
    mem_info(1, "MEM at the end");
    poldek_destroy(ctx);
    mem_info(1, "MEM at the *real* end");
    return rc ? EXIT_SUCCESS : EXIT_FAILURE;
}
