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
#include "poldek_util.h"
#include "i18n.h"
#include "pkgu.h"
#include "cli.h"
#include "log.h"

static int ls(struct cmdctx *cmdctx);
static
int do_ls(const tn_array *ents, struct cmdctx *cmdctx, const tn_array *evrs);
static error_t parse_opt(int key, char *arg, struct argp_state *state);

static
int pkg_cmp_lookup(struct pkg *lpkg, tn_array *pkgs, int compare_ver,
                   int *cmprc, char *evr, size_t size);


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
    COMMAND_EMPTYARGS | COMMAND_PIPEABLE |
    COMMAND_PIPE_XARGS | COMMAND_PIPE_PACKAGES, 
    "ls", N_("[PACKAGE...]"), N_("List packages"), 
    options, parse_opt, NULL, ls,
    NULL, NULL, NULL, NULL, 0, 0
};


static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdctx *cmdctx = state->input;
    const char *errmsg_excl = _("ls: -l and -G are exclusive");
    arg = arg;
    
    switch (key) {
        case 'l':
            if (cmdctx->_flags & OPT_LS_GROUP) {
                logn(LOGERR, errmsg_excl);
                return EINVAL;
            }
            
            cmdctx->_flags |= OPT_LS_LONG;
            break;

        case 'O':
            cmdctx->_flags |= OPT_LS_SUMMARY;
            break;

        case 'G':
            if (cmdctx->_flags & OPT_LS_LONG) {
                logn(LOGERR, errmsg_excl);
                return EINVAL;
            }

            cmdctx->_flags |= OPT_LS_GROUP;
            break;
            
        case 't':
            cmdctx->_flags |= OPT_LS_SORTBUILDTIME;
            break;

        case 'T':
            cmdctx->_flags |= OPT_LS_SORTBUILDAY;
            break;

        case 'r':
            cmdctx->_flags |= OPT_LS_SORTREV;
            break;
            
        case 'U':
            cmdctx->_flags |= OPT_LS_UPGRADEABLE_VER;
                                /* no break */
        case 'u':
            cmdctx->_flags |= OPT_LS_UPGRADEABLE;
            
            //if (cmdctx->cctx->instpkgs == NULL) {
            //    log(LOGERR, _("ls: installed packages not loaded, "
            //                  "type \"reload\" to load them\n"));
            //    return EINVAL;
            //}
            break;
            
        case 'I':
            //if (cmdctx->cctx->instpkgs == NULL) {
            //    log(LOGERR, _("ls: installed packages not loaded, "
            //                 "type \"reload\" to load them\n"));
            //    return EINVAL;
            //}
            
            cmdctx->_flags |= OPT_LS_INSTALLED;
            break;

        case 'n':
            cmdctx->_flags |= OPT_LS_NAMES_ONLY;
            break;
            
        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}


static tn_fn_cmp select_cmpf(unsigned flags) 
{
    tn_fn_cmp cmpf = NULL;

    if (flags & OPT_LS_SORTBUILDTIME)
        cmpf = (tn_fn_cmp)pkg_dent_cmp_btime;
        
    if (flags & OPT_LS_SORTBUILDAY)
        cmpf = (tn_fn_cmp)pkg_dent_cmp_bday;
    
    return cmpf;
}

static
int pkg_cmp_lookup(struct pkg *lpkg, tn_array *pkgs,
                   int compare_ver, int *cmprc,
                   char *evr, size_t size) 
{
    struct pkg *pkg = NULL;
    char name[256];
    int n, found = 0;

    snprintf(name, sizeof(name), "%s-", lpkg->name);
    n = n_array_bsearch_idx_ex(pkgs, name, (tn_fn_cmp)pkg_nvr_strncmp);

    if (n == -1)
        return 0;

    while (n < n_array_size(pkgs)) {
        pkg = n_array_nth(pkgs, n++);

        if (strcmp(pkg->name, lpkg->name) == 0) {
            found = 1;
            break;
        }

        if (*pkg->name != *lpkg->name)
            break;
    }
    
    if (!found)
        return 0;
    
    if (compare_ver == 0)
        *cmprc = pkg_cmp_evr(lpkg, pkg);
    else 
        *cmprc = pkg_cmp_ver(lpkg, pkg);
    
    snprintf(evr, size, "%s-%s", pkg->ver, pkg->rel);
    
    return found;
}

