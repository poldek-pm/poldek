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

#include "sigint/sigint.h"
#include "i18n.h"
#include "misc.h"
#include "pkgu.h"
#include "cli.h"
#include "pager.h"

static int ls(struct cmdarg *cmdarg);
static
int do_ls(const tn_array *ents, struct cmdarg *cmdarg, const tn_array *evrs);
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
 { "upgradeable", 'u', 0, 0, N_("Show upgrade-able packages only"), 1},
 { "upgradeablev", 'U', 0, 0,
   N_("Likewise but omit packages with different releases only"), 1},
 { "installed", 'I', 0, 0, N_("List installed packages"), 1},
 { NULL, 't', 0, 0, N_("Sort by build time"), 1},
 { NULL, 'T', 0, 0, N_("Sort by build day"), 1},
 { NULL, 'h', 0, OPTION_HIDDEN, "", 1 }, 
 { "reverse", 'r', 0, 0, N_("Reverse order while sorting"), 1},
 { NULL, 'n', 0, 0, N_("Print only package names"), 1},
 { NULL, 'G', 0, 0, N_("Print package groups"), 1},
 { NULL, 'O', 0, 0, N_("Print package summaries"), 1},
// { NULL, 'i', 0, OPTION_ALIAS, 0, 1 }, 
 { 0, 0, 0, 0, 0, 0 },
};

struct poclidek_cmd command_ls = {
    COMMAND_EMPTYARGS, 
    "ls", N_("[PACKAGE...]"), N_("List packages"), 
    options, parse_opt, NULL, ls,
    NULL, NULL, NULL, NULL
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
            
            //if (cmdarg->cctx->instpkgs == NULL) {
            //    log(LOGERR, _("ls: installed packages not loaded, "
            //                  "type \"reload\" to load them\n"));
            //    return EINVAL;
            //}
            break;
            
