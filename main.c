/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
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

#include <stdlib.h>
#include <string.h>

#include <argp.h>
#include <sys/types.h>
#include <sys/stat.h> 
#include <unistd.h>

#include <trurl/narray.h>
#include <trurl/nassert.h>
#include <vfile/vfile.h>

#include "log.h"
#include "pkgset.h"
#include "usrset.h"
#include "misc.h"
#include "rpm.h"
#include "install.h"
#include "conf.h"

#ifndef VERSION
# error "undefined VERSION"
#endif

#ifdef ENABLE_INTERACTIVE_MODE
extern int shell_main(struct pkgset *ps, struct inst_s *inst);
#endif

static const char *argp_program_version = "poldek " VERSION " (ALPHA)";
static const char *argp_program_bug_address = "<mis@pld.org.pl>";
/* Program documentation. */
static char doc[] = "poldek " VERSION " (ALPHA)\n"
"This program may be freely redistributed under the terms of the GNU GPL\n";
/* A description of the arguments we accept. */
static char args_doc[] = "[PACKAGE..] [--rpm-RPM_LONG_OPTION...]";


#define MODE_VERIFY       1
#define MODE_MKIDX        2
#define MODE_INSTALLDIST  3
#define MODE_INSTALL      4
#define MODE_UPGRADEDIST  5
#define MODE_UPGRADE      6
#define MODE_UPDATEIDX    7

#ifdef ENABLE_INTERACTIVE_MODE
# define MODE_SHELL       8
#endif

#define INDEXTYPE_TXT     1
#define INDEXTYPE_TXTZ    2
#define INDEXTYPE_RPMH    3 

struct args {
    int       mjrmode;
    char      *source_path;
    char      *pkg_prefix;
    int       ldmethod;
    
    int       idx_type;
    char      *idx_path;

    int       has_pkgdef;
    tn_array  *pkgdef_files;    /* A.rpm      */
    tn_array  *pkgdef_defs;     /* -n "A 1.2" */
    tn_array  *pkgdef_sets;     /* -p ftp://ftp.zenek.net/PLD/tiny */
    
    unsigned  psflags;
    unsigned  instflags;
    unsigned  inst_sflags;
    int       verify;
    char      *fetchdir;
    char      *dumpfile;
    char      *install_root;
    
    struct usrpkgset *ups;
    tn_array  *rpmopts;
    tn_array  *rpmacros;

    char      *conf_path;
    int       noconf;
    char      *cachedir;
    
    int       nodesc;		/* don't put descriptions in Packges */
} args;

tn_hash *htcnf = NULL;          /* config file values */



#define OPT_MKTXTINDEX   1001
#define OPT_MKTXTINDEXZ  1002
#define OPT_MKRPMINDEX   1003
#define OPT_NODESC	 1004

#define OPT_SOURCETXT   1015
#define OPT_SOURCEDIR   1016
#define OPT_SOURCEHDR   1017
#define OPT_PKGPREFIX   1018
#define OPT_SOURCEUP    1019
#define OPT_SOURCECACHE 1020

#ifdef ENABLE_INTERACTIVE_MODE
# define OPT_SHELLMODE   1021
#endif

#define OPT_INST_INSTDIST  1041
#define OPT_INST_UPGRDIST  1042
#define OPT_INST_NODEPS    1043
#define OPT_INST_FORCE     1044
#define OPT_INST_JUSTDB    1045
#define OPT_INST_TEST      1046
#define OPT_INST_MKDBDIR   1047
#define OPT_INST_KILLDB    1048
#define OPT_INST_RPMDEF    1049
#define OPT_INST_FETCH     1050
#define OPT_INST_MKSCRIPT  1051


