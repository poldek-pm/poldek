/*
  Copyright (C) 2000 - 2005 Pawel A. Gajda <mis@k2.net.pl>

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

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <unistd.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/n_snprintf.h>

#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgfl.h"
#include "pkgu.h"
#include "capreq.h"
#include "pkgset-req.h"         /* for struct reqpkg, TOFIX */
#include "sigint/sigint.h"

#include "cli.h"
#include "sigint/sigint.h"

#define IDENT     16
#define SUBIDENT  4
#define RMARGIN   2


static error_t parse_opt(int key, char *arg, struct argp_state *state);
static int desc(struct cmdctx *cmdctx);

#define OPT_DESC_CAPS         (1 << 0)
#define OPT_DESC_REQS         (1 << 1)
#define OPT_DESC_REQDIRS      (1 << 2)
#define OPT_DESC_REQPKGS      (1 << 3)
#define OPT_DESC_REVREQPKGS   (1 << 4)
#define OPT_DESC_CNFLS        (1 << 5)
#define OPT_DESC_DESCR        (1 << 6)
#define OPT_DESC_FL           (1 << 7)
#define OPT_DESC_FL_LONGFMT   (1 << 8)
#define OPT_DESC_ALL          (OPT_DESC_CAPS | OPT_DESC_REQS | \
                               OPT_DESC_REQDIRS |                       \
                               OPT_DESC_REQPKGS | OPT_DESC_REVREQPKGS | \
                               OPT_DESC_CNFLS |                         \
                               OPT_DESC_DESCR |                         \
                               OPT_DESC_FL)



static struct argp_option options[] = {
    { "all",  'a', 0, 0,
      N_("Show all described below fields"), 1},
    
    { "capreqs",  'C', 0, 0,
      N_("Show capabilities, requirements, conflicts and obsolences"),
      1},

    { "provides",  'p', 0, 0, N_("Show package's capablities"), 1},

    { "requires",  'r', 0, 0,
      N_("Show requirements"), 1},

    { "reqpkgs",  'R', 0, 0,
      N_("Show required packages"), 1},

    { "reqbypkgs",  'B', 0, 0,
      N_("Show packages which requires given package"), 1},

    { "conflicts",  'c', 0, 0,
      N_("Show conflicts and obsolences"), 1},
    
    { "descr", 'd', 0, 0, N_("Show description (the default)"), 1},
    
    { "files", 'f', 0, 0,
      N_("Show package files (doubled gives long listing format)"), 1},
    { NULL,        'l', 0,  OPTION_ALIAS, 0, 1},
    { 0, 0, 0, 0, 0, 0 },
};


struct poclidek_cmd command_desc = {
    COMMAND_PIPE_DEFAULTS, 
    "desc", N_("PACKAGE..."), N_("Display packages info"), 
    options, parse_opt,
    NULL, desc,
    NULL, NULL, NULL, NULL, NULL, 0, 0
};

