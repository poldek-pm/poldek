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

#include "i18n.h"

#include "shell.h"
#include "pager.h"


static int ls(struct cmdarg *cmdarg);
static
int do_ls(const tn_array *shpkgs, struct cmdarg *cmdarg, const tn_array *evrs);
static error_t parse_opt(int key, char *arg, struct argp_state *state);


/* cmd_state->flags */
#define OPT_LS_LONG            (1 << 0)
#define OPT_LS_UPGRADEABLE     (1 << 1) 
#define OPT_LS_UPGRADEABLE_VER (1 << 2)
#define OPT_LS_INSTALLED       (1 << 3)
#define OPT_LS_SORTBUILDTIME   (1 << 4)
#define OPT_LS_SORTBUILDAY     (1 << 5)
#define OPT_LS_SORTREV         (1 << 6)

#define OPT_LS_GROUP           (1 << 9)
#define OPT_LS_SUMMARY         (1 << 10)
#define OPT_LS_NAMES_ONLY      (1 << 11)


#define OPT_LS_ERR             (1 << 16);

static struct argp_option options[] = {
 { "long", 'l', 0, 0, N_("Use a long listing format"), 1},
 { "upgradeable", 'u', 0, 0, N_("Show upgradeable packages only"), 1},
 { "upgradeablev", 'U', 0, 0,
   N_("Like above but omit packages with diffrent releases only"), 1},
 { "installed", 'I', 0, 0, N_("List installed packages"), 1},
 { NULL, 't', 0, 0, N_("Sort by build time"), 1},
 { NULL, 'T', 0, 0, N_("Sort by build day"), 1},
 { NULL, 'h', 0, OPTION_HIDDEN, "", 1 }, 
 { "reverse", 'r', 0, 0, N_("Reverse order while sorting"), 1},
 { NULL, 'n', 0, 0, N_("Print only package's names"), 1},
 { NULL, 'G', 0, 0, N_("Print package's group"), 1},
 { NULL, 'O', 0, 0, N_("Print package's summary"), 1},
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
    "ls", N_("[PACKAGE...]"), N_("List packages"), 
    options, parse_opt, NULL, ls,
    NULL, NULL,
    (struct command_alias*)&cmd_aliases, NULL
};



static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdarg *cmdarg = state->input;
    const char *errmsg_excl = _("ls: -l and -G are exclusive");
    arg = arg;
    
    switch (key) {
        case 'l':
            if (cmdarg->flags & OPT_LS_GROUP) {
                logn(LOGERR, errmsg_excl);
                return EINVAL;
            }
            
            cmdarg->flags |= OPT_LS_LONG;
            break;

        case 'O':
            cmdarg->flags |= OPT_LS_SUMMARY;
            break;

        case 'G':
            if (cmdarg->flags & OPT_LS_LONG) {
                logn(LOGERR, errmsg_excl);
                return EINVAL;
            }

            cmdarg->flags |= OPT_LS_GROUP;
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
                log(LOGERR, _("ls: installed packages not loaded, "
                              "type \"reload\" to load them\n"));
                return EINVAL;
            }
            break;
            
        case 'I':
            if (cmdarg->sh_s->instpkgs == NULL) {
                log(LOGERR, _("ls: installed packages not loaded, "
                              "type \"reload\" to load them\n"));
                return EINVAL;
            }
            
            cmdarg->flags |= OPT_LS_INSTALLED;
            break;

        case 'n':
            cmdarg->flags |= OPT_LS_NAMES_ONLY;
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

static tn_fn_cmp select_cmpf(unsigned flags) 
{
    tn_fn_cmp cmpf = NULL;

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
    
    return cmpf;
}