static tn_array *do_upgradeable(struct cmdctx *cmdctx, tn_array *ls_ents,
                                tn_array *evrs)
{
    int        found, compare_ver = 0, i;
    tn_array   *ls_ents2, *cmpto_pkgs;
    char       *cmpto_path;

    n_assert(cmdctx->_flags & OPT_LS_UPGRADEABLE);
    
    compare_ver = cmdctx->_flags & OPT_LS_UPGRADEABLE_VER;

    cmpto_path = POCLIDEK_INSTALLEDDIR;
    if (cmdctx->_flags & OPT_LS_INSTALLED)
        cmpto_path = POCLIDEK_AVAILDIR;
    
    cmpto_pkgs = poclidek_get_dent_packages(cmdctx->cctx, cmpto_path);
    ls_ents2 = n_array_clone(ls_ents);
    
    for (i=0; i < n_array_size(ls_ents); i++) {
        struct pkg_dent *ent;
        char             evr[128], *p;
        int              cmprc = 0, n;
        
        ent = n_array_nth(ls_ents, i);

        if (pkg_dent_isdir(ent)) 
            continue;
            
        found = pkg_cmp_lookup(ent->pkg_dent_pkg, cmpto_pkgs, compare_ver,
                               &cmprc, evr, sizeof(evr));
        
        if ((cmdctx->_flags & OPT_LS_INSTALLED) == 0)
            cmprc = -cmprc;
        
        if (!found || cmprc >= 0)
            continue;

        n = strlen(evr) + 1;
        p = n_malloc(n + 1);
        memcpy(p, evr, n);
        n_array_push(evrs, p);
        n_array_push(ls_ents2, pkg_dent_link(ent));
        
        if (sigint_reached())
            break;
    }
    
    return ls_ents2;
}

    

static int ls(struct cmdctx *cmdctx) 
{
    tn_array             *ls_ents = NULL;
    tn_array             *evrs = NULL;
    int                  rc = 1;
    char                 *path = NULL, pwd[PATH_MAX];


    poclidek_pwd(cmdctx->cctx, pwd, sizeof(pwd));
    if (cmdctx->_flags & OPT_LS_INSTALLED)
        path = POCLIDEK_INSTALLEDDIR;

    ls_ents = poclidek_resolve_dents(path, cmdctx->cctx, cmdctx->ts, 0);
    if (ls_ents == NULL || n_array_size(ls_ents) == 0) {
        rc = 0;
        goto l_end;
    }

    if (cmdctx->_flags & OPT_LS_UPGRADEABLE) {
        tn_array *tmp;

        if (strcmp(pwd, POCLIDEK_INSTALLEDDIR) == 0)
            cmdctx->_flags |= OPT_LS_INSTALLED;

        evrs = n_array_new(n_array_size(ls_ents), free, NULL);
        tmp = do_upgradeable(cmdctx, ls_ents, evrs);
        n_array_free(ls_ents);
        ls_ents = tmp;
    }
    
    if (n_array_size(ls_ents)) {
        tn_fn_cmp cmpf;
        if ((cmpf = select_cmpf(cmdctx->_flags)))
            n_array_sort_ex(ls_ents, cmpf);
        
        rc = do_ls(ls_ents, cmdctx, evrs);
    }
    

 l_end:

    if (ls_ents)
        n_array_free(ls_ents);
        
    if (evrs) 
        n_array_free(evrs);
    
    return rc;
}



