/* 
  Copyright (C) 2000, 2001 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <time.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>

#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgset.h"
#include "pkgset.h"
#include "misc.h"
#include "pkgset-req.h"
#include "sigint/sigint.h"

#include "shell.h"
#include "sigint/sigint.h"

#define IDENT     16
#define SUBIDENT  4
#define RMARGIN   2


static error_t parse_opt(int key, char *arg, struct argp_state *state);
static int desc(struct cmdarg *cmdarg);

#define OPT_DESC_CAPS         (1 << 0)
#define OPT_DESC_REQS         (1 << 1)
#define OPT_DESC_REQPKGS      (1 << 2)
#define OPT_DESC_REVREQPKGS   (1 << 3)
#define OPT_DESC_CNFLS        (1 << 4)
#define OPT_DESC_DESCR        (1 << 5)
#define OPT_DESC_FL           (1 << 6)
#define OPT_DESC_FL_LONGFMT   (1 << 10)
#define OPT_DESC_ALL          (OPT_DESC_CAPS | OPT_DESC_REQS |          \
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
    {NULL, 'h', 0, OPTION_HIDDEN, "", 1 },
    { 0, 0, 0, 0, 0, 0 },
};


struct command command_desc = {
    0, 
    "desc", N_("PACKAGE..."), N_("Display packages info"), 
    options, parse_opt,
    NULL, desc,
    NULL, NULL, 
    NULL, NULL
};

static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdarg *cmdarg = state->input;

    arg = arg;
    
    switch (key) {
        case 'a':
            cmdarg->flags |= OPT_DESC_ALL;
            break;
            
        case 'C':
            cmdarg->flags |= OPT_DESC_CAPS | OPT_DESC_REQS | OPT_DESC_CNFLS;
            break;
            

        case 'c':
            cmdarg->flags |= OPT_DESC_CNFLS;
            break;

        case 'p':
            cmdarg->flags |= OPT_DESC_CAPS;
            break;

        case 'r':
            cmdarg->flags |= OPT_DESC_REQS;
            break;
            
        case 'R':
            cmdarg->flags |= OPT_DESC_REQPKGS;
            break;

        case 'B':
            cmdarg->flags |= OPT_DESC_REVREQPKGS;
            break;

        case 'f':
        case 'l':
            if (cmdarg->flags & OPT_DESC_FL)
                cmdarg->flags |= OPT_DESC_FL_LONGFMT;
            else
                cmdarg->flags |= OPT_DESC_FL;
            break;
            
        case 'd':
            cmdarg->flags |= OPT_DESC_DESCR;
            break;
            
        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int nlident(int width) 
{
    char fmt[64];

    snprintf(fmt, sizeof(fmt), "\n%%%dc", width);
    return printf(fmt, ' ');
}


static void show_caps(struct pkg *pkg)
{
    int i, ncol = IDENT;
    int term_width;
    char *p, *colon = ", ";
    

    term_width = term_get_width() - RMARGIN;
    
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
                printf_c(PRCOLOR_CYAN, "%-16s", "Provides:");
                hdr_printed = 1;
            }
            	
            p = capreq_snprintf_s(cr);
            if (ncol + (int)strlen(p) >= term_width) {
                ncol = SUBIDENT;
                nlident(ncol);
            }
            
            if (--ncaps == 0)
                colon = "";
            
            ncol += printf("%s%s", p, colon);
        }
        
        if (hdr_printed)
            printf("\n");
    }
}


static void show_reqs(struct pkg *pkg)
{
    int ncol = IDENT, nrpmreqs = 0, nreqs = 0, nprereqs = 0;
    int term_width;
    int i;

    if (pkg->reqs == NULL || n_array_size(pkg->reqs) == 0)
        return;
    
    term_width = term_get_width() - RMARGIN;

    for (i=0; i<n_array_size(pkg->reqs); i++) {
        struct capreq *cr = n_array_nth(pkg->reqs, i);

        if (pkg_eq_capreq(pkg, cr))
            continue;
        
        if (capreq_is_rpmlib(cr))
            nrpmreqs++;
        else if (capreq_is_prereq(cr))
            nprereqs++;
        else 
            nreqs++;
    }


    if (nprereqs) {
        char *p, *colon = ", ";
        int n = 0;

        ncol = IDENT;
        printf_c(PRCOLOR_CYAN, "%-16s", "PreReqs:");
        for (i=0; i<n_array_size(pkg->reqs); i++) {
            struct capreq *cr = n_array_nth(pkg->reqs, i);
                
            if (pkg_eq_capreq(pkg, cr))
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
                nlident(ncol);
            }
            ncol += printf("%s%s", p, colon);
        }
        printf("\n");
    }

        
    if (nreqs) {
        char *p, *colon = ", ";
        int n = 0;

        ncol = IDENT;
        printf_c(PRCOLOR_CYAN, "%-16s", "Requires:");
        for (i=0; i<n_array_size(pkg->reqs); i++) {
            struct capreq *cr = n_array_nth(pkg->reqs, i);
                
            if (pkg_eq_capreq(pkg, cr))
                continue;
                
            if (capreq_is_rpmlib(cr))
                continue;

            if (capreq_is_prereq(cr))
                continue;
            
            if (++n == nreqs)
                colon = "";
                
            p = capreq_snprintf_s(cr);
            if (ncol + (int)strlen(p) >= term_width) {
                ncol = SUBIDENT;
                nlident(ncol);
            }
            ncol += printf("%s%s", p, colon);
        }
        printf("\n");
    }
                

    if (nrpmreqs) {
        char *p, *colon = ", ";
        int n = 0;

        ncol = IDENT;
        printf_c(PRCOLOR_CYAN, "%-16s", "RPMReqs:");
        for (i=0; i<n_array_size(pkg->reqs); i++) {
            struct capreq *cr = n_array_nth(pkg->reqs, i);

            if (!capreq_is_rpmlib(cr))
                continue;
                
            if (++n == nrpmreqs)
                colon = "";
                
            p = capreq_snprintf_s(cr);
            if (ncol + (int)strlen(p) >= term_width) {
                ncol = SUBIDENT;
                nlident(ncol);
            }
            ncol += printf("%s%s", p, colon);
        }
        printf("\n");
    }

}

static void show_reqpkgs(struct pkg *pkg)
{
    int i, term_width;
    
    
    term_width = term_get_width() - RMARGIN;

    if (pkg->reqpkgs && n_array_size(pkg->reqpkgs)) {
        int ncol = IDENT;
        
        printf_c(PRCOLOR_CYAN, "%-16s", "Reqpkgs:");

        for (i=0; i<n_array_size(pkg->reqpkgs); i++) {
            struct reqpkg *rp;	
            char *p;

            
            rp = n_array_nth(pkg->reqpkgs, i);

            p = rp->pkg->name;
            if (ncol + (int)strlen(p) >= term_width) {
                ncol = SUBIDENT;
                nlident(ncol);
            }
            ncol += printf("%s", p);
            
            if (rp->flags & REQPKG_MULTI) {
                int n = 0;

                ncol += printf(" | ");
                
                while (rp->adds[n]) {
                    char *p = rp->adds[n++]->pkg->name;
                    if (ncol + (int)strlen(p) >= term_width) {
                        ncol = SUBIDENT;
                        nlident(ncol);
                    }
                    ncol += printf("%s", p);
                    if (rp->adds[n] != NULL) {
                        ncol += printf(" | ");
                    }
                }
            }
            if (i + 1 < n_array_size(pkg->reqpkgs))
                ncol += printf(" ");
        }
        printf("\n");
    }
}

static
void show_revreqpkgs(struct pkg *pkg)
{
    int i, term_width;
    
    
    term_width = term_get_width() - RMARGIN;

    if (pkg->revreqpkgs && n_array_size(pkg->revreqpkgs)) {
        int ncol = IDENT;
        
        printf_c(PRCOLOR_CYAN, "%-16s", "RequiredBy:");

        for (i=0; i<n_array_size(pkg->revreqpkgs); i++) {
            struct pkg *tmpkg;	
            char *p, *colon = ", ";

            
            tmpkg = n_array_nth(pkg->revreqpkgs, i);
            
            p = tmpkg->name;
            if (ncol + (int)strlen(p) + 2 >= term_width) {
                ncol = SUBIDENT;
                nlident(ncol);
            }
            if (i + 1 == n_array_size(pkg->revreqpkgs))
                colon = "";
                    
            ncol += printf("%s%s", p, colon);
        }
        printf("\n");
    }
}


static void show_cnfls(struct pkg *pkg)
{
    int i, ncol = 0;
    int term_width;

    term_width = term_get_width() - RMARGIN;
    if (pkg->cnfls && n_array_size(pkg->cnfls)) {
        int nobsls = 0;
        
        for (i=0; i<n_array_size(pkg->cnfls); i++) {
            struct capreq *cr = n_array_nth(pkg->cnfls, i);
            if (cnfl_is_obsl(cr))
                nobsls++;
        }
        
        if (nobsls != n_array_size(pkg->cnfls)) {
            int n = 0;
            
            ncol = printf_c(PRCOLOR_CYAN, "%-16s", "Conflicts:");
            for (i=0; i<n_array_size(pkg->cnfls); i++) {
                struct capreq *cr = n_array_nth(pkg->cnfls, i);
                
                if (cnfl_is_obsl(cr))
                    continue;
                n++;
                ncol += printf(capreq_snprintf_s(cr));
                if (n < n_array_size(pkg->cnfls) - nobsls)
                    ncol += printf(", ");

                if (ncol >= term_width) {
                    ncol = SUBIDENT;
                    nlident(ncol);
                }
            }
            printf("\n");
        }
        
        if (nobsls) {
            int n = 0;
            
            ncol = printf_c(PRCOLOR_CYAN, "%-16s", "Obsoletes:");
            for (i=0; i<n_array_size(pkg->cnfls); i++) {
                struct capreq *cr = n_array_nth(pkg->cnfls, i);
                char s[255], slen;
                
                if (!cnfl_is_obsl(cr))
                    continue;
                n++;

                slen = capreq_snprintf(s, sizeof(s), cr);
                //printf("[%d] %d; %d, %d, %s\n", term_width,
                //       ncol, n, strlen(name), name);
                //continue;
                if (ncol + slen + 2 >= term_width) {
                    ncol = SUBIDENT;
                    nlident(ncol);
                }
                ncol += printf("%s%s", s, n < nobsls ? ", " : "");
            }
            printf("\n");
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



    
static void list_files_long(tn_array *fl, int mode_octal)
{
    int i, j;
    const char *fmt = "%-10s%10s\t%s\n";


    if (mode_octal) 
        fmt = "%-6s%10s\t%s\n";
        
    printf_c(PRCOLOR_YELLOW, fmt, _("mode"), _("size"), _("name"));
    
    for (i=0; i < n_array_size(fl); i++) {
        struct pkgfl_ent    *flent;
        char                tmpbuf[PATH_MAX];
        
        
        flent = n_array_nth(fl, i);
        
        for (j=0; j < flent->items; j++) {
            struct flfile *f = flent->files[j];
            char buf[1024], *slash = "";
            int n;
            
                
            if (S_ISDIR(f->mode)) {
                struct pkgfl_ent tmpent;

                if (*flent->dirname != '/')
                    slash = "/";
                snprintf(tmpbuf, sizeof(tmpbuf), "%s/%s", flent->dirname,
                         f->basename);
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
                printf("%6o%10d\t%s\n", f->mode, f->size, buf);
                
            } else {
                char s[12];
                mode_t_to_str(s, sizeof(s), f->mode);
                printf("%10s%10d\t%s\n", s, f->size, buf);
            }
        }
    }
}


static void list_files(tn_array *fl, int term_width) 
{
    int i, j, ncol = 0;

    for (i=0; i<n_array_size(fl); i++) {
        struct pkgfl_ent    *flent;
        char                tmpbuf[PATH_MAX];
        int                 dn_printed = 0;

        
        flent = n_array_nth(fl, i);
        
        for (j=0; j<flent->items; j++) {
            struct flfile *f = flent->files[j];
            char buf[1024], *slash = "";
            int n;
            
                
            if (S_ISDIR(f->mode)) {
                struct pkgfl_ent tmpent;

                slash = "/";
                snprintf(tmpbuf, sizeof(tmpbuf), "%s/%s", flent->dirname,
                         f->basename);
                tmpent.dirname = tmpbuf;
                if (n_array_bsearch(fl, &tmpent))
                    continue;
            }
            
            if (!dn_printed) {
                ncol = printf_c(PRCOLOR_BLUE | PRAT_BOLD, "%s%s:  ",
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
                nlident(ncol);
            }
            
            ncol += printf("%s%s", buf, j + 1 < flent->items ? ", " : "");
        }
        
        if (dn_printed)
            printf("\n");
    }
}


static void show_files(struct pkg *pkg, int longfmt) 
{
    tn_array *fl;
    int term_width;
    void *flmark;
    
    if ((fl = pkg_info_get_fl(pkg)) == NULL || n_array_size(fl) == 0)
        return;

    flmark = pkgflmodule_allocator_push_mark();
    term_width = term_get_width() - RMARGIN;
    
    if (longfmt)
        list_files_long(fl, 0);
    else
        list_files(fl, term_width);

    pkg_info_free_fl(pkg, fl);
    pkgflmodule_allocator_pop_mark(flmark);
}

static void show_pkg(struct pkg *pkg, unsigned flags)
{
    if (flags & OPT_DESC_CAPS)
        show_caps(pkg);

    if (flags & OPT_DESC_REQS)
        show_reqs(pkg);
            
    if (flags & OPT_DESC_REQPKGS) 
        show_reqpkgs(pkg);

    if (flags & OPT_DESC_REVREQPKGS) 
        show_revreqpkgs(pkg);
            
    if (flags & OPT_DESC_CNFLS)
        show_cnfls(pkg);
}



static void show_description(struct pkg *pkg, unsigned flags) 
{
    struct pkguinf  *pkgu;
    char            timbuf[30];
    char            unit = 'K';
    const char      *group;
    double          pkgsize;

    
    if ((pkgu = pkg_info(pkg)) == NULL) {
        log(LOGERR, _("%s: description unavailable (index without packages "
                      "info loaded?)\n"), pkg_snprintf_s(pkg));
        return;
    }
        

    if (pkg->btime) 
        strftime(timbuf, sizeof(timbuf), "%Y/%m/%d %H:%M",
                 localtime((time_t*)&pkg->btime));
    else
        *timbuf = '\0';

    if (pkgu->summary) {
        printf_c(PRCOLOR_CYAN, "%-16s", "Summary:");
        printf("%s\n", pkgu->summary);
    }

    if ((group = pkg_group(pkg))) {
        printf_c(PRCOLOR_CYAN, "%-16s", "Group:");
        printf("%s\n", group);
    }

    if (pkgu->vendor) {
        printf_c(PRCOLOR_CYAN, "%-16s", "Vendor:");
        printf("%s\n", pkgu->vendor);
    }

    if (pkgu->license) {
        printf_c(PRCOLOR_CYAN, "%-16s", "License:");
        printf("%s\n", pkgu->license);
    }

    if (pkg->arch) {
        char *p = "Arch:";
        if (pkg->os) 
            p = "Arch/OS:";

        printf_c(PRCOLOR_CYAN, "%-16s", p);
        printf("%s", pkg->arch);
            
        if (pkg->os) 
            printf("/%s", pkg->os);
        printf("\n");
    }
        
        
    if (pkgu->url) {
        printf_c(PRCOLOR_CYAN, "%-16s", "URL:");
        printf("%s\n", pkgu->url);
    }
        	
        
    if (*timbuf) {
        printf_c(PRCOLOR_CYAN, "%-16s", "Built:");
        printf("%s", timbuf);
        if (pkgu->buildhost) 
            printf(" at %s", pkgu->buildhost);
        printf("\n");
    }

    pkgsize = pkg->size/1024;
    if (pkgsize >= 1024) {
        pkgsize /= 1024;
        unit = 'M';
    }

    printf_c(PRCOLOR_CYAN, "%-16s", "Size:");
    printf("%.1f %cB (%d B)\n", pkgsize, unit, pkg->size);

    unit = 'K';
    if (pkg->fsize > 0) {
        pkgsize = pkg->fsize/1024;
        if (pkgsize >= 1024) {
            pkgsize /= 1024;
            unit = 'M';
        }
        printf_c(PRCOLOR_CYAN, "%-16s", "Package size:");
        printf("%.1f %cB (%d B)\n", pkgsize, unit, pkg->fsize);
    }
    
    if (pkg->pkgdir && pkg->pkgdir->path) {
        printf_c(PRCOLOR_CYAN, "%-16s", "Path:");
        printf("%s\n", pkg->pkgdir->path);
    }
        
    if (pkg->epoch) {
        printf_c(PRCOLOR_CYAN, "%-16s", "Epoch:");
        printf("%d\n", pkg->epoch);
    }

    show_pkg(pkg, flags);
        
    if (pkgu->description) {
        printf_c(PRCOLOR_CYAN, "Description:\n");
        printf("%s\n", pkgu->description);
    }
            
    pkguinf_free(pkgu);

}



static int desc(struct cmdarg *cmdarg)
{
    tn_array *shpkgs = NULL;
    int i, err = 0;


    sh_resolve_packages(cmdarg->pkgnames, cmdarg->sh_s->instpkgs, &shpkgs, 0);
    //sh_resolve_packages(cmdarg->pkgnames, cmdarg->sh_s->avpkgs, &shpkgs, 0);
    if (shpkgs == NULL)
        return 0;

    if (n_array_size(shpkgs) == 0) {
        err++;
        goto l_end;
    }

    if (cmdarg->flags == 0) 
        cmdarg->flags = OPT_DESC_DESCR;
    
    for (i=0; i<n_array_size(shpkgs); i++) {
        struct shpkg *shpkg;

        shpkg = n_array_nth(shpkgs, i);

        if (n_array_size(shpkgs) > 1) {
            printf("\n");
            printf_c(PRCOLOR_YELLOW, "%-16s", "Package:");
            printf("%s\n", pkg_snprintf_s(shpkg->pkg));
        }
        
        if (cmdarg->flags & OPT_DESC_DESCR) 
            show_description(shpkg->pkg, cmdarg->flags);
        
        else 
            show_pkg(shpkg->pkg, cmdarg->flags);

        if (cmdarg->flags & OPT_DESC_FL) {
            if (n_array_size(shpkgs) > 1)
                printf_c(PRCOLOR_CYAN, "Content:\n");
            show_files(shpkg->pkg, cmdarg->flags & OPT_DESC_FL_LONGFMT);
        }
        
        if (sigint_reached()) 
            goto l_end;
    }
    
 l_end:
    
    if (shpkgs != cmdarg->sh_s->avpkgs)
        n_array_free(shpkgs);

    return err == 0;
}