static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdctx *cmdctx = state->input;

    arg = arg;
    
    switch (key) {
        case 'a':
            cmdctx->_flags |= OPT_DESC_ALL;
            break;
            
        case 'C':
            cmdctx->_flags |= OPT_DESC_CAPS | OPT_DESC_REQS | OPT_DESC_CNFLS;
            break;
            

        case 'c':
            cmdctx->_flags |= OPT_DESC_CNFLS;
            break;

        case 'p':
            cmdctx->_flags |= OPT_DESC_CAPS;
            break;

        case 'r':
            cmdctx->_flags |= OPT_DESC_REQS;
            cmdctx->_flags |= OPT_DESC_REQDIRS;
            break;
            
        case 'R':
            cmdctx->_flags |= OPT_DESC_REQPKGS;
            break;

        case 'B':
            cmdctx->_flags |= OPT_DESC_REVREQPKGS;
            break;

        case 'f':
        case 'l':
            if (cmdctx->_flags & OPT_DESC_FL)
                cmdctx->_flags |= OPT_DESC_FL_LONGFMT;
            else
                cmdctx->_flags |= OPT_DESC_FL;
            break;
            
        case 'd':
            cmdctx->_flags |= OPT_DESC_DESCR;
            break;
            
        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int nlident(struct cmdctx *cmdctx, int width) 
{
    char fmt[64];

    snprintf(fmt, sizeof(fmt), "\n%%%dc", width);
    return cmdctx_printf(cmdctx, fmt, ' ');
}


static void show_caps(struct cmdctx *cmdctx, struct pkg *pkg, int term_width)
{
    int i, ncol = IDENT;
    char *p, *colon = ", ";
    
    if (pkg->caps && n_array_size(pkg->caps)) {
        int ncaps, hdr_printed = 0;
        
        ncol = IDENT;
        ncaps = n_array_size(pkg->caps);
        
        for (i=0; i < n_array_size(pkg->caps); i++) {
            struct capreq *cr = n_array_nth(pkg->caps, i);

            if (pkg_eq_capreq(pkg, cr))
                ncaps--;
        }
        
        for (i=0; i < n_array_size(pkg->caps); i++) {
            struct capreq *cr = n_array_nth(pkg->caps, i);

            if (pkg_eq_capreq(pkg, cr))
                continue;
            
            if (hdr_printed == 0) {
                cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "Provides:");
                hdr_printed = 1;
            }
            	
            p = capreq_snprintf_s(cr);
            if (ncol + (int)strlen(p) >= term_width) {
                ncol = SUBIDENT;
                nlident(cmdctx, ncol);
            }
            
            if (--ncaps == 0)
                colon = "";
            
            ncol += cmdctx_printf(cmdctx, "%s%s", p, colon);
        }
        
        if (hdr_printed)
            cmdctx_printf(cmdctx, "\n");
    }
}


static void show_reqs(struct cmdctx *cmdctx, struct pkg *pkg, int term_width)
{
    int ncol = IDENT, nrpmreqs = 0, nreqs = 0, nprereqs = 0, nprereqs_un = 0;
    int i;

    if (pkg->reqs == NULL || n_array_size(pkg->reqs) == 0)
        return;
    
    for (i=0; i<n_array_size(pkg->reqs); i++) {
        struct capreq *cr = n_array_nth(pkg->reqs, i);
        int is_prereq = 0;
        
        if (pkg_eq_capreq(pkg, cr))
            continue;

        if (capreq_is_bastard(cr))
            continue;
        
        if (capreq_is_rpmlib(cr)) {
            nrpmreqs++;
            continue;
        }
        
        if (capreq_is_prereq_un(cr)) {
            nprereqs_un++;
            is_prereq = 1;
        }
        
        
        if (capreq_is_prereq(cr)) {
            is_prereq = 1;
            nprereqs++;
        }
        
        if (is_prereq == 0)
            nreqs++;
    }


    if (nprereqs) {
        char *p, *colon = ", ";
        int n = 0;

        ncol = IDENT;
        cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "Requires(pre):");
        for (i=0; i<n_array_size(pkg->reqs); i++) {
            struct capreq *cr = n_array_nth(pkg->reqs, i);
                
            if (pkg_eq_capreq(pkg, cr))
                continue;

            if (capreq_is_bastard(cr))
                continue;
            
            if (capreq_is_rpmlib(cr))
                continue;

            if (!capreq_is_prereq(cr))
                continue;
            
            if (++n == nprereqs)
                colon = "";
                
            p = capreq_snprintf_s(cr);
            if (ncol + (int)strlen(p) >= term_width) {
                ncol = SUBIDENT;
                nlident(cmdctx, ncol);
            }
            ncol += cmdctx_printf(cmdctx, "%s%s", p, colon);
        }
        cmdctx_printf(cmdctx, "\n");
    }

        
    if (nreqs) {
        char *p, *colon = ", ";
        int n = 0;

        ncol = IDENT;
        cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "Requires:");
        for (i=0; i<n_array_size(pkg->reqs); i++) {
            struct capreq *cr = n_array_nth(pkg->reqs, i);
                
            if (pkg_eq_capreq(pkg, cr))
                continue;
                
            if (capreq_is_rpmlib(cr))
                continue;

            if (capreq_is_bastard(cr))
                continue;

            if (capreq_is_prereq(cr))
                continue;

            if (capreq_is_prereq_un(cr))
                continue;
            
            if (++n == nreqs)
                colon = "";
                
            p = capreq_snprintf_s(cr);
            if (ncol + (int)strlen(p) >= term_width) {
                ncol = SUBIDENT;
                nlident(cmdctx, ncol);
            }
            ncol += cmdctx_printf(cmdctx, "%s%s", p, colon);
        }
        cmdctx_printf(cmdctx, "\n");
    }

    if (nprereqs_un) {
        char *p, *colon = ", ";
        int n = 0;

        ncol = IDENT;
        cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "Requires(un):");
        for (i=0; i<n_array_size(pkg->reqs); i++) {
            struct capreq *cr = n_array_nth(pkg->reqs, i);
                
            if (pkg_eq_capreq(pkg, cr))
                continue;

            if (!capreq_is_prereq_un(cr))
                continue;
            
            if (++n == nprereqs_un)
                colon = "";
                
            p = capreq_snprintf_s(cr);
            if (ncol + (int)strlen(p) >= term_width) {
                ncol = SUBIDENT;
                nlident(cmdctx, ncol);
            }
            ncol += cmdctx_printf(cmdctx, "%s%s", p, colon);
        }
        cmdctx_printf(cmdctx, "\n");
    }

                

    if (nrpmreqs) {
        char *p, *colon = ", ";
        int n = 0;

        ncol = IDENT;
        cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "Requires(rpm):");
        for (i=0; i<n_array_size(pkg->reqs); i++) {
            struct capreq *cr = n_array_nth(pkg->reqs, i);

            if (!capreq_is_rpmlib(cr))
                continue;
                
            if (++n == nrpmreqs)
                colon = "";
                
            p = capreq_snprintf_s(cr);
            if (ncol + (int)strlen(p) >= term_width) {
                ncol = SUBIDENT;
                nlident(cmdctx, ncol);
            }
            ncol += cmdctx_printf(cmdctx, "%s%s", p, colon);
        }
        cmdctx_printf(cmdctx, "\n");
    }
}