static int ls(struct cmdarg *cmdarg) 
{
    tn_array             *ls_shpkgs = NULL, *av_shpkgs;
    tn_array             *evrs = NULL;
    int                  rc = 1, ls_all;
    tn_fn_cmp            cmpf;

    
    ls_all = 0;
    
    if (cmdarg->flags & OPT_LS_INSTALLED && cmdarg->sh_s->instpkgs) 
        av_shpkgs = cmdarg->sh_s->instpkgs;
    else
        av_shpkgs = cmdarg->sh_s->avpkgs;
    
    if (n_array_size(cmdarg->pkgnames)) 
        sh_resolve_packages(cmdarg->pkgnames, av_shpkgs, &ls_shpkgs, 0);
    else {
        ls_all = 1;
        ls_shpkgs = av_shpkgs;
    }
    
    n_array_free(cmdarg->pkgnames);
    cmdarg->pkgnames = NULL;


    if ((cmpf = select_cmpf(cmdarg->flags)))
        n_array_sort_ex(ls_shpkgs, cmpf);


    if (cmdarg->flags & OPT_LS_UPGRADEABLE && cmdarg->sh_s->instpkgs) {
        int        finded, compare_ver = 0, i;
        tn_array   *shpkgs_tmp;

        compare_ver = cmdarg->flags & OPT_LS_UPGRADEABLE_VER;
        shpkgs_tmp = n_array_new(64, NULL, (tn_fn_cmp)shpkg_cmp);
        evrs = n_array_new(64, NULL, NULL);
        
        for (i=0; i < n_array_size(ls_shpkgs); i++) {
            struct shpkg  *shpkg;
            char          evr[128], *p;
            int           cmprc = 0, n;

            shpkg = n_array_nth(ls_shpkgs, i);
            
            if (cmdarg->flags & OPT_LS_INSTALLED) {
                finded = find_pkg(shpkg, cmdarg->sh_s->avpkgs, compare_ver, 
                                  &cmprc, evr, sizeof(evr));
                
            } else {
                finded = find_pkg(shpkg, cmdarg->sh_s->instpkgs, compare_ver,
                                  &cmprc, evr, sizeof(evr));
                cmprc = -cmprc;
            }
            
            if (!finded || cmprc >= 0)
                continue;

            n = strlen(evr) + 1;
            p = alloca(n + 1);
            memcpy(p, evr, n);
            n_array_push(evrs, p);
            n_array_push(shpkgs_tmp, shpkg);
        }
        
        if (ls_shpkgs != av_shpkgs)
            n_array_free(ls_shpkgs);
        ls_shpkgs = shpkgs_tmp;
    }

    if (n_array_size(ls_shpkgs))
        rc = do_ls(ls_shpkgs, cmdarg, evrs);
    
    if (ls_shpkgs && ls_shpkgs != av_shpkgs)
        n_array_free(ls_shpkgs);

    if (evrs)
        n_array_free(evrs);
    
    if (ls_all && cmpf)
        n_array_sort(av_shpkgs);
    
    return rc;
}


static void ls_summary(FILE *stream, struct pkg *pkg)
{
    struct pkguinf  *pkgu;
    
    if ((pkgu = pkg_info(pkg)) && pkgu->summary)
        fprintf(stream, "    %s\n", pkgu->summary);

    if (pkgu)
        pkguinf_free(pkgu);
}


