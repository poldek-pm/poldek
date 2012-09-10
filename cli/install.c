/* 
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <trurl/trurl.h>

#include "compiler.h"
#include "sigint/sigint.h"

#include "i18n.h"
#include "log.h"
#include "poldek_util.h"
#include "pkg.h"
#include "cli.h"
#include "op.h"

static error_t parse_opt(int key, char *arg, struct argp_state *state);
static error_t cmdl_parse_opt(int key, char *arg, struct argp_state *state);
static int install(struct cmdctx *cmdctx);

#define OPT_GID             OPT_GID_OP_INSTALL
#define OPT_INST_NODEPS     (OPT_GID + 1)
#define OPT_INST_FORCE      (OPT_GID + 2)
#define OPT_INST_REINSTALL  (OPT_GID + 3)
#define OPT_INST_DOWNGRADE  (OPT_GID + 4)
#define OPT_INST_INSTDIST   (OPT_GID + 5)
#define OPT_INST_UPGRDIST   (OPT_GID + 6)
#define OPT_INST_REINSTDIST (OPT_GID + 7)

#define OPT_INST_FETCH      (OPT_GID + 16)

#define OPT_INST_JUSTDB           (OPT_GID + 17)
#define OPT_INST_TEST             't'
#define OPT_INST_RPMDEF           (OPT_GID + 19)
#define OPT_INST_DUMP             (OPT_GID + 20)
#define OPT_INST_DUMPN            (OPT_GID + 21)
#define OPT_INST_NOFOLLOW         'N'
#define OPT_INST_FOLLOW           (OPT_GID + 22) /* bool opt */
#define OPT_INST_FRESHEN          'F'
#define OPT_INST_HOLD             (OPT_GID + 24)
#define OPT_INST_NOHOLD           (OPT_GID + 25)
#define OPT_INST_IGNORE           (OPT_GID + 26)
#define OPT_INST_NOIGNORE         (OPT_GID + 27)
#define OPT_INST_OLDGREEDY        'G'
#define OPT_INST_GREEDY           (OPT_GID + 28)
#define OPT_INST_UNIQNAMES        'Q'
#define OPT_INST_UNIQNAMES_ALIAS  (OPT_GID + 30)
#define OPT_INST_ROOTDIR          'r' 
#define OPT_MERCY                 'm'
#define OPT_PROMOTEEPOCH           (OPT_GID + 32)
#define OPT_PMONLY_NODEPS         (OPT_GID + 33)
#define OPT_PMONLY_FORCE          (OPT_GID + 34)
#define OPT_PM                    (OPT_GID + 35)
#define OPT_INST_NOFETCH          (OPT_GID + 36)
#define OPT_INST_PARSABLETS       (OPT_GID + 37)
#define OPT_INST_MKDIR            (OPT_GID + 38)
#define OPT_INST_CAPLOOKUP        (OPT_GID + 39)

