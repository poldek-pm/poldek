/* 
  Copyright (C) 2000, 2001 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <string.h>
#include <time.h>

#include "shell.h"


static int ls(struct cmdarg *cmdarg);
static int do_ls(tn_array *shpkgs, struct cmdarg *cmdarg);
static error_t parse_opt(int key, char *arg, struct argp_state *state);


#define OPT_LS_LONG            (1 << 0) /* cmd_state->flags */
#define OPT_LS_UPGRADEABLE     (1 << 1) /* cmd_state->flags */
#define OPT_LS_UPGRADEABLE_VER (1 << 2) /* cmd_state->flags */
#define OPT_LS_INSTALLED       (1 << 3) /* cmd_state->flags */
#define OPT_LS_SORTBUILDTIME   (1 << 4) /* cmd_state->flags */
#define OPT_LS_SORTBUILDAY     (1 << 5) /* cmd_state->flags */
#define OPT_LS_SORTREV         (1 << 6) /* cmd_state->flags */
#define OPT_LS_ERR             (1 << 10);

static struct argp_option options[] = {
 { "long", 'l', 0, 0, "Use a long listing format", 1},
 { "upgradeable", 'u', 0, 0, "Show upgradeable packages only", 1},
 { "upgradeablev", 'U', 0, 0, "Like above but omit packages with diffrent releases only", 1},
 { "installed", 'I', 0, 0, "List installed packages", 1},
 { NULL, 't', 0, 0, "Sort by build time", 1},
 { NULL, 'T', 0, 0, "Sort by build day", 1},
 { NULL, 'h', 0, OPTION_HIDDEN, "", 1 }, 
 { "reverse", 'r', 0, 0, "Reverse order while sorting", 1},
// { NULL, 'i', 0, OPTION_ALIAS, 0, 1 }, 
 { 0, 0, 0, 0, 0, 0 },
};

struct command command_ls;

static
struct command_alias cmd_aliases[] = {
    {
        "ll", "ls -l", &command_ls,
    },

    {
        "llu", "ls -lu", &command_ls,
    },

    {
        "llU", "ls -lU", &command_ls,
    },

    {
        "lli", "ls -lI", &command_ls,
    },

    {
        NULL, NULL, NULL
    },
};



struct command command_ls = {
    COMMAND_EMPTYARGS, 
    "ls", "[PACKAGE...]", "List packages", 
    options, parse_opt, NULL, ls,
    NULL, NULL,
    (struct command_alias*)&cmd_aliases, NULL
};



static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdarg *cmdarg = state->input;

    arg = arg;
    
    switch (key) {
        case 'l':
            cmdarg->flags |= OPT_LS_LONG;
            break;

        case 't':
            cmdarg->flags |= OPT_LS_SORTBUILDTIME;
            break;

        case 'T':
            cmdarg->flags |= OPT_LS_SORTBUILDAY;
            break;

        case 'r':
            cmdarg->flags |= OPT_LS_SORTREV;
            break;
            
        case 'U':
            cmdarg->flags |= OPT_LS_UPGRADEABLE_VER;
                                /* no break */
        case 'u':
            cmdarg->flags |= OPT_LS_UPGRADEABLE;
            
            if (cmdarg->sh_s->instpkgs == NULL) {
                log(LOGERR, "ls: installed packages not loaded, "
                    "type \"reload\" to load them\n");
                return EINVAL;
            }
            break;

        case 'I':
            if (cmdarg->sh_s->instpkgs == NULL) {
                log(LOGERR, "ls: installed packages not loaded, "
                    "type \"reload\" to load them\n");
                return EINVAL;
            }
            
            cmdarg->flags |= OPT_LS_INSTALLED;
            break;
            
        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}