static void show_suggests(struct cmdctx *cmdctx, struct pkg *pkg, int term_width)
{
    char *p, *colon = ", ";
    int i, ncol;
    
    if (pkg->sugs == NULL)
        return;

    ncol = IDENT;
    cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "Suggests:");
    for (i=0; i<n_array_size(pkg->sugs); i++) {
        struct capreq *cr = n_array_nth(pkg->sugs, i);
                
        if (i == (n_array_size(pkg->sugs) - 1))
            colon = "";
                
        p = capreq_snprintf_s(cr);
        if (ncol + (int)strlen(p) >= term_width) {
            ncol = SUBIDENT;
            nlident(cmdctx, ncol);
        }
        ncol += cmdctx_printf(cmdctx, "%s%s", p, colon);
    }
    cmdctx_printf(cmdctx, "\n");
}


static void show_reqdirs(struct cmdctx *cmdctx, struct pkg *pkg, int term_width)
{
    int i, ncol = IDENT;
    char *colon = ", ";
    tn_array *dirs;
    
    
    dirs = pkg_required_dirs(pkg);
    
    if (dirs && n_array_size(dirs)) {
        int n, hdr_printed = 0;
        
        ncol = IDENT;
        n = n_array_size(dirs);
        
        for (i=0; i < n_array_size(dirs); i++) {
            const char *dir = n_array_nth(dirs, i);
            
            if (hdr_printed == 0) {
                cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "Requires(dir):");
                hdr_printed = 1;
            }
            	
            if (ncol + (int)strlen(dir) >= term_width) {
                ncol = SUBIDENT;
                nlident(cmdctx, ncol);
            }
            
            if (--n == 0)
                colon = "";
            
            ncol += cmdctx_printf(cmdctx, "%s%s", dir, colon);
        }
        
        if (hdr_printed)
            cmdctx_printf(cmdctx, "\n");
    }
}


