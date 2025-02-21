/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>
#include <sys/param.h>

#include "compiler.h"
#include "sigint/sigint.h"
#include "poldek_util.h"
#include "pkgcmp.h"
#include "i18n.h"
#include "pkgu.h"
#include "cli.h"
#include "log.h"
#include "ls_queryfmt.h"
#include "arg_packages.h"

static int ls(struct cmdctx *cmdctx);
static
int do_ls(const tn_array *ents, struct cmdctx *cmdctx, const tn_array *evrs);
static error_t parse_opt(int key, char *arg, struct argp_state *state);

static
int pkg_cmp_lookup(struct pkg *lpkg, tn_array *pkgs, int compare_ver,
                   int *cmprc, struct pkg **rpkg);



/* cmd_state->flags */
#define OPT_LS_LONG             (1 << 0)
#define OPT_LS_UPGRADEABLE      (1 << 1)
#define OPT_LS_UPGRADEABLE_VER  (1 << 2)
#define OPT_LS_UPGRADEABLE_SEC  (1 << 3)
#define OPT_LS__LONGLONG        (1 << 4) /* upgradeable version in separate column */
#define OPT_LS_INSTALLED        (1 << 5)
#define OPT_LS_SORTBUILDTIME    (1 << 6)
#define OPT_LS_SORTBUILDAY      (1 << 7)
#define OPT_LS_SORTREV          (1 << 8)

#define OPT_LS_GROUP           (1 << 9)
#define OPT_LS_SUMMARY         (1 << 10)
#define OPT_LS_NAMES_ONLY      (1 << 11)
#define OPT_LS_SOURCERPM       (1 << 12)

#define OPT_LS_QUERYFMT        (1 << 13)
#define OPT_LS_QUERYTAGS       (1 << 14)

#define OPT_LS_NOSTUBS         (1 << 15) /* need to operate on full packages */

#define OPT_LS_ERR             (1 << 16)

static struct argp_option options[] = {
 { "long", 'l', 0, 0, N_("Use a long listing format"), 1},
 { "upgradeable", 'u', 0, 0, N_("Show upgrade-able packages only"), 1},
 { "upgradeablev", 'U', 0, 0,
   N_("Likewise but omit packages with different releases only"), 1},
 { "upgradeable-sec", 'S', 0, 0,
   N_("Show upgrade-able packages with potential security fixes"), 1},
 { "installed", 'I', 0, 0, N_("List installed packages"), 1},
 { NULL, 't', 0, 0, N_("Sort by build time"), 1},
 { NULL, 'T', 0, 0, N_("Sort by build day"), 1},
 { "reverse", 'r', 0, 0, N_("Reverse order while sorting"), 1},
 { NULL, 'n', 0, 0, N_("Print only package names"), 1},
 { NULL, 'G', 0, 0, N_("Print package groups"), 1},
 { NULL, 'O', 0, 0, N_("Print package summaries"), 1},
 { "source-rpm", 's', 0, 0, N_("Print package source rpm"), 1},
 { 0, 0, 0, 0, N_("Query format options:"), 2},
 { "qf", OPT_LS_QUERYFMT, "QUERYFMT", 0, N_("Use the following query format"), 2},
 { "querytags", OPT_LS_QUERYTAGS, 0, 0, N_("Show supported tags"), 2},
 { 0, 0, 0, 0, 0, 0 },
};

struct poclidek_cmd command_ls = {
    COMMAND_EMPTYARGS | COMMAND_PIPEABLE |
    COMMAND_PIPE_XARGS | COMMAND_PIPE_PACKAGES,
    "ls", N_("[PACKAGE...]"), N_("List packages"),
    options, parse_opt, NULL, ls,
    NULL, NULL, NULL, NULL, NULL, 0, 0,
    NULL
};