static struct argp_option options[] = {
{0, 'I', 0, 0, N_("Install, not upgrade packages"), OPT_GID },
{"reinstall", OPT_INST_REINSTALL, 0, 0, N_("Reinstall"), OPT_GID }, 
{"downgrade", OPT_INST_DOWNGRADE, 0, 0, N_("Downgrade"), OPT_GID },
{"force", OPT_INST_FORCE, 0, 0,
N_("Install packages ignoring broken dependencies, conflicts, etc"), OPT_GID },
{"test", 't', 0, 0, N_("Don't install, but tell if it would work or not"),
     OPT_GID },
{"fresh", 'F', 0, 0, N_("Upgrade packages, but only if an earlier version "
                        "currently exists"), OPT_GID },
{"nofollow", OPT_INST_NOFOLLOW, 0, 0, N_("Don't install packages required by "
                                         "selected ones"), OPT_GID },

{"follow", OPT_INST_FOLLOW, "[yes|no]", OPTION_ARG_OPTIONAL,
     N_("Install packages required by selected ones"), OPT_GID },
    
{"greedy", OPT_INST_GREEDY, "[yes|no]", OPTION_ARG_OPTIONAL,
        N_("Automatically upgrade packages which dependencies "
           "are broken by unistalled ones"), OPT_GID },

/* legacy, -G w/o parameter */                                           
{"oldgreedy", OPT_INST_OLDGREEDY, NULL, OPTION_HIDDEN, 
        N_("Automatically upgrade packages which dependencies "
           "are broken by unistalled ones"), OPT_GID },
                                           
{"fetch", OPT_INST_FETCH, "DIR", OPTION_ARG_OPTIONAL,
     N_("Download packages to DIR (poldek's cache directory by default)"
        "instead of install them"), OPT_GID },

{"nodeps", OPT_INST_NODEPS, 0, 0,
 N_("Install packages with broken dependencies"), OPT_GID },

{"mercy", OPT_MERCY, 0, 0, N_("Treat requirements with EVR as satisfied by "
                              "unversioned capabilities (old RPM behaviour)"), OPT_GID},

{"promoteepoch", OPT_PROMOTEEPOCH, 0, 0,
     N_("Promote non-existent requirement's epoch to "
        "package's one (rpm prior to 4.2.1 behaviour)"), OPT_GID },
                                           

{"dump", OPT_INST_DUMP, "FILE", OPTION_ARG_OPTIONAL,
N_("Print packages file names to FILE (stdout by default) instead of install them"),
         OPT_GID },

{"dumpn", OPT_INST_DUMPN, "FILE", OPTION_ARG_OPTIONAL,
N_("Print packages names to FILE (stdout by default) instead of install them"), OPT_GID },

{"justdb", OPT_INST_JUSTDB, 0, 0, N_("Modify only the database"), OPT_GID },
                                           
{"pm-nodeps", OPT_PMONLY_NODEPS, 0, 0, 
N_("Same as --nodeps but applied to PM (rpm) only"), OPT_GID },

{"rpm-nodeps", 0, 0, OPTION_ALIAS | OPTION_HIDDEN, 0, OPT_GID },

{"pm-force", OPT_PMONLY_FORCE, 0, 0,
N_("Same as --force but applied to PM (rpm) only)"), OPT_GID },
{"rpm-force", 0, 0, OPTION_ALIAS | OPTION_HIDDEN, 0, OPT_GID },
    
{"pmop", OPT_PM, "OPTION", 0, 
 N_("pass option OPTION to PM binary"), OPT_GID },
{"rpm", 0, 0, OPTION_ALIAS | OPTION_HIDDEN, 0, OPT_GID },
{"pmopt", 0, 0, OPTION_ALIAS | OPTION_HIDDEN, 0, OPT_GID },                                           

{"nohold", OPT_INST_NOHOLD, 0, 0,
 N_("Do not hold any packages. Disables --hold settings."), OPT_GID },    

/* hidden, for debugging/testing purposes */
{"nofetch", OPT_INST_NOFETCH, 0, OPTION_HIDDEN, 
     N_("Do not download packages"), OPT_GID },
    
{"caplookup", OPT_INST_CAPLOOKUP, 0, 0,
N_("Look into package capabilities and files to resolve packages"), OPT_GID },

{ 0, 0, 0, 0, 0, 0 },
};


static struct argp_option cmdl_options[] = {
    {0,0,0,0, N_("Package installation:"), OPT_GID - 10 },
    {"install", 'i', 0, 0, N_("Install given packages"), OPT_GID - 10 },
    /* shadow cli 'I' to hide_child_options() could hide it, messss */
    {0, 'I', 0, OPTION_HIDDEN | OPTION_ALIAS, 0, OPT_GID - 10 },
    
    {"reinstall", OPT_INST_REINSTALL, 0, 0, N_("Reinstall given packages"),
         OPT_GID - 10},
    {"downgrade", OPT_INST_DOWNGRADE, 0, 0, N_("Downgrade"), OPT_GID - 10 },     
    {"upgrade", 'u', 0, 0, N_("Upgrade given packages"), OPT_GID - 10 },
    { NULL, 'U', 0, OPTION_ALIAS, 0, OPT_GID - 10 }, 
        