static void show_reqpkgs(struct cmdctx *cmdctx, struct pkg *pkg, int term_width)
{
    char *colon = ", ";
    int i, ncol = IDENT;
    
    if (pkg->reqpkgs == NULL || n_array_size(pkg->reqpkgs) == 0)
        return;
    
    cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "Required(pkgs):");

    for (i=0; i<n_array_size(pkg->reqpkgs); i++) {
        struct reqpkg *rp = n_array_nth(pkg->reqpkgs, i);
        char *p;

        p = rp->pkg->name;
            
        if (ncol + (int)strlen(p) >= term_width) {
            ncol = SUBIDENT;
            nlident(cmdctx, ncol);
        }
        ncol += cmdctx_printf(cmdctx, "%s", p);
            
        if (rp->flags & REQPKG_MULTI) {
            int n = 0;

            ncol += cmdctx_printf(cmdctx, " | ");
                
            while (rp->adds[n]) {
                char *p = rp->adds[n++]->pkg->name;
                if (ncol + (int)strlen(p) >= term_width) {
                    ncol = SUBIDENT;
                    nlident(cmdctx, ncol);
                }
                ncol += cmdctx_printf(cmdctx, "%s", p);
                if (rp->adds[n] != NULL) {
                    ncol += cmdctx_printf(cmdctx, " | ");
                }
            }
        }
            
        if (i + 1 < n_array_size(pkg->reqpkgs))
            ncol += cmdctx_printf(cmdctx, colon);
    }
    cmdctx_printf(cmdctx, "\n");
}

static
void show_revreqpkgs(struct cmdctx *cmdctx, struct pkg *pkg, int term_width)
{
    int i;
    
    if (pkg->revreqpkgs && n_array_size(pkg->revreqpkgs)) {
        int ncol = IDENT;
        
        cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "Required(by):");

        for (i=0; i<n_array_size(pkg->revreqpkgs); i++) {
            struct pkg *tmpkg;	
            char *p, *colon = ", ";

            
            tmpkg = n_array_nth(pkg->revreqpkgs, i);
            
            p = tmpkg->name;
            if (ncol + (int)strlen(p) + 2 >= term_width) {
                ncol = SUBIDENT;
                nlident(cmdctx, ncol);
            }
            if (i + 1 == n_array_size(pkg->revreqpkgs))
                colon = "";
                    
            ncol += cmdctx_printf(cmdctx, "%s%s", p, colon);
        }
        cmdctx_printf(cmdctx, "\n");
    }
}


static void show_cnfls(struct cmdctx *cmdctx, struct pkg *pkg, int term_width)
{
    int i, ncol = 0;

    if (pkg->cnfls && n_array_size(pkg->cnfls)) {
        int nobsls = 0;
        
        for (i=0; i<n_array_size(pkg->cnfls); i++) {
            struct capreq *cr = n_array_nth(pkg->cnfls, i);
            if (capreq_is_obsl(cr))
                nobsls++;
        }
        
        if (nobsls != n_array_size(pkg->cnfls)) {
            int n = 0;
            
            ncol = cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "Conflicts:");
            for (i=0; i<n_array_size(pkg->cnfls); i++) {
                struct capreq *cr = n_array_nth(pkg->cnfls, i);
                
                if (capreq_is_obsl(cr))
                    continue;
                n++;
                ncol += cmdctx_printf(cmdctx, capreq_snprintf_s(cr));
                if (n < n_array_size(pkg->cnfls) - nobsls)
                    ncol += cmdctx_printf(cmdctx, ", ");

                if (ncol >= term_width) {
                    ncol = SUBIDENT;
                    nlident(cmdctx, ncol);
                }
            }
            cmdctx_printf(cmdctx, "\n");
        }
        
        if (nobsls) {
            int n = 0;
            
            ncol = cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "Obsoletes:");
            for (i=0; i<n_array_size(pkg->cnfls); i++) {
                struct capreq *cr = n_array_nth(pkg->cnfls, i);
                char s[255], slen;
                
                if (!capreq_is_obsl(cr))
                    continue;
                n++;

                slen = capreq_snprintf(s, sizeof(s), cr);
                //cmdctx_printf(cmdctx, "[%d] %d; %d, %d, %s\n", term_width,
                //       ncol, n, strlen(name), name);
                //continue;
                if (ncol + slen + 2 >= term_width) {
                    ncol = SUBIDENT;
                    nlident(cmdctx, ncol);
                }
                ncol += cmdctx_printf(cmdctx, "%s%s", s, n < nobsls ? ", " : "");
            }
            cmdctx_printf(cmdctx, "\n");
        }
    }
}