static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdctx *cmdctx = state->input;
    const char *errmsg_excl = _("ls: -l, -s and -G are exclusive");
    arg = arg;

    switch (key) {
        case 'l':
            if (cmdctx->_flags & OPT_LS_GROUP) {
                logn(LOGERR, "%s", errmsg_excl);
                return EINVAL;
            }
            if (cmdctx->_flags & OPT_LS_LONG)
                cmdctx->_flags |= OPT_LS__LONGLONG;
            else
                cmdctx->_flags |= OPT_LS_LONG;
            break;

        case 'O':
            cmdctx->_flags |= OPT_LS_SUMMARY | OPT_LS_NOSTUBS;
            break;

        case 'G':
            if (cmdctx->_flags & OPT_LS_LONG || cmdctx->_flags & OPT_LS_SOURCERPM) {
                logn(LOGERR, "%s", errmsg_excl);
                return EINVAL;
            }

            cmdctx->_flags |= OPT_LS_GROUP | OPT_LS_NOSTUBS;
            break;

        case 's':
            if (cmdctx->_flags & OPT_LS_LONG || cmdctx->_flags & OPT_LS_GROUP) {
                logn(LOGERR, "%s", errmsg_excl);
                return EINVAL;
            }

            cmdctx->_flags |= OPT_LS_SOURCERPM | OPT_LS_NOSTUBS;
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

        case 'u':
            cmdctx->_flags |= OPT_LS_UPGRADEABLE;
            break;

        case 'U':
            cmdctx->_flags |= OPT_LS_UPGRADEABLE | OPT_LS_UPGRADEABLE_VER;
            break;

        case 'S':
            cmdctx->_flags |= OPT_LS_UPGRADEABLE | OPT_LS_UPGRADEABLE_SEC | OPT_LS_NOSTUBS;
            break;

        case 'I':
            cmdctx->_flags |= OPT_LS_INSTALLED;
            break;

        case 'n':
            cmdctx->_flags |= OPT_LS_NAMES_ONLY;
            break;

	case OPT_LS_QUERYFMT:
	    cmdctx->_flags |= OPT_LS_QUERYFMT | OPT_LS_NOSTUBS;

	    if (arg) {
		struct lsqf_ent_array *array = NULL;

		if ((array = lsqf_parse(arg)) == NULL)
		    return EINVAL;

		cmdctx->_data = array;
	    }

	    break;

	case OPT_LS_QUERYTAGS:
	    lsqf_show_querytags(cmdctx);
	    return EINVAL;

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
                   struct pkg **rpkg)
{
    struct pkg *pkg = NULL;
    int n, found = 0;

    n = n_array_bsearch_idx_ex(pkgs, lpkg, (tn_fn_cmp)pkg_ncmp_name);

    if (n == -1)
        return 0;