/* The options we understand. */
static struct argp_option options[] = {

{0,0,0,0, "Source options:", 1 },    
{"source", 's', "SOURCE", 0, "Get packages info from SOURCE (guess type)", 1 },
    
{"sidx", OPT_SOURCETXT, "FILE", 0,
 "Get packages info from package index file FILE", 1 },

{"sdir", OPT_SOURCEDIR, "DIR", 0,
 "Get packages info from directory DIR", 1 },

{"shdr", OPT_SOURCEHDR, "FILE", OPTION_HIDDEN,
 "Get packages info from header index FILE", 1 },

{"prefix", 'P', "PREFIX", 0,
 "Get packages from PREFIX instead of SOURCE", 1 },

{"update", OPT_SOURCEUP, 0, 0, 
 "Update package index (for remote indexes)", 1 },

{"cachedir", OPT_SOURCECACHE, "DIR", 0, 
 "Store fetched packages and indexes under DIR (default is /tmp or if set, $TMPDIR)", 1 },    
  
{0,0,0,0, "Verify options:", 50 },        
{"verify",  'V', 0, 0, "Verify package set", 50 },
{"mercy",   'm', 0, 0, "Be tolerant for bugs which RPM tolerates", 50 },


{0,0,0,0, "Indexes creation:", 60},
{"mkidx", OPT_MKTXTINDEX, "FILE", OPTION_ARG_OPTIONAL,
 "Create package index, SOURCE/Packages by default", 60},

{"mkidxz", OPT_MKTXTINDEXZ, "FILE", OPTION_ARG_OPTIONAL,
 "Like above, but gzipped file is created", 60},

{"mkrpmidx", OPT_MKRPMINDEX, "FILE", OPTION_ARG_OPTIONAL | OPTION_HIDDEN,
 "Create header index, SOURCE/Packages-hdrs by default", 60},
    
{"nodesc", OPT_NODESC, 0, 0,
 "Don't put packages user-level information (like Summary or Description) in created index.", 60 },
    

{0,0,0,0, "Installation:", 70},
{"pkgset", 'p',  "FILE", 0, "Take package set from FILE", 70 },
{"pkgnevr",'n',  "\"NAME [[E:][V[-R]]]\"", 0,
     "Take package by NAME /and EVR", 70 },

{"install-dist", OPT_INST_INSTDIST, "DIR", 0,
     "Install package set under DIR as root directory", 70 },

{"upgrade-dist", OPT_INST_UPGRDIST, "DIR", OPTION_ARG_OPTIONAL,
     "Upgrade all packages needs upgrade", 70 },

{"install", 'i', 0, 0, "Install given package set", 70 },    
{"upgrade", 'U', 0, 0, "Upgrade given package set", 70 },
{"root", 'r', "DIR", 0, "Set top directory to DIR", 70 },
    
{"dump", OPT_INST_MKSCRIPT, "FILE", OPTION_ARG_OPTIONAL,
     "Just dump install packages to FILE (default stdout)", 70 }, 

{"fetch", OPT_INST_FETCH, "DIR", 0,
     "Do not install, only fetch packages", 70}, 
    
{"nodeps", OPT_INST_NODEPS, 0, 0,
     "Install packages with broken dependencies", 70 },
    
{"force", OPT_INST_FORCE, 0, 0,
     "Be unconcerned", 70 },
    
{"justdb", OPT_INST_JUSTDB, 0, 0,
     "Modify only the database", 70 },
    
{"rpmdef", OPT_INST_RPMDEF, "RPMMACRO", 0,
     "Set up rpm macro (only simple definitions)", 70 },
    
{"test", 't', 0, 0,
 "Don't install, but tell if it would work or not", 70 },
    
{"mkdir", OPT_INST_MKDBDIR, 0, OPTION_HIDDEN,
     "make %{_dbpath} if not exists", 70 },

{"killdb", OPT_INST_KILLDB, 0, OPTION_HIDDEN,           /* not implemented */
     "kills existing database (for install-dist)", 70 },    
    
{"conf", 'c', "FILE", 0, "Read configuration from FILE", 80 }, 
{"noconf", 'z', 0, 0, "Do not read configuration", 80 }, 

#ifdef ENABLE_INTERACTIVE_MODE
{"shell", OPT_SHELLMODE, 0, 0, "Shell mode", 80 },
#endif
    
{0,  'v', "v...", OPTION_ARG_OPTIONAL,
 "Be more (and more) verbose.", 80 },
{0,  'q', 0, 0,
 "Do not produce any output.", 80 },
{ 0, 0, 0, 0, 0, 0 },
};