static void mode_t_to_str(char *str, int size, mode_t mode)
{
    snprintf(str, size, "%c%c%c%c%c%c%c%c%c%c",
             S_ISDIR(mode) ? 'd' : (S_ISSOCK(mode) ? 's'  :
                                    (S_ISCHR(mode) ? 'c'  :
                                     (S_ISBLK(mode) ? 'b' :
                                      (S_ISBLK(mode) ? 'b' :
                                       (S_ISFIFO(mode) ? 'f' : '-'))))), 
             
             (mode & S_IRUSR) != 0 ? 'r': '-',
             (mode & S_IWUSR) != 0 ? 'w' : '-',
             (mode & S_ISUID) != 0 ? 's' : (mode & S_IXUSR) != 0 ? 'x': '-',
             (mode & S_IRGRP) != 0 ? 'r' : '-',
             (mode & S_IWGRP) != 0 ? 'w' : '-',
             (mode & S_ISGID) != 0 ? 's' : (mode & S_IXGRP) != 0 ? 'x': '-',
             (mode & S_IROTH) != 0 ? 'r' : '-',
             (mode & S_IWOTH) != 0 ? 'w' : '-',
             (mode & S_ISVTX) != 0 ? 't' : (mode & S_IXOTH) != 0 ? 'x': '-');
}



    
static void list_files_long(struct cmdctx *cmdctx, tn_tuple *fl, int mode_octal)
{
    int i, j;
    const char *fmt = "!%-10s%10s\t%s\n";


    if (mode_octal) 
        fmt = "!%-6s%10s\t%s\n";
        
    cmdctx_printf_c(cmdctx, PRCOLOR_YELLOW, fmt, _("mode"), _("size"), _("name"));
    
    for (i=0; i < n_tuple_size(fl); i++) {
        struct pkgfl_ent    *flent = n_tuple_nth(fl, i);
        char                tmpbuf[PATH_MAX];
        
        for (j=0; j < flent->items; j++) {
            struct flfile *f = flent->files[j];
            char buf[1024], *slash = "";
            int n;
            
                
            if (S_ISDIR(f->mode)) {
                struct pkgfl_ent tmpent;

                if (*flent->dirname != '/')
                    slash = "/";
                snprintf(tmpbuf, sizeof(tmpbuf),
                                "%s/%s", flent->dirname, f->basename);
                tmpent.dirname = tmpbuf;
                //if (n_array_bsearch(fl, &tmpent))
                //    continue;
            }
            
            n = n_snprintf(buf, sizeof(buf), "%s%s%s%s%s",
                         *flent->dirname == '/' ? "":"/",
                         flent->dirname,
                         *flent->dirname == '/' ? "":"/",
                         f->basename, slash);
            
            if (S_ISLNK(f->mode)) 
                n += n_snprintf(&buf[n], sizeof(buf) - n, " -> %s",
                              f->basename + strlen(f->basename) + 1);
            
            if (mode_octal) {
                cmdctx_printf(cmdctx, "%6o%10d\t%s\n", f->mode, f->size, buf);
                
            } else {
                char s[12];
                mode_t_to_str(s, sizeof(s), f->mode);
                cmdctx_printf(cmdctx, "%10s%10d\t%s\n", s, f->size, buf);
            }
        }
    }
}


static void list_files(struct cmdctx *cmdctx, tn_tuple *fl, int term_width) 
{
    int i, j, ncol = 0;

    for (i=0; i < n_tuple_size(fl); i++) {
        struct pkgfl_ent    *flent = n_tuple_nth(fl, i);
        char                tmpbuf[PATH_MAX];
        int                 dn_printed = 0;
        
        for (j=0; j<flent->items; j++) {
            struct flfile *f = flent->files[j];
            char buf[1024], *slash = "";
            int n;
            
                
            if (S_ISDIR(f->mode)) {
                struct pkgfl_ent tmpent;

                slash = "/";
                snprintf(tmpbuf, sizeof(tmpbuf),
                                "%s/%s", flent->dirname, f->basename);
                tmpent.dirname = tmpbuf;
                if (n_tuple_bsearch_ex(fl, &tmpent, (tn_fn_cmp)pkgfl_ent_cmp))
                    continue;
            }
            
            if (!dn_printed) {
                ncol = cmdctx_printf_c(cmdctx, PRCOLOR_BLUE | PRAT_BOLD,
                                       "%s%s:  ",
                                       *flent->dirname == '/' ? "":"/",
                                       flent->dirname);
                dn_printed = 1;
            }
            

            n = n_snprintf(buf, sizeof(buf), "%s%s", f->basename, slash);
            
            if (S_ISLNK(f->mode)) 
                n += n_snprintf(&buf[n], sizeof(buf) - n, " -> %s",
                              f->basename + strlen(f->basename) + 1);
            
            if (ncol + n >= term_width) {
                ncol = SUBIDENT;
                nlident(cmdctx, ncol);
            }
            
            ncol += cmdctx_printf(cmdctx, "%s%s", buf, j + 1 < flent->items ? ", " : "");
        }
        
        if (dn_printed)
            cmdctx_printf(cmdctx, "\n");
    }
}