    {NULL, 'h', 0, OPTION_HIDDEN, "", OPT_GID - 10 }, /* for compat with -Uvh */
    
    {0,0,0,0, N_("Distribution installation/upgrade:"), OPT_GID - 9 },
    {"install-dist", OPT_INST_INSTDIST, "DIR", 0,
    N_("Install package set under DIR as root directory"), OPT_GID - 9 },

    {"upgrade-dist", OPT_INST_UPGRDIST, "DIR", OPTION_ARG_OPTIONAL,
     N_("Upgrade all packages needs upgrade"), OPT_GID - 9 },

    {"reinstall-dist", OPT_INST_REINSTDIST, "DIR", OPTION_ARG_OPTIONAL,
     N_("Reinstall all packages under DIR as root directory"), OPT_GID - 9 },

    {"root", OPT_INST_ROOTDIR, "DIR", 0, N_("Set top directory to DIR"), OPT_GID - 9 },

    {0,0,0,0, N_("Installation switches:"), OPT_GID },
{"hold", OPT_INST_HOLD, "PACKAGE[,PACKAGE]...", 0,
 N_("Prevent packages listed from being upgraded if they are already installed."),
     OPT_GID },

{"nohold", OPT_INST_NOHOLD, 0, 0,
 N_("Do not hold any packages. Disables --hold settings."), OPT_GID },
                                                
{"ignore", OPT_INST_IGNORE, "PACKAGE[,PACKAGE]...", 0,
 N_("Make packages listed invisible."), OPT_GID },

{"noignore", OPT_INST_NOIGNORE, NULL, 0,
 N_("Make invisibled packages visible. Disables --ignore settings."), OPT_GID },

{"uniq", OPT_INST_UNIQNAMES, 0, 0, 
N_("Remove package duplicates from available package list"), OPT_GID },

{"unique-pkg-names", OPT_INST_UNIQNAMES_ALIAS, 0, OPTION_ALIAS | OPTION_HIDDEN,
        0, OPT_GID },

{"parsable-tr-summary", OPT_INST_PARSABLETS, 0, 0,
     N_("Print installation summary in parseable form"), OPT_GID },

{"mkdir", OPT_INST_MKDIR, 0, OPTION_HIDDEN, /* legacy */
     "make %{_dbpath} directory if not exists", OPT_GID },
    
{ 0, 0, 0, 0, 0, 0 },
};


struct poclidek_cmd command_install = {
    COMMAND_HASVERBOSE | COMMAND_MODIFIESDB |
    COMMAND_PIPEABLE_LEFT | COMMAND_PIPE_XARGS | COMMAND_PIPE_PACKAGES, 
    "install", N_("PACKAGE..."), N_("Install packages"), 
    options, parse_opt,
    NULL, install, NULL, NULL, NULL, NULL, NULL, 0, 0
};

static struct argp cmd_argp = {
    options, parse_opt, 0, 0, 0, 0, 0
};

static struct argp_child cmd_argp_child[2] = {
    { &cmd_argp, 0, NULL, 0 },
    { NULL, 0, NULL, 0 },
};

static struct argp poclidek_argp = {
    cmdl_options, cmdl_parse_opt, 0, 0, cmd_argp_child, 0, 0
};

static 
struct argp_child poclidek_argp_child = {
    &poclidek_argp, 0, NULL, OPT_GID,
};


static int cmdl_run(struct poclidek_opgroup_rt *rt);

struct poclidek_opgroup poclidek_opgroup_install = {
    "Package installation", 
    &poclidek_argp, 
    &poclidek_argp_child,
    cmdl_run
};

struct cmdl_arg_s {
    struct cmdctx cmdctx;
};