void check_mjrmode(struct args *argsp) 
{
    if (argsp->mjrmode) {
        log(LOGERR,
     "only one mode of mkidx, update, verify, install*, upgrade* or shell\n"
     "may be specified\n");
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
                log(LOGERR, "poldek: option requires an argument -- %s\n",
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

    if (arg)
        chkarg(key, arg);
    
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

        case 'n': 
            n_array_push(argsp->pkgdef_defs, arg);
            break;

        case 'p':
            n_array_push(argsp->pkgdef_sets, arg);
            break;
            
        case 's':
            argsp->source_path = trimslash(arg);
            break;
            
        case OPT_SOURCEDIR:
            argsp->source_path = trimslash(arg);;
            argsp->ldmethod = PKGSET_LD_DIR;
            break;

        case OPT_SOURCEHDR:
            argsp->source_path = trimslash(arg);
            argsp->ldmethod = PKGSET_LD_HDRFILE;
            break;

        case OPT_SOURCETXT:
            argsp->source_path = trimslash(arg);
            argsp->ldmethod = PKGSET_LD_TXTFILE;
            break;

        case 'P': 
            argsp->pkg_prefix = trimslash(arg);
            break;

        case OPT_SOURCEUP:
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_UPDATEIDX;
            break;

        case OPT_SOURCECACHE:
            argsp->cachedir = trimslash(arg);
            break;
            
        case 'V':
            argsp->verify = 1;
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_VERIFY;
            break;
            
#ifdef ENABLE_INTERACTIVE_MODE
        case OPT_SHELLMODE:
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_SHELL;
            argsp->psflags |= PSMODE_UPGRADE;
            verbose = 1;
            break;
#endif            
        case 'm':
            argsp->psflags |= PSVERIFY_MERCY;
            break;

        case OPT_MKTXTINDEX:
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_MKIDX;
            argsp->psflags |= PSMODE_MKIDX;
            argsp->idx_path = arg;
            argsp->idx_type = INDEXTYPE_TXT;
            break;

        case OPT_MKTXTINDEXZ:
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_MKIDX;
            argsp->psflags |= PSMODE_MKIDX;
            argsp->idx_path = trimslash(arg);
            argsp->idx_type = INDEXTYPE_TXTZ;
            break;
            
        case OPT_MKRPMINDEX:
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_MKIDX;
            argsp->idx_type = INDEXTYPE_RPMH;
            argsp->idx_path = trimslash(arg);
            break;

        case OPT_NODESC:
	    argsp->nodesc = 1;
	    break;
            
        case OPT_INST_INSTDIST:
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_INSTALLDIST;
            argsp->install_root = trimslash(arg);
            argsp->psflags |= PSMODE_INSTALL | PSMODE_INSTALL_DIST;
            break;
            
        case OPT_INST_UPGRDIST:
            check_mjrmode(argsp);
            argsp->mjrmode = MODE_UPGRADEDIST;
            argsp->install_root = arg ? trimslash(arg) : "/";
            argsp->psflags |= PSMODE_UPGRADE | PSMODE_UPGRADE_DIST;
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
            argsp->install_root = trimslash(arg);
            break;
            
        case OPT_INST_RPMDEF:
            if (argsp->rpmacros == NULL)
                argsp->rpmacros = n_array_new(2, NULL, NULL);
            
            n_array_push(argsp->rpmacros, arg);
            break;
            
            
        case OPT_INST_FETCH:
            argsp->fetchdir = trimslash(arg);
            argsp->inst_sflags |= INSTS_JUSTFETCH;
            break;

        case OPT_INST_MKSCRIPT:
            argsp->inst_sflags |= INSTS_JUSTPRINT;
            argsp->dumpfile = trimslash(arg);
            break;
            
        case OPT_INST_NODEPS:
            argsp->instflags  |= PKGINST_NODEPS;
            break;

        case OPT_INST_FORCE:
            argsp->instflags |= PKGINST_FORCE;
            break;
            
        case OPT_INST_JUSTDB:
            argsp->instflags |= PKGINST_JUSTDB;
            break;

        case 't':
            argsp->instflags |= PKGINST_TEST;
            break;

        case OPT_INST_MKDBDIR:
            argsp->inst_sflags |= INSTS_MKDBDIR;
            break;

        case 'c':
            argsp->conf_path = arg;
            break;
            
        case 'z':
            argsp->noconf = 1;
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
                     log(LOGERR, "'%s' option should be set by --%s\n",
                         optname, optname);
                     exit(EXIT_FAILURE);
                 }
                
                if (argsp->rpmopts == NULL)
                    argsp->rpmopts = n_array_new(4, NULL, (tn_fn_cmp)strcmp);

                n_array_push(argsp->rpmopts, arg);
                
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
    struct argp argp = { options, parse_opt, args_doc, doc, 0, 0, 0};
    int vfile_cnflags = 0;
    char *p;


    verbose = 0;
    
    memset(&args, 0, sizeof(args));
    args.source_path = NULL;
    args.pkg_prefix = NULL;
    args.idx_path = NULL;
    args.fetchdir = NULL;
    args.install_root = NULL;
    args.cachedir = tmpdir();
    
    args.pkgdef_files = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    args.pkgdef_defs  = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    args.pkgdef_sets  = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    
    
    argp_parse(&argp, argc, argv, 0, 0, &args);

    if (args.noconf && args.conf_path) {
        log(LOGERR, "--noconf and --conf are exclusive, aren't they?\n");
        exit(EXIT_FAILURE);
    }
    
    if (args.conf_path != NULL)
        htcnf = ldconf(args.conf_path);
    else if (args.noconf == 0)
        htcnf = ldconf_deafult();

    if (args.source_path == NULL && htcnf != NULL)
        args.source_path = conf_get(htcnf, "source", NULL);

    if ((p = conf_get(htcnf, "use_sudo", NULL)) != NULL && strcmp(p, "yes") == 0)
        args.inst_sflags |= INSTS_USESUDO;
    
    if (args.source_path == NULL) {
        log(LOGERR, "No source specified\n");
        exit(EXIT_FAILURE);
    }

    if (args.mjrmode == 0) {
        log(LOGERR, "so what?\n");
        exit(EXIT_FAILURE);
    }
    
    if (args.verify && args.has_pkgdef == 0)
        args.psflags |= PSMODE_VERIFY;

    args.has_pkgdef = n_array_size(args.pkgdef_sets) +
        n_array_size(args.pkgdef_defs) +
        n_array_size(args.pkgdef_files);

    if (htcnf) {
        char *v;
        int is_multi;
        
	if ((v = conf_get(htcnf, "cachedir", NULL)))
	    args.cachedir = v;
        
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

        if ((v = conf_get(htcnf, "rpmdef", &is_multi))) {
            tn_array *macros = NULL;

            if (is_multi)
                macros = conf_get_multi(htcnf, "rpmdef");
         
            if (args.rpmacros == NULL) {
                if (macros)
                    args.rpmacros = macros;
                else {
                    args.rpmacros = n_array_new(4, free, (tn_fn_cmp)strcmp);
                    n_array_push(args.rpmacros, v);
                }
            } else {
                if (macros) {
                    while (n_array_size(macros))
                        n_array_push(args.rpmacros,
                                     strdup(n_array_shift(macros)));
                } else {
                    n_array_push(args.rpmacros, v);
                }
            }
        }
    }

    vfile_verbose = &verbose;
    vfile_configure(args.cachedir ? args.cachedir : tmpdir(), vfile_cnflags);
}


int select_ldmethod(void) 
{
    struct stat st;

    if (args.ldmethod)
        return args.ldmethod;
    
    if (stat(args.source_path, &st) == 0 && S_ISDIR(st.st_mode))
        args.ldmethod = PKGSET_LD_DIR;
    else if (strstr(args.source_path, "tocfile") ||
             strstr(args.source_path, "Packages-hdrs")) 
        args.ldmethod = PKGSET_LD_HDRFILE;
    else 
        args.ldmethod = PKGSET_LD_TXTFILE;
    return args.ldmethod;
}


static struct pkgset *load_pkgset(int ldflags) 
{
    struct pkgset *ps;
    
    if (args.ldmethod == 0) {
        log(LOGERR, "Cannot determine source type\n");
        exit(EXIT_FAILURE);
    }

    ps = pkgset_new(args.psflags);
    if (!pkgset_load(ps, args.ldmethod, ldflags, args.source_path, args.pkg_prefix)) {
        log(LOGERR, "No packages loaded\n");
        pkgset_free(ps);
        ps = NULL;
    }
    mem_info(1, "MEM after load");

    if (ps)
        if (!pkgset_setup(ps)) {
            pkgset_free(ps);
            ps = NULL;
        }

    return ps;
}

static int update_idx(void)
{
    if (strstr(args.source_path, "://") == NULL) {
        log(LOGERR, "? update local index gives no effect\n");
        return 0;
    }
    
    return pkgset_update_txtidx(args.source_path);
}

    
/* ps may be NULL for rpm index */
static int mkidx(struct pkgset *ps) 
{
    int rc;
    char *idx_path = NULL;
    char path[PATH_MAX];
    
    if (args.idx_path != NULL) {
        idx_path = args.idx_path;
        
    } else if (args.ldmethod != PKGSET_LD_DIR) {
        log(LOGERR, "You must specify index path for non-directory source\n");
        return 0;
        
    } else if (strstr(args.source_path, "://")) {
        log(LOGERR, "mkidx requested for URL source without destination "
            "specified\n");
        return 0;
        
    } else {
        switch (args.idx_type) {
            case INDEXTYPE_TXTZ:
                snprintf(path, sizeof(path), "%s/%s", args.source_path, 
                         "Packages.gz");
                break;
                    
            case INDEXTYPE_TXT:
                snprintf(path, sizeof(path), "%s/%s", args.source_path, 
                         "Packages");
                break;
                    
            case INDEXTYPE_RPMH:
                if (args.ldmethod != PKGSET_LD_DIR) {
                    log(LOGERR, "--mkrpmidx option requires that source "
                        "is directory\n");
                    return 0;
                }

                snprintf(path, sizeof(path), "%s/%s", args.source_path, 
                         "Packages-hdrs");
                break;
                    
            default:
                n_assert(0);
                exit(EXIT_FAILURE);
        }

        idx_path = path;
    }
    
    
    n_assert(idx_path != NULL);
    msg(1, "Writing package index to %s...\n", idx_path);
    
    if (args.idx_type == INDEXTYPE_RPMH)
        rc = pkgset_create_rpmidx(args.source_path, idx_path);
    else
        rc = pkgset_create_txtidx(ps, idx_path, args.nodesc);

    return rc;
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


int check_args(void) 
{
    int rc = 1;
    
    switch (args.mjrmode) {
        case 0: 
            log(LOGERR, "so what?\n");
            exit(EXIT_FAILURE);
            break;

#ifdef ENABLE_INTERACTIVE_MODE
        case MODE_SHELL:
#endif            
        case MODE_UPDATEIDX:
            break;
            
        case MODE_VERIFY:
            if (args.has_pkgdef)
                rc = prepare_given_packages();
            
        case MODE_MKIDX:
            verbose = 1;
            break;

            
        case MODE_INSTALLDIST:
        case MODE_INSTALL:
        case MODE_UPGRADE:
            if (prepare_given_packages() == 0) {
                log(LOGERR, "no packages to install\n");
                rc = 0;
            }
            break;
            
        case MODE_UPGRADEDIST:
            if (args.has_pkgdef) {
                log(LOGERR, "-p is not valid in this mode\n");
                exit(EXIT_FAILURE);
            }
            
            break;
            
        default:
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
    
    if (mjrmode == MODE_VERIFY && verbose < 2) 
        verbose = 2;

    if (mjrmode == MODE_VERIFY || mjrmode == MODE_INSTALLDIST)
        markflag = MARK_DEPS;
    
    if (n_array_size(ups->pkgdefs) == 0) {
        log(LOGERR, "no packages specified\n");
        exit(EXIT_FAILURE);
    }

    rc = pkgset_mark_usrset(ps, ups, inst, markflag);
    usrpkgset_free(ups);
    return rc;
}
    

int main(int argc, char **argv)
{
    struct pkgset *ps;
    struct inst_s inst;
    char *logprefix = "poldek";
    int rc = 1;
    int ldflags;
    

    mem_info_verbose = -1;
    
#ifdef ENABLE_INTERACTIVE_MODE
    if (strcmp(n_basenam(argv[0]), "poldeksh") == 0) {
        args.mjrmode = MODE_SHELL;
        logprefix = NULL;
    }
#endif
    
    log_sopenlog(stdout, 0, logprefix);
    parse_options(argc, argv);

    if (!check_args())
        exit(EXIT_FAILURE);

    poldek_init();
    inst_s_init(&inst);
    
    inst.rootdir   = args.install_root;
    inst.instflags = args.instflags;
    inst.fetchdir  = args.fetchdir;
    inst.cachedir  = args.cachedir;
    inst.dumpfile  = args.dumpfile;
    inst.flags     = args.inst_sflags;
    inst.rpmopts   = args.rpmopts;
    inst.rpmacros  = args.rpmacros;

    select_ldmethod();
    rpm_initlib(inst.rpmacros);

    if (args.mjrmode == MODE_MKIDX && args.idx_type == INDEXTYPE_RPMH) {
        if (mkidx(NULL))
            exit(EXIT_SUCCESS);
        exit(EXIT_FAILURE);
    }

    if (args.mjrmode == MODE_UPDATEIDX) {
        if (verbose < 1)
            verbose = 1;
        
        if (update_idx())
            exit(EXIT_SUCCESS);
        exit(EXIT_FAILURE);
    }

    if (args.mjrmode == MODE_VERIFY && args.has_pkgdef == 0 && verbose < 2)
        verbose = 2;

    ldflags = 0;

    if (args.mjrmode == MODE_MKIDX)
        ldflags = PKGSET_IDX_LDRAW;
    
    else if (args.mjrmode == MODE_VERIFY) 
        ldflags = PKGSET_IDX_LDVERIFY;

    if ((ps = load_pkgset(ldflags)) == NULL)
        exit(EXIT_FAILURE);

    
    switch (args.mjrmode) {
#ifdef ENABLE_INTERACTIVE_MODE
        case MODE_SHELL:
            verbose = 1;
            log_sopenlog(stdout, 0, "ERR");
            rc = shell_main(ps, &inst);
            break;
#endif            
        case MODE_VERIFY:
            if (args.has_pkgdef)
                if ((rc = usrpkgset_size(args.ups)))
                    rc = mark_usrset(ps, args.ups, &inst, args.mjrmode);
                    
            break;
            
        case MODE_MKIDX:
            rc = mkidx(ps);
            break;
            
        case MODE_INSTALLDIST:
            if (args.has_pkgdef == 0)
                rc = 0;
            
            else if ((rc = usrpkgset_size(args.ups))) {
                rc = mark_usrset(ps, args.ups, &inst, args.mjrmode);
                if (rc) 
                    rc = install_dist(ps, &inst);
            }
            break;

        case MODE_INSTALL:
        case MODE_UPGRADE:
            if ((rc = usrpkgset_size(args.ups))) {
                if ((rc = mark_usrset(ps, args.ups, &inst, args.mjrmode))) 
                    rc = install_pkgs(ps, &inst);
            }
            break;
            
        case MODE_UPGRADEDIST:
            rc = upgrade_dist(ps, &inst);
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