static void show_files(struct cmdctx *cmdctx, struct pkg *pkg, int longfmt, int term_width) 
{
    struct pkgflist *flist;
    
    if ((flist = pkg_get_flist(pkg)) == NULL)
        return;
    
    if (longfmt)
        list_files_long(cmdctx, flist->fl, 0);
    else
        list_files(cmdctx, flist->fl, term_width);

    pkgflist_free(flist);
}

static void show_pkg(struct cmdctx *cmdctx, struct pkg *pkg, unsigned flags, int term_width)
{
    if (flags & OPT_DESC_CAPS)
        show_caps(cmdctx, pkg, term_width);

    if (flags & OPT_DESC_REQS) {
        show_reqs(cmdctx, pkg, term_width);
        show_suggests(cmdctx, pkg, term_width);
    }

    if (flags & OPT_DESC_REQDIRS)
        show_reqdirs(cmdctx, pkg, term_width);
    
    if (flags & OPT_DESC_REQPKGS) 
        show_reqpkgs(cmdctx, pkg, term_width);

    if (flags & OPT_DESC_REVREQPKGS) 
        show_revreqpkgs(cmdctx, pkg, term_width);
            
    if (flags & OPT_DESC_CNFLS)
        show_cnfls(cmdctx, pkg, term_width);
}