static
error_t cmdl_parse_opt(int key, char *arg, struct argp_state *state)
{
    struct poclidek_opgroup_rt  *rt;
    struct poldek_ts            *ts;
    struct cmdl_arg_s           *arg_s;

    rt = state->input;
    ts = rt->ts;
    arg = arg;
    
    if (rt->_opdata != NULL) {  /* TODO: is it really needed? */
        arg_s = rt->_opdata;
        
    } else {
        arg_s = n_malloc(sizeof(*arg_s));
        memset(arg_s, 0, sizeof(*arg_s));
        arg_s->cmdctx.ts = rt->ts;
        rt->_opdata = arg_s;
        rt->_opdata_free = free;
    }
    
    switch (key) {
        case ARGP_KEY_INIT:
            state->child_inputs[0] = &arg_s->cmdctx;
            state->child_inputs[1] = NULL;
            break;

        case 'i':
            poldek_ts_set_type(ts, POLDEK_TS_INSTALL, "install");
            poldek_ts_setf(ts, POLDEK_TS_INSTALL);
            rt->set_major_mode(rt, "install", NULL);
            break;

        case OPT_INST_DOWNGRADE:
        case OPT_INST_REINSTALL:
        case 'U':
        case 'u':
            rt->set_major_mode(rt, "upgrade", key == OPT_INST_DOWNGRADE ? "downgrade" :
                               key == OPT_INST_REINSTALL ? "reinstall" : NULL);
            
            poldek_ts_set_type(ts, POLDEK_TS_INSTALL, "install");
            poldek_ts_setf(ts, POLDEK_TS_UPGRADE);
            
            if (key == OPT_INST_DOWNGRADE)
                poldek_ts_setf(ts, POLDEK_TS_DOWNGRADE);
            
            else if (key == OPT_INST_REINSTALL)
                poldek_ts_setf(ts, POLDEK_TS_REINSTALL);
            break;

        case 'h':          /* common rpm's users brain hardcoded -Uvh  */
            break;
            

        case OPT_INST_INSTDIST:
            poldek_ts_set_type(ts, POLDEK_TS_INSTALL, "install-dist");
            poldek_ts_setf(ts, POLDEK_TS_DIST);
            if (arg)
                poldek_ts_configure(ts, POLDEK_CONF_ROOTDIR, arg);
            rt->set_major_mode(rt, "install-dist", NULL);
            break;

        case OPT_INST_REINSTDIST:
            poldek_ts_set_type(ts, POLDEK_TS_INSTALL, "reinstall-dist");
            poldek_ts_setf(ts, POLDEK_TS_DIST);
            poldek_ts_setf(ts, POLDEK_TS_UPGRADE);
            poldek_ts_setf(ts, POLDEK_TS_REINSTALL);
            if (arg)
                poldek_ts_configure(ts, POLDEK_CONF_ROOTDIR, arg);
            rt->set_major_mode(rt, "reinstall-dist", NULL);
            break;

        case OPT_INST_UPGRDIST:
            poldek_ts_set_type(ts, POLDEK_TS_INSTALL, "upgrade-dist");
            poldek_ts_setf(ts, POLDEK_TS_DIST);
            poldek_ts_setf(ts, POLDEK_TS_UPGRADE);
            if (arg)
                poldek_ts_configure(ts, POLDEK_CONF_ROOTDIR, arg);
            rt->set_major_mode(rt, "upgrade-dist", NULL);
            break;

        case OPT_INST_ROOTDIR:
            poldek_configure(ts->ctx, POLDEK_CONF_ROOTDIR, arg);
            break;

        case OPT_INST_HOLD:
            poldek_configure(ts->ctx, POLDEK_CONF_OPT, POLDEK_OP_HOLD, 1);
            poldek_configure(ts->ctx, POLDEK_CONF_HOLD, arg);
            break;
        
        case OPT_INST_NOHOLD:
            ts->setop(ts, POLDEK_OP_HOLD, 0);
            poldek_configure(ts->ctx, POLDEK_CONF_OPT, POLDEK_OP_HOLD, 0);
            break;
        
        case OPT_INST_IGNORE:
            poldek_configure(ts->ctx, POLDEK_CONF_OPT, POLDEK_OP_IGNORE, 1);
            poldek_configure(ts->ctx, POLDEK_CONF_IGNORE, arg);
            break;
	
        case OPT_INST_NOIGNORE:
            ts->setop(ts, POLDEK_OP_IGNORE, 0);
            poldek_configure(ts->ctx, POLDEK_CONF_OPT, POLDEK_OP_IGNORE, 0);
            break;

        case OPT_INST_UNIQNAMES:
        case OPT_INST_UNIQNAMES_ALIAS:
            poldek_configure(ts->ctx, POLDEK_CONF_OPT, POLDEK_OP_UNIQN, 1);
            break;

        case OPT_INST_PARSABLETS:
            ts->setop(ts, POLDEK_OP_PARSABLETS, 1);
            break;

        case OPT_INST_MKDIR:
            break;              /* ignored, directory is created by default */
            
        case 'I':               /* silently ignore */
            break;

        default:
            return ARGP_ERR_UNKNOWN;
            
    }
    
    return 0;
}