static void ls_summary(struct cmdctx *cmdctx, struct pkg *pkg)
{
    struct pkguinf  *pkgu;
    const char *s;
    
    
    if ((pkgu = pkg_info(pkg)) == NULL)
        return;
    
    if ((s = pkguinf_getstr(pkgu, PKGUINF_SUMMARY)))
        cmdctx_printf(cmdctx, "    %s\n", s);
    pkguinf_free(pkgu);
}


static
int do_ls(const tn_array *ents, struct cmdctx *cmdctx, const tn_array *evrs)
{
    char                 hdr[256], fmt_hdr[256], fmt_pkg[256];
    int                  i, size, err = 0, npkgs = 0;
    register int         incstep = 0;
    int                  term_width, term_width_div2;
    unsigned             flags;

    //printf("do_ls %d\n", n_array_size(ents));
    if (n_array_size(ents) == 0) 
        return 0;

    flags = cmdctx->_flags;
    term_width = poldek_term_get_width();
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
    
    size = 0;
    i = 0;
    incstep = 1;
    if (cmdctx->_flags & OPT_LS_SORTREV) {
        incstep = -1;
        i = n_array_size(ents) - 1;
    }
    
    while (i < n_array_size(ents) && i >= 0) {
        struct pkg_dent *ent = n_array_nth(ents, i);
        struct pkg      *pkg;
        char            *pkg_name;

        if (sigint_reached())
            break;

        if (pkg_dent_isdir(ent)) {
            cmdctx_printf_c(cmdctx, PRCOLOR_GREEN, "!%s/\n", ent->name);
            i += incstep;
            continue;
        }

        pkg = ent->pkg_dent_pkg;
        cmdctx_addtoresult(cmdctx, pkg);

        pkg_name = pkg->nvr;
        if (flags & OPT_LS_NAMES_ONLY) 
            pkg_name = pkg->name;
        
        if (npkgs == 0)
            cmdctx_printf_c(cmdctx, PRCOLOR_YELLOW, "!%s", hdr);

        

        if (flags & OPT_LS_GROUP) {
            const char *group = pkg_group(pkg);
            cmdctx_printf(cmdctx, fmt_pkg, pkg_name, group ? group : "(unset)");
            
        } else if ((flags & OPT_LS_LONG) == 0) {
            cmdctx_printf(cmdctx, "%s\n", pkg_name);
            
        } else if (flags & OPT_LS_LONG) {                /* -l */
            char timbuf[30];
            char sizbuf[30];
 
            if (pkg->size)
                pkg_strsize(sizbuf, sizeof(sizbuf), pkg);
            else
                *sizbuf = '\0';
            
            if (pkg->btime)
                pkg_strbtime(timbuf, sizeof(timbuf), pkg);
            else
                *timbuf = '\0';
            
            if ((flags & OPT_LS_UPGRADEABLE) == 0) {
                cmdctx_printf(cmdctx, fmt_pkg, pkg_name, timbuf, sizbuf);
                
            } else if (evrs) {
                const char *evr = n_array_nth(evrs, i);
                cmdctx_printf(cmdctx, fmt_pkg, pkg_name, evr, timbuf, sizbuf);
            }
            size += pkg->size/1024;
            
        } else {
            n_assert(0);
        }
        
        if (flags & OPT_LS_SUMMARY)
            ls_summary(cmdctx, pkg);
        
        npkgs++;
        i += incstep;
    }
    
    if (npkgs) {
        char buf[1024];
        int n;

        n = 0;
        n += n_snprintf(&buf[n], sizeof(buf) - n,
                        poldek_util_ngettext_n_packages_fmt(npkgs), npkgs);
        
        if (flags & OPT_LS_LONG) {
            char unit = 'K';
            double val = size;
        
            if (val >= 1024) {
                val /= 1024;
                unit = 'M';
            }
            n += n_snprintf(&buf[n], sizeof(buf) - n, ", %.1f %cB\n", val, unit);
        }
        n += n_snprintf(&buf[n], sizeof(buf) - n, "\n");
        cmdctx_printf_c(cmdctx, PRCOLOR_YELLOW, "!%s", buf);
    }

    return err == 0;
}