    while (n < n_array_size(pkgs)) {
        pkg = n_array_nth(pkgs, n++);

        if (pkg_is_kind_of(pkg, lpkg)) {
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

    *rpkg = pkg;

    return found;
}

static tn_array *do_upgradeable(struct cmdctx *cmdctx, tn_array *ls_ents,
                                tn_array *evrs)
{
    int        found, compare_ver = 0, i;
    tn_array   *upgradeable, *cmpto_pkgs = NULL, *srcpkgs = NULL;
    char       *cmpto_path;

    n_assert(cmdctx->_flags & OPT_LS_UPGRADEABLE);

    compare_ver = cmdctx->_flags & OPT_LS_UPGRADEABLE_VER;

    cmpto_path = POCLIDEK_INSTALLEDDIR;
    if (cmdctx->_flags & OPT_LS_INSTALLED)
        cmpto_path = POCLIDEK_AVAILDIR;

    unsigned dent_ldflags = (cmdctx->_flags & OPT_LS_NOSTUBS) ? 0 : PKG_DENT_LDFIND_STUBSOK;
    DBGF("ldflags %d\n", dent_ldflags);
    cmpto_pkgs = poclidek_get_dent_packages(cmdctx->cctx, cmpto_path, dent_ldflags);

    if (cmpto_pkgs == NULL) {
        logn(LOGERR, _("%s: no packages found"), cmpto_path);
        return NULL;
    }

    n_assert(n_array_ctl_get_cmpfn(cmpto_pkgs) == (tn_fn_cmp)pkg_cmp_name_evr_rev);

    upgradeable = n_array_clone(ls_ents);

    if (cmdctx->_flags & OPT_LS_UPGRADEABLE_SEC)
        srcpkgs = n_array_new(64, free, (tn_fn_cmp)strcmp);

    for (i=0; i < n_array_size(ls_ents); i++) {
        struct pkg_dent  *ent;
        struct pkg       *rpkg = NULL;
        int              cmprc = 0;

        ent = n_array_nth(ls_ents, i);

        if (pkg_dent_isdir(ent))
            continue;

        found = pkg_cmp_lookup(ent->pkg_dent_pkg, cmpto_pkgs, compare_ver,
                               &cmprc, &rpkg);

        if ((cmdctx->_flags & OPT_LS_INSTALLED) == 0)
            cmprc = -cmprc;

        if (!found || cmprc >= 0)
            continue;

        if (cmdctx->_flags & OPT_LS_UPGRADEABLE_SEC) {
            const char *spkg = pkg_srcfilename_s(rpkg);

            if (spkg && n_array_bsearch(srcpkgs, spkg)) { /* parent included, so me too */
                found = 1;

            } else {
                struct pkg *ipkg, *upkg;
                struct pkguinf *inf;

                ipkg = rpkg;
                upkg = ent->pkg_dent_pkg;
                if (cmdctx->_flags & OPT_LS_INSTALLED) {
                    upkg = rpkg;
                    ipkg = ent->pkg_dent_pkg;
                }

                found = 0;
                if ((inf = pkg_uinf(upkg)) == NULL)
                    continue;

                if (pkguinf_changelog_with_security_fixes(inf, ipkg->btime)) {
                    char *sp;
                    if ((sp = pkg_srcfilename_s(rpkg))) {
                        n_array_push(srcpkgs, n_strdup(sp));
                        n_array_sort(srcpkgs);
                        DBGF("%s\n", sp);
                    }
                    found = 1;
                }

                pkguinf_free(inf);
            }
        }

        if (!found)
            continue;


        n_array_push(evrs, pkg_link(rpkg));
        n_array_push(upgradeable, pkg_dent_link(ent));

        if (sigint_reached())
            break;
    }

    n_array_cfree(&cmpto_pkgs);
    n_array_cfree(&srcpkgs);

    return upgradeable;
}


static int ls(struct cmdctx *cmdctx)
{
    tn_array             *ls_ents = NULL;
    tn_array             *evrs = NULL;
    int                  rc = 1;
    const char           *path, *pwd;
    unsigned             cmdflags = cmdctx->_flags;
    unsigned             dent_ldflags;
    tn_fn_cmp            cmpf;

    pwd = path = poclidek_pwd(cmdctx->cctx);
    if (cmdflags & OPT_LS_INSTALLED)
        path = POCLIDEK_INSTALLEDDIR;

    dent_ldflags = (cmdctx->_flags & OPT_LS_NOSTUBS) ? 0 : PKG_DENT_LDFIND_STUBSOK;
    ls_ents = poclidek_resolve_dents(path, cmdctx->cctx, cmdctx->ts,
                                     ARG_PACKAGES_RESOLV_WARN_ONLY, dent_ldflags);

    if (ls_ents == NULL || n_array_size(ls_ents) == 0) {
        rc = 0;
        goto l_end;
    }

    if (cmdctx->_flags & OPT_LS_UPGRADEABLE) {
        tn_array *tmp;

        if (pwd && strcmp(pwd, POCLIDEK_INSTALLEDDIR) == 0)
            cmdctx->_flags |= OPT_LS_INSTALLED;

        evrs = n_array_new(n_array_size(ls_ents), (tn_fn_free)pkg_free, NULL);
        tmp = do_upgradeable(cmdctx, ls_ents, evrs);
        if (tmp == NULL)
            goto l_end;
        n_array_free(ls_ents);
        ls_ents = tmp;
    }

    if (n_array_size(ls_ents)) {
        if ((cmpf = select_cmpf(cmdctx->_flags)))
            n_array_sort_ex(ls_ents, cmpf);

        rc = do_ls(ls_ents, cmdctx, evrs);

        if (cmpf)
            n_array_sort(ls_ents);  /* sort them back, ls_ents could be reference
                                       to global packages array */
    }


 l_end:
    if (cmdctx->_flags & OPT_LS_QUERYFMT) {
	lsqf_ent_array_free(cmdctx->_data);
	cmdctx->_data = NULL;
    }

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


    if ((pkgu = pkg_uinf(pkg)) == NULL)
        return;

    if ((s = pkguinf_get(pkgu, PKGUINF_SUMMARY)))
        cmdctx_printf(cmdctx, "    %s\n", s);

    pkguinf_free(pkgu);
}

static void snprintf_c(int color, char *buf, size_t size,
                       const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    if (color)
        poldek_term_vsnprintf_c(color, buf, size, fmt, args);
    else
        n_vsnprintf(buf, size, fmt, args);

    va_end(args);
}

static int print_pair(int colored, char *line, size_t size,
                      struct pkg *pkg, struct pkg *old_pkg)
{
    int eq_ver = n_str_eq(pkg->ver, old_pkg->ver);
    int eq_rel = n_str_eq(pkg->rel, old_pkg->rel);
    int eq_arch = n_str_eq(pkg_arch(pkg), pkg_arch(old_pkg));

    char arch[256];
    int n = 0;

    int new_color = PRCOLOR_GREEN;
    int old_color = PRCOLOR_RED;

    if (!colored) {
        new_color = 0;
        old_color = 0;
    }

    if (eq_arch) {
        snprintf(arch, sizeof(arch), "%s", pkg_arch(pkg));
    } else {
        char old[64], new[64];
        snprintf_c(old_color, old, sizeof(old), "%s", pkg_arch(old_pkg));
        snprintf_c(new_color, new, sizeof(new), "%s", pkg_arch(pkg));

        n_snprintf(arch, sizeof(arch), "(%s => %s)", old, new);
    }

    if (eq_ver && eq_rel) {     /* reinstallation */
        n = n_snprintf(line, size, "%s", pkg_id(pkg));

    } else if (!eq_ver && eq_rel) {
        char old[64], new[64];
        snprintf_c(old_color, old, sizeof(old), "%s", old_pkg->ver);
        snprintf_c(new_color, new, sizeof(new), "%s", pkg->ver);

        n = n_snprintf(line, size, "%s-(%s => %s)-%s.%s",
                   pkg->name, old, new, pkg->rel, arch);

    } else if (eq_ver && !eq_rel) {
        char old[64], new[64];
        snprintf_c(old_color, old, sizeof(old), "%s", old_pkg->rel);
        snprintf_c(new_color, new, sizeof(new), "%s", pkg->rel);

        n = n_snprintf(line, size, "%s-%s-(%s => %s).%s",
                   pkg->name, pkg->ver, old, new, arch);

    } else if (!eq_ver && !eq_rel) {
        char old[64], new[64];
        snprintf_c(old_color, old, sizeof(old), "%s-%s", old_pkg->ver, old_pkg->rel);
        snprintf_c(new_color, new, sizeof(new), "%s-%s", pkg->ver, pkg->rel);

        n = n_snprintf(line, size, "%s-(%s => %s).%s",
                       pkg->name, old, new, arch);
    }

    return n;
}


static
int do_ls(const tn_array *ents, struct cmdctx *cmdctx, const tn_array *evrs)
{
    char                 hdr[256];
    int                  i, size, err = 0, npkgs = 0;
    register int         incstep = 0;
    int                  term_width, term_width_div2;
    unsigned             flags;

    if (n_array_size(ents) == 0)
        return 0;

    flags = cmdctx->_flags;
    term_width = poldek_term_get_width();
    term_width_div2 = term_width/2;

    *hdr = '\0';

    if (flags & OPT_LS_GROUP || flags & OPT_LS_SOURCERPM) {
	if (flags & OPT_LS_GROUP)
	    snprintf(hdr, sizeof(hdr), "%-*s%-*s\n",
		term_width_div2 + term_width_div2/10, _("package"), (term_width/7), _("group"));
        else
	    snprintf(hdr, sizeof(hdr), "%-*s%-*s\n",
		term_width_div2 + term_width_div2/10, _("package"), (term_width/7), _("source rpm"));
    } else if (flags & OPT_LS_LONG) {
        if ((flags & OPT_LS_UPGRADEABLE) == 0 || (flags & OPT_LS__LONGLONG) == 0) {
            snprintf(hdr, sizeof(hdr), "%-*s %*s %*s\n",
                     term_width_div2 + term_width_div2/10, _("package"),
                     (term_width/7), _("build date"),
                     (term_width/8) + 2, _("size"));
        } else {
            if (flags & OPT_LS_INSTALLED)
                snprintf(hdr, sizeof(hdr), "%-*s%-*s %-*s%*s\n",
			 (term_width/2) - 1, _("installed"),
                         (term_width/6) - 1, _("available"),
			 (term_width/6) - 1, _("build date"),
			 (term_width/6) - 1, _("size"));
            else
                snprintf(hdr, sizeof(hdr), "%-*s%-*s %-*s%*s\n",
			 (term_width/2) - 1, _("available"),
                         (term_width/6) - 1, _("installed"),
			 (term_width/6) - 1, _("build date"),
			 (term_width/6) - 1, _("size"));
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
        const char      *pkg_name;

        if (sigint_reached())
            break;

        if (pkg_dent_isdir(ent)) {
            cmdctx_printf_c(cmdctx, PRCOLOR_GREEN, "%s/\n", ent->name);
            i += incstep;
            continue;
        }

        pkg = ent->pkg_dent_pkg;
        cmdctx_addtoresult(cmdctx, pkg);

        pkg_name = pkg_id(pkg);
        if (flags & OPT_LS_NAMES_ONLY)
            pkg_name = pkg->name;

        if (npkgs == 0)
            cmdctx_printf_c(cmdctx, PRCOLOR_YELLOW, "!%s", hdr);

        if (flags & OPT_LS_GROUP) {
            const char *group = pkg_group(pkg);
            cmdctx_printf(cmdctx, "%-*s %-*s\n",
			  term_width_div2 + term_width_div2/10 - 1, pkg_name,
			  (term_width/7), group ? group : "(unset)");
	}
        else if (flags & OPT_LS_SOURCERPM) {
            const char *srcrpm = pkg_srcfilename_s(pkg);
            cmdctx_printf(cmdctx, "%-*s %-*s\n",
			  term_width_div2 + term_width_div2/10 - 1, pkg_name,
			  (term_width/7), srcrpm ? srcrpm : "(unset)");

        } else if (flags & OPT_LS_QUERYFMT) {
	    char *queryfmt = NULL;

	    if ((queryfmt = lsqf_to_string(cmdctx->_data, pkg))) {
		cmdctx_printf(cmdctx, "%s", queryfmt);

                n_free(queryfmt);
	    }

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
                cmdctx_printf(cmdctx, "%-*s %*s %*s\n",
			      term_width_div2 + term_width_div2/10, pkg_name,
			      (term_width/7), timbuf,
			      (term_width/8) + 2, sizbuf);

            } else if (evrs) {
                struct pkg *epkg = n_array_nth(evrs, i);

                /* short, colored version by default */
                if ((flags & OPT_LS__LONGLONG) == 0) {
                    struct pkg *old, *new;
                    int is_piped = cmdctx_is_piped(cmdctx);
                    char pbuf[1024];
                    int diff = 0;

                    new = epkg;
                    old = pkg;

                    if (flags & OPT_LS_INSTALLED) {
                        new = pkg;
                        old = epkg;
                    }

                    if (is_piped) {
                        print_pair(0, pbuf, sizeof(pbuf), old, new);

                    } else {
                        int plen = print_pair(0, pbuf, sizeof(pbuf), old, new);
                        int clen = print_pair(1, pbuf, sizeof(pbuf), old, new);
                        diff = clen - plen;
                    }

                    cmdctx_printf(cmdctx, "%-*s %*s %*s\n",
                                  term_width_div2 + term_width_div2/10 + diff, pbuf,
                                  (term_width/7), timbuf,
                                  (term_width/8) + 2, sizbuf);

                } else {
                    char evr[256];
                    pkg_idevr_snprintf(evr, sizeof(evr), epkg);
                    cmdctx_printf(cmdctx, "%-*s%-*s %-*s %*s\n",
                                  (term_width/2) - 1, pkg_name,
                                  (term_width/6) - 1, evr,
                                  (term_width/6) - 1, timbuf,
                                  (term_width/6) - 1, sizbuf);
                }
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