static void show_description(struct cmdctx *cmdctx, struct pkg *pkg,
                             unsigned flags, int term_width) 
{
    struct pkguinf  *pkgu;
    char            fnbuf[PATH_MAX], *fn;
    char            unit = 'K';
    const char      *group, *s;
    double          pkgsize;
    
    if ((pkgu = pkg_uinf(pkg)) == NULL && poldek_verbose() > 1) {
        log(LOGWARN, _("%s: full description unavailable (index without "
                       "packages info loaded?)\n"), pkg_snprintf_s(pkg));
    }
    
    if (pkgu && (s = pkguinf_get(pkgu, PKGUINF_SUMMARY))) {
        cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "Summary:");
        cmdctx_printf(cmdctx, "%s\n", s);
    }

    if ((group = pkg_group(pkg))) {
        cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "Group:");
        cmdctx_printf(cmdctx, "%s\n", group);
    }

    if (pkgu && (s = pkguinf_get(pkgu, PKGUINF_VENDOR))) {
        cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "Vendor:");
        cmdctx_printf(cmdctx, "%s\n", s);
    }

    if (pkgu && (s = pkguinf_get(pkgu, PKGUINF_LICENSE))) {
        cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "License:");
        cmdctx_printf(cmdctx, "%s\n", s);
    }

    if (pkg->_arch) {
        char label[256];
        int n;
        
        n = n_snprintf(label, sizeof(label), "Arch");
            
        if (pkg->_os)
            n += n_snprintf(&label[n], sizeof(label) - n, "/OS");
                
        if (pkg->color)
            n += n_snprintf(&label[n], sizeof(label) - n, "/Color");

        n += n_snprintf(&label[n], sizeof(label) - n, ":");

        cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", label);
        cmdctx_printf(cmdctx, "%s", pkg_arch(pkg));
            
        if (pkg->_os) 
            cmdctx_printf(cmdctx, "/%s", pkg_os(pkg));

        if (pkg->color)
            cmdctx_printf(cmdctx, "/%d", pkg->color);
        cmdctx_printf(cmdctx, "\n");
    }
        
    if (pkgu && (s = pkguinf_get(pkgu, PKGUINF_URL))) {
        cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "URL:");
        cmdctx_printf(cmdctx, "%s\n", s);
    }
        	
    if (pkg->btime) {
        char timbuf[30];
        
        pkg_strbtime(timbuf, sizeof(timbuf), pkg);
        
        if (*timbuf) {
            cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "Built:");
            cmdctx_printf(cmdctx, "%s", timbuf);
            if (pkgu && (s = pkguinf_get(pkgu, PKGUINF_BUILDHOST))) 
                cmdctx_printf(cmdctx, " at %s", s);
            cmdctx_printf(cmdctx, "\n");
        }
    }

    if (pkg->itime) {
        char timbuf[30];
        
        pkg_stritime(timbuf, sizeof(timbuf), pkg);
        
        if (*timbuf) {
            cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "Installed:");
            cmdctx_printf(cmdctx, "%s\n", timbuf);
        }
    }
    

    pkgsize = pkg->size/1024;
    if (pkgsize >= 1024) {
        pkgsize /= 1024;
        unit = 'M';
    }

    cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "Size:");
    cmdctx_printf(cmdctx, "%.1f %cB (%d B)\n", pkgsize, unit, pkg->size);

    unit = 'K';
    if (pkg->fsize > 0) {
        pkgsize = pkg->fsize/1024;
        if (pkgsize >= 1024) {
            pkgsize /= 1024;
            unit = 'M';
        }
        cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "Package size:");
        cmdctx_printf(cmdctx, "%.1f %cB (%d B)\n", pkgsize, unit, pkg->fsize);
    }
    
    if (pkg_pkgdirpath(pkg)) {
        cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "Path:");
        cmdctx_printf(cmdctx, "%s\n", pkg_pkgdirpath(pkg));
    }

    if ((fn = pkg_filename(pkg, fnbuf, sizeof(fnbuf)))) {
        cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "File:");
        cmdctx_printf(cmdctx, "%s\n", fn);
    }
        
    if (pkg->epoch) {
        cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "%-16s", "Epoch:");
        cmdctx_printf(cmdctx, "%d\n", pkg->epoch);
    }

    show_pkg(cmdctx, pkg, flags, term_width);
        
    if (pkgu && (s = pkguinf_get(pkgu, PKGUINF_DESCRIPTION))) {
        cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "Description:\n");
        cmdctx_printf(cmdctx, "%s\n", s);
    }

    if (pkgu)
        pkguinf_free(pkgu);
}



static int desc(struct cmdctx *cmdctx)
{
    tn_array               *pkgs = NULL;
    int                    i, err = 0, term_width;

    poclidek_load_packages(cmdctx->cctx, POCLIDEK_LOAD_ALL);
    pkgs = poclidek_resolve_packages(NULL, cmdctx->cctx, cmdctx->ts, 0);
    if (pkgs == NULL) {
        err++;
        goto l_end;
    }

    if (cmdctx->_flags == 0) 
        cmdctx->_flags = OPT_DESC_DESCR;

    term_width = poldek_term_get_width() - RMARGIN;
    if (cmdctx->pipe_right)
        term_width = INT_MAX;

    if (term_width < 50)
        term_width = 79 - RMARGIN;
    
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg;

        pkg = n_array_nth(pkgs, i);

        cmdctx_printf(cmdctx, "\n");
        cmdctx_printf_c(cmdctx, PRCOLOR_YELLOW, "%-16s", "Package:");
        cmdctx_printf(cmdctx, "%s\n", pkg_id(pkg));
        
        if (cmdctx->_flags & OPT_DESC_DESCR) 
            show_description(cmdctx, pkg, cmdctx->_flags, term_width);
        
        else 
            show_pkg(cmdctx, pkg, cmdctx->_flags, term_width);

        if (cmdctx->_flags & OPT_DESC_FL) {
            if (n_array_size(pkgs) > 1)
                cmdctx_printf_c(cmdctx, PRCOLOR_CYAN, "Content:\n");
            show_files(cmdctx, pkg, cmdctx->_flags & OPT_DESC_FL_LONGFMT, term_width);
        }
        
        if (sigint_reached()) 
            goto l_end;
    }
    
 l_end:
    
    if (pkgs)
        n_array_free(pkgs);

    return err == 0;
}