static
int do_ls(const tn_array *shpkgs, struct cmdarg *cmdarg, const tn_array *evrs)
{
    char                 hdr[256], fmt_hdr[256], fmt_pkg[256];
    int                  i, size, err = 0, npkgs = 0;
    int                  term_width, term_width_div2;
    unsigned             flags;
    struct pager         pg = { NULL, 0 };
    FILE                 *out_stream = stdout;
    
    if (n_array_size(shpkgs) == 0) 
        return 0;
    
    flags = cmdarg->flags;
    term_width = term_get_width();
    term_width_div2 = term_width/2;

    *hdr = '\0';

    if (flags & OPT_LS_GROUP) {
        snprintf(fmt_hdr, sizeof(fmt_hdr), "%%-%ds%%-%ds\n",
                 term_width_div2 + term_width_div2/10, (term_width/7));

        snprintf(fmt_pkg, sizeof(fmt_pkg), "%%-%ds%%-%ds\n",
                 term_width_div2 + term_width_div2/10, (term_width/7));
        
        snprintf(hdr, sizeof(hdr), fmt_hdr, _("package"), _("group"));

    } else if (flags & OPT_LS_LONG) {
        if ((flags & OPT_LS_UPGRADEABLE) == 0) {
            snprintf(fmt_hdr, sizeof(fmt_hdr), "%%-%ds%%-%ds %%%ds\n",
                     term_width_div2 + term_width_div2/10, (term_width/7),
                     (term_width/8) + 2);
            
            snprintf(fmt_pkg, sizeof(fmt_pkg), "%%-%ds%%%ds %%%ds\n",
                     term_width_div2 + term_width_div2/10, (term_width/7),
                     (term_width/8));
            snprintf(hdr, sizeof(hdr), fmt_hdr,
                     _("package"), _("build date"), _("size"));

            
        } else {
            snprintf(fmt_hdr, sizeof(fmt_hdr), "%%-%ds%%-%ds%%-%ds%%%ds\n",
                     (term_width/2) - 1, (term_width/6) - 1,
                     (term_width/6) - 1, (term_width/5) - 1);

            snprintf(fmt_pkg, sizeof(fmt_pkg), "%%-%ds%%-%ds%%-%ds%%%ds\n",
                     (term_width/2) - 1, (term_width/6) - 1,
                     (term_width/6) - 1, (term_width/6) - 1);
            
            if (flags & OPT_LS_INSTALLED) 
                snprintf(hdr, sizeof(hdr), fmt_hdr, _("installed"),
                         _("available"), _("build date"), _("size"));
            else
                snprintf(hdr, sizeof(hdr), fmt_hdr, _("available"),
                         _("installed"), _("build date"), _("size"));
        }
    }
    
    hdr[sizeof(hdr) - 2] = '\n';
    
    if (shOnTTY && term_get_height() < n_array_size(shpkgs)) {
        if ((out_stream = pager(&pg)) == NULL)
            out_stream = stdout;
    }
    
    size = 0;
    for (i=0; i < n_array_size(shpkgs); i++) {
        struct shpkg   *shpkg = n_array_nth(shpkgs, i);
        struct pkg     *pkg = shpkg->pkg;
        char           *pkg_name;
        
        if (flags & OPT_LS_NAMES_ONLY) 
            pkg_name = pkg->name;
        else
            pkg_name = shpkg->nevr;
        
        if (npkgs == 0)
            sh_printf_c(out_stream, PRCOLOR_YELLOW, "%s", hdr);
        
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
                fprintf(out_stream, fmt_pkg, pkg_name, timbuf, sizbuf);
                
            } else if (evrs) {
                const char *evr = n_array_nth(evrs, i);
                fprintf(out_stream, fmt_pkg, pkg_name, evr, timbuf, sizbuf);
            }
            size += pkg->size/1024;
            
        } else if (flags & OPT_LS_GROUP) {
            const char *group = pkg_group(pkg);
            fprintf(out_stream, fmt_pkg, pkg_name, group ? group : "(unset)");

        } else {
            fprintf(out_stream, "%s\n", pkg_name);
        }

        if (flags & OPT_LS_SUMMARY)
            ls_summary(out_stream, pkg);
        
        npkgs++;
    }
    
    if (npkgs) {
        if ((flags & OPT_LS_LONG) == 0 && out_stream != stdout) {
            sh_printf_c(out_stream, PRCOLOR_YELLOW, _("%d packages\n"), npkgs);
            
        } else if (flags & OPT_LS_LONG) {
            char *unit;
            int val;
        
            if (size > 1000) {
                unit = "MB";
                val = size/1000;
            } else {
                unit = "kB";
                val = size;
            }
            
            sh_printf_c(out_stream, PRCOLOR_YELLOW,
                        _("%d packages, %d %s\n"), npkgs, val, unit);
        }
    }

    if (out_stream != stdout)
        pager_close(&pg);
    
    return err == 0;
}