static int find_pkg(struct shpkg *lshpkg, tn_array *shpkgs, int compare_ver, 
                    int *cmprc, char *evr, size_t size) 
{
    struct shpkg *shpkg = NULL;
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


static int ls(struct cmdarg *cmdarg) 
{
    tn_array             *shpkgs = NULL, *av_shpkgs;
    int                  rc;
    
    
    if (cmdarg->flags & OPT_LS_INSTALLED && cmdarg->sh_s->instpkgs) 
        av_shpkgs = cmdarg->sh_s->instpkgs;
    else
        av_shpkgs = cmdarg->sh_s->avpkgs;
    
    if (n_array_size(cmdarg->pkgnames)) 
        sh_resolve_packages(cmdarg->pkgnames, av_shpkgs, &shpkgs, 0);
    else 
        shpkgs = av_shpkgs;

    n_array_free(cmdarg->pkgnames);
    cmdarg->pkgnames = NULL;
    
    rc = do_ls(shpkgs, cmdarg);
    
    if (shpkgs && shpkgs != av_shpkgs)
        n_array_free(shpkgs);
    
    if (av_shpkgs == cmdarg->sh_s->avpkgs)
        n_array_sort(av_shpkgs);
    
    return rc;
}


static int do_ls(tn_array *shpkgs, struct cmdarg *cmdarg)
{
    char                 hdr[256], fmt_hdr[256], fmt_pkg[256];
    int                  i, size, err = 0, npkgs = 0;
    int                  compare_ver = 0;
    int                  term_width, term_width_div2;
    unsigned             flags;
    tn_fn_cmp            cmpf = NULL;

    if (n_array_size(shpkgs) == 0) 
        return 0;
    
    flags = cmdarg->flags;
    term_width = get_term_width();
    term_width_div2 = term_width/2;

    *hdr = '\0';
    if (flags & OPT_LS_LONG) {
        if ((flags & OPT_LS_UPGRADEABLE) == 0) {
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
            
            if (flags & OPT_LS_INSTALLED) 
                snprintf(hdr, sizeof(hdr), fmt_hdr, "installed",
                         "available", "build date", "size");
            else
                snprintf(hdr, sizeof(hdr), fmt_hdr, "available",
                         "installed", "build date", "size");
        }
    }
    
    hdr[sizeof(hdr) - 2] = '\n';
    compare_ver = flags & OPT_LS_UPGRADEABLE_VER;
    
    if (flags & (OPT_LS_SORTBUILDTIME | OPT_LS_SORTBUILDAY)) {
        cmpf = (tn_fn_cmp)shpkg_cmp_btime;
        
        if (flags & OPT_LS_SORTREV)
            cmpf = (tn_fn_cmp)shpkg_cmp_btime_rev;
        
        if (flags & OPT_LS_SORTBUILDAY) {
            cmpf = (tn_fn_cmp)shpkg_cmp_bday;

            if (flags & OPT_LS_SORTREV)
                cmpf = (tn_fn_cmp)shpkg_cmp_bday_rev;
        }
        
    } else if (flags & OPT_LS_SORTREV) {
        cmpf = (tn_fn_cmp)shpkg_cmp_rev;
    }

    if (cmpf) 
        n_array_sort_ex(shpkgs, cmpf);
    
    size = 0;
    for (i=0; i<n_array_size(shpkgs); i++) {
        struct shpkg *shpkg = n_array_nth(shpkgs, i);
        struct pkg *pkg = shpkg->pkg;
        char evr[128]; 
        int cmprc = 0;
        
        
        if (flags & OPT_LS_UPGRADEABLE && cmdarg->sh_s->instpkgs) {
            int finded;
            
            if (flags & OPT_LS_INSTALLED) {
                finded = find_pkg(shpkg, cmdarg->sh_s->avpkgs, compare_ver, 
                                  &cmprc, evr, sizeof(evr));
                
            } else {
                finded = find_pkg(shpkg, cmdarg->sh_s->instpkgs, compare_ver,
                                  &cmprc, evr, sizeof(evr));
                cmprc = -cmprc;
            }
            
            if (!finded || cmprc >= 0)
                continue;
        }
        
        if (npkgs == 0)
            printf_c(PRCOLOR_YELLOW, "%s", hdr);
        
        if (flags & OPT_LS_LONG) {
            char timbuf[30];
            char sizbuf[30];
            char unit = 'k';
            double pkgsize = pkg->size/1024;

            if (pkgsize > 1000) {
                pkgsize /= 1024;
                unit = 'm';
            }

            snprintf(sizbuf, sizeof(sizbuf), "%.1f%c", pkgsize, unit);
            
            if (pkg->btime)
                strftime(timbuf, sizeof(timbuf), "%Y/%m/%d %H:%M",
                         localtime((time_t*)&pkg->btime));
            else
                *timbuf = '\0';
            
            if ((flags & OPT_LS_UPGRADEABLE) == 0) {
                printf(fmt_pkg, shpkg->nevr, timbuf, sizbuf);
                
            } else {
#if 0                
                char buf[255];
                int n;
                
                n = snprintf(buf, sizeof(buf), "%s-", shpkg->pkg->name);
                
                n += snprintf_c(PRCOLOR_YELLOW, &buf[n], sizeof(buf) - n, "%s",
                           shpkg->pkg->ver, shpkg->pkg->rel);

                n += snprintf(&buf[n], sizeof(buf) - n, "-");

                n += snprintf_c(PRCOLOR_CYAN, &buf[n], sizeof(buf) - n, "%s",
                           shpkg->pkg->rel);
#endif
                
                printf(fmt_pkg, shpkg->nevr, evr, timbuf, sizbuf);
            }
            size += pkg->size/1024;
            
        } else {
            printf("%s\n", shpkg->nevr);
        }
        npkgs++;
    }
    

    if (flags & OPT_LS_LONG && n_array_size(shpkgs)) {
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
            printf_c(PRCOLOR_YELLOW, "%d packages, %d %s\n", npkgs, val, unit);
    }
    
    return err == 0;
}