        case 'I':
            //if (cmdarg->cctx->instpkgs == NULL) {
            //    log(LOGERR, _("ls: installed packages not loaded, "
            //                 "type \"reload\" to load them\n"));
            //    return EINVAL;
            //}
            
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


static tn_fn_cmp select_cmpf(unsigned flags) 
{
    tn_fn_cmp cmpf = NULL;

    if (flags & (OPT_LS_SORTBUILDTIME | OPT_LS_SORTBUILDAY)) {
        cmpf = (tn_fn_cmp)pkg_nvr_strcmp_btime;
        
        if (flags & OPT_LS_SORTREV)
            cmpf = (tn_fn_cmp)pkg_nvr_strcmp_btime_rev;
        
        if (flags & OPT_LS_SORTBUILDAY) {
            cmpf = (tn_fn_cmp)pkg_nvr_strcmp_bday;
            
            if (flags & OPT_LS_SORTREV)
                cmpf = (tn_fn_cmp)pkg_nvr_strcmp_bday_rev;
        }
        
    } else if (flags & OPT_LS_SORTREV) {
        cmpf = (tn_fn_cmp)pkg_nvr_strcmp_rev;
    }
    
    return cmpf;
}

#if 0                           /* NFY */
static tn_array *do_upgradeable (struct cmdarg *cmdarg, tn_array *pkgs)
{
    
    int        finded, compare_ver = 0, i;
    tn_array   *pkgs_tmp;

    compare_ver = cmdarg->flags & OPT_LS_UPGRADEABLE_VER;
    pkgs_tmp = n_array_new(64, NULL, (tn_fn_cmp)pkg_nvr_strcmp);
    evrs = n_array_new(64, NULL, NULL);
        
    for (i=0; i < n_array_size(ls_ents); i++) {
        struct pkg   *pkg;
        char          evr[128], *p;
        int           cmprc = 0, n;
        
        pkg = n_array_nth(ls_ents, i);
            
        if (cmdarg->flags & OPT_LS_INSTALLED) {
            finded = pkg_cmp_lookup(pkg, avpkgs,
                                    compare_ver, &cmprc,
                                    evr, sizeof(evr));
            
        } else {
            finded = pkg_cmp_lookup(pkg, instpkgs,
                                    compare_ver, &cmprc,
                                    evr, sizeof(evr));
            cmprc = -cmprc;
        }
            
        if (!finded || cmprc >= 0)
            continue;

        n = strlen(evr) + 1;
        p = alloca(n + 1);
        memcpy(p, evr, n);
        n_array_push(evrs, p);
        n_array_push(pkgs_tmp, pkg);
        
        if (sigint_reached())
            break;
    }

    return pkgs_tmp;
}
#endif

static int ls(struct cmdarg *cmdarg) 
{
    tn_array             *ls_ents = NULL, *instpkgs, *avpkgs;
    tn_array             *evrs = NULL;
    int                  rc = 1, ls_all;
    tn_fn_cmp            cmpf = NULL;
    const char           *path = NULL;
    

    ls_all = 0;
    
    if (cmdarg->flags & OPT_LS_INSTALLED)
        path = "/installed";
    
    if ((ls_ents = poclidek_cmdarg_dents(cmdarg, path, 0)) == NULL) {
        rc = 0;
        goto l_end;
    }
    
    //if ((cmpf = select_cmpf(cmdarg->flags)))
    //    n_array_sort_ex(ls_ents, cmpf);

    instpkgs = poclidek_get_dent_packages(cmdarg->cctx, "/installed");
    avpkgs = poclidek_get_dent_packages(cmdarg->cctx, "/all-avail");
    
    if (cmdarg->flags & OPT_LS_UPGRADEABLE) {
        int        finded, compare_ver = 0, i;
        tn_array   *pkgs_tmp;

        compare_ver = cmdarg->flags & OPT_LS_UPGRADEABLE_VER;
        pkgs_tmp = n_array_new(64, NULL, (tn_fn_cmp)pkg_nvr_strcmp);
        evrs = n_array_new(64, NULL, NULL);
        
        for (i=0; i < n_array_size(ls_ents); i++) {
            struct pkg   *pkg;
            char          evr[128], *p;
            int           cmprc = 0, n;

            pkg = n_array_nth(ls_ents, i);
            
            if (cmdarg->flags & OPT_LS_INSTALLED) {
                finded = pkg_cmp_lookup(pkg, avpkgs,
                                        compare_ver, &cmprc,
                                        evr, sizeof(evr));
                
            } else {
                finded = pkg_cmp_lookup(pkg, instpkgs,
                                        compare_ver, &cmprc,
                                        evr, sizeof(evr));
                cmprc = -cmprc;
            }
            
            if (!finded || cmprc >= 0)
                continue;

            n = strlen(evr) + 1;
            p = alloca(n + 1);
            memcpy(p, evr, n);
            n_array_push(evrs, p);
            n_array_push(pkgs_tmp, pkg);

            if (sigint_reached())
                break;
        }
        
        if (ls_ents)
            n_array_free(ls_ents);
        ls_ents = pkgs_tmp;
    }


    
    if (n_array_size(ls_ents))
        rc = do_ls(ls_ents, cmdarg, evrs);

 l_end:

    if (!ls_all) {
        if (ls_ents)
            n_array_free(ls_ents);
        
    } else if (cmpf) {
        n_array_sort(ls_ents);
    }


    if (evrs) 
        n_array_free(evrs);
    
    return rc;
}



static void ls_summary(FILE *stream, struct pkg *pkg)
{
    struct pkguinf  *pkgu;
    
    if ((pkgu = pkg_info(pkg)) == NULL)
        return;
    
    if (pkgu->summary)
        fprintf(stream, "    %s\n", pkgu->summary);
    pkguinf_free(pkgu);
}


static
int do_ls(const tn_array *ents, struct cmdarg *cmdarg, const tn_array *evrs)
{
    char                 hdr[256], fmt_hdr[256], fmt_pkg[256];
    int                  i, size, err = 0, npkgs = 0;
    int                  term_width, term_width_div2;
    unsigned             flags;
    struct pager         pg;
    FILE                 *out_stream = stdout;

    //printf("do_ls %d\n", n_array_size(ents));
    if (n_array_size(ents) == 0) 
        return 0;

    memset(&pg, 0, sizeof(pg));
    
    
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
    
    if (shOnTTY && (1.3 * term_get_height()) < n_array_size(ents)) {
        if ((out_stream = pager(&pg)) == NULL)
            out_stream = stdout;
    }
    
    size = 0;
    for (i=0; i < n_array_size(ents); i++) {
        struct pkg_dent *ent = n_array_nth(ents, i);
        struct pkg      *pkg;
        char            *pkg_name;

        
        if (out_stream != stdout && pager_exited(&pg))
            goto l_end;

        if (pkg_dent_isdir(ent)) {
            sh_printf_c(out_stream, PRCOLOR_GREEN, "%s/\n", ent->name);
            continue;
        }

        pkg = ent->pkg_dent_pkg;
        
        if (flags & OPT_LS_NAMES_ONLY) 
            pkg_name = pkg->name;
        else
            pkg_name = pkg->nvr;
        
        if (npkgs == 0)
            sh_printf_c(out_stream, PRCOLOR_YELLOW, "%s", hdr);
        
        if (flags & OPT_LS_LONG) {
            char timbuf[30];
            char sizbuf[30];
            char unit = 'K';
            double pkgsize = pkg->size/1024;

            if (pkgsize >= 1024) {
                pkgsize /= 1024;
                unit = 'M';
            }

            snprintf(sizbuf, sizeof(sizbuf), "%.1f %cB", pkgsize, unit);
            
            if (pkg->btime)
                pkg_strbtime(timbuf, sizeof(timbuf), pkg);
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
            sh_printf_c(out_stream, PRCOLOR_YELLOW,
                        ngettext_n_packages_fmt(npkgs), npkgs);
            fprintf(out_stream, "\n");
            
        } else if (flags & OPT_LS_LONG) {
            char unit = 'K';
            double val=size;
        
            if (val >= 1024) {
                val /= 1024;
                unit = 'M';
            }
            
            sh_printf_c(out_stream, PRCOLOR_YELLOW,
                        ngettext_n_packages_fmt(npkgs), npkgs);
            sh_printf_c(out_stream, PRCOLOR_YELLOW,
                        ", %.1f %cB\n", val, unit);
        }
    }

 l_end:
    if (out_stream != stdout)
        pager_close(&pg);
    
    return err == 0;
}