static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdctx         *cmdctx = state->input;
    struct poldek_ts      *ts;


    ts = cmdctx->ts;
    
    switch (key) {
        case ARGP_KEY_INIT:
            break;
            
        case OPT_MERCY:
            ts->setop(ts, POLDEK_OP_VRFYMERCY, 1);
            break;

        case OPT_PROMOTEEPOCH:
            ts->setop(ts, POLDEK_OP_PROMOTEPOCH, 1);
            break;
            
        case OPT_INST_NODEPS:
            ts->setop(ts, POLDEK_OP_NODEPS, 1);
            break;
            
        case OPT_INST_FORCE:
            ts->setop(ts, POLDEK_OP_FORCE, 1);
            break;
            
        case 't':
            if (ts->getop(ts, POLDEK_OP_TEST))
                ts->setop(ts, POLDEK_OP_RPMTEST, 1);
            else
                ts->setop(ts, POLDEK_OP_TEST, 1);
            break;
            
        case 'F':
            ts->setop(ts, POLDEK_OP_FRESHEN, 1);
            break;

        case OPT_INST_NOFOLLOW:
            ts->setop(ts, POLDEK_OP_FOLLOW, 0);
            break;

        case OPT_INST_FOLLOW:
            if (!arg) {
                ts->setop(ts, POLDEK_OP_FOLLOW, 1);
                
            } else {
                int bool = poldek_util_parse_bool(arg);
                if (bool == -1) {
                    logn(LOGERR, _("invalid value ('%s') of option 'follow'"),
                         arg);
                    return EINVAL;
                }
                ts->setop(ts, POLDEK_OP_FOLLOW, bool);
            }
            break;

        case OPT_INST_OLDGREEDY:
            ts->setop(ts, POLDEK_OP_GREEDY, 1);
            break;

        case OPT_INST_GREEDY:
            if (!arg) {
                ts->setop(ts, POLDEK_OP_GREEDY, 1);
                
            } else {
                int v, bool;
                    
                if (sscanf(arg, "%u", &v) == 1) {
                    bool = v;
                    
                } else if ((bool = poldek_util_parse_bool(arg)) == -1) {
                    logn(LOGERR, _("invalid value ('%s') of option 'greedy'"),
                         arg);
                    return EINVAL;
                }
                
                ts->setop(ts, POLDEK_OP_GREEDY, bool);
            }
            break;
            
        case 'I':
            poldek_ts_setf(ts, POLDEK_TS_INSTALL);
            poldek_ts_clrf(ts, POLDEK_TS_UPGRADE);
            
            break;

        case OPT_INST_REINSTALL:
            poldek_ts_setf(ts, POLDEK_TS_REINSTALL);
            break;

        case OPT_INST_DOWNGRADE:
            poldek_ts_setf(ts, POLDEK_TS_DOWNGRADE);
            break;

        case OPT_INST_NOHOLD:
            ts->setop(ts, POLDEK_OP_HOLD, 0);
            break;
            
        case OPT_INST_JUSTDB:
            ts->setop(ts, POLDEK_OP_JUSTDB, 1);
            break;

        case OPT_INST_DUMP:
            if (arg)
                poldek_ts_configure(ts, POLDEK_CONF_DUMPFILE, arg);
            ts->setop(ts, POLDEK_OP_JUSTPRINT, 1);
            break;

        case OPT_INST_DUMPN:
            if (arg)
                poldek_ts_configure(ts, POLDEK_CONF_DUMPFILE, arg);
            ts->setop(ts, POLDEK_OP_JUSTPRINT_N, 1);
            break;

        case OPT_INST_NOFETCH:
            ts->setop(ts, POLDEK_OP_NOFETCH, 1);
            break;
 
        case OPT_INST_CAPLOOKUP:
            ts->setop(ts, POLDEK_OP_CAPLOOKUP, 1);
            break;

        case OPT_INST_FETCH:
            if (arg) {
                if (!poldek_util_is_rwxdir(arg)) {
                    logn(LOGERR, _("%s: no such directory"), arg);
                    return EINVAL;
                }
                
                poldek_ts_configure(ts, POLDEK_CONF_FETCHDIR, arg);
            }

            ts->setop(ts, POLDEK_OP_JUSTFETCH, 1);
            break;

        case OPT_PMONLY_FORCE:
            poldek_ts_configure(ts, POLDEK_CONF_RPMOPTS, "--force");
            break;
            
        case OPT_PMONLY_NODEPS:
            poldek_ts_configure(ts, POLDEK_CONF_RPMOPTS, "--nodeps");
            break;

        case OPT_PM: {
            tn_array *tl = NULL;
            unsigned int i;
            
            if ((tl = n_str_etokl_ext(arg, "\t ", "", "\"'", '\\')) == NULL) {
                logn(LOGERR, _("%s: parse error"), arg);
                return ARGP_ERR_UNKNOWN;
            }

            for (i=0; i < n_array_size(tl); i++) {
                char *a, opt[256], *dash = "--";

                a = n_array_nth(tl, i);
                if (*a == '-')
                    dash = "";
            
                n_snprintf(opt, sizeof(opt), "%s%s", dash, a);
                poldek_ts_configure(ts, POLDEK_CONF_RPMOPTS, opt);
            }

            n_array_cfree(&tl);
        }
            break;
            
            
        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int install(struct cmdctx *cmdctx)
{
    struct poclidek_ctx  *cctx;
    struct poldek_ts     *ts;
    int rc = 1, is_test;
    
    cctx = cmdctx->cctx;
    ts = cmdctx->ts;
    
    poldek_ts_set_type(ts, POLDEK_TS_INSTALL, "install-cmd");
    if (!poldek_ts_issetf(ts, POLDEK_TS_INSTALL))
        poldek_ts_setf(ts, POLDEK_TS_UPGRADE); /* the default */
    is_test = ts->getop_v(ts, POLDEK_OP_TEST, POLDEK_OP_RPMTEST, 0);

    rc = poldek_ts_run(ts, is_test ? 0 : POLDEK_TS_TRACK);
    
    if (rc == 0 && !sigint_reached())
        msgn(1, _("There were errors"));
    
    if (!is_test && cmdctx->cctx->pkgs_installed)
        poclidek_apply_iinf(cmdctx->cctx, ts);
    
    return rc;
}

static int cmdl_run(struct poclidek_opgroup_rt *rt)
{
    int rc;

    //DBGF("%p->%p, %p->%p\n", rt->ts, rt->ts->hold_patterns,
    //     rt->ts->ctx->ts, rt->ts->ctx->ts->hold_patterns);
    
    if (poldek_ts_get_type(rt->ts) != POLDEK_TS_INSTALL)
        return OPGROUP_RC_NIL;

    if (!poldek_ts_issetf_all(rt->ts, POLDEK_TS_UPGRADEDIST)) {
        if (poldek_ts_get_arg_count(rt->ts) == 0) {
            logn(LOGERR, _("no packages specified"));
            return OPGROUP_RC_ERROR;
        }
    }

    rc = poldek_ts_run(rt->ts, 0);
    return rc ? OPGROUP_RC_OK : OPGROUP_RC_ERROR;
}
