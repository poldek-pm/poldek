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

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <netinet/in.h>

#include <trurl/nstr.h>
#include <trurl/nassert.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "capreq.h"
#include "pkg.h"
#include "pkgdir/pkgdir.h"
#include "pkg_ver_cmp.h"
#include "pkgcmp.h"
#include "poldek_util.h"

extern int poldek_conf_MULTILIB;

/* same name && arch? */
int pkg_is_kind_of(const struct pkg *candidate, const struct pkg *pkg)
{
    register int rc = strcmp(pkg->name, candidate->name);

    if (rc == 0 && poldek_conf_MULTILIB) {
        rc = 1 - pkg_is_colored_like(candidate, pkg);
        //if (rc == 0)
        //    DBGF("%s, %s => YES\n", pkg_id(candidate), pkg_id(pkg));
    }

    return rc == 0;
}

#if 0 /* XXX: disabled, rpm relies on colors only */
int pkg_is_colored_like(const struct pkg *candidate, const struct pkg *pkg)
{
    int rc = -1;

    if (!poldek_conf_MULTILIB)
        return 1;

    if (pkg->color && candidate->color)
        rc = pkg->color & candidate->color;

    if (rc == -1 && pkg_cmp_same_arch(pkg, candidate)) { /* no color? use arch */
        rc = 1;
        DBGF("%s(c=%d), %s(c=%d) => YES\n", pkg_id(candidate),
             candidate->color, pkg_id(pkg), pkg->color);
    }

    if (rc == -1)
        rc = 0;

    return rc;
}
#endif

int pkg_is_colored_like(const struct pkg *candidate, const struct pkg *pkg)
{
    if (!poldek_conf_MULTILIB)
        return 1;

    if (pkg->color && candidate->color)
        return (pkg->color & candidate->color) > 0;

    /* either new or old package contains no binary files, let it happen */
    return 1;
}

/* ret : 1 if pkg is cappable to upgrade arch<=>arch, arch<=>noarch */
int pkg_is_arch_compat(const struct pkg *candidate, const struct pkg *pkg)
{
    // if upgrade preffer same arch but
    // change from/to noarch depends on which pkg is noarch

    return (pkg_cmp_same_arch(candidate, pkg) ||
            pkg_is_noarch(candidate) ||
            pkg_is_noarch(pkg));
}

int pkg_eq_capreq(const struct pkg *pkg, const struct capreq *cr)
{
    return strcmp(pkg->name, capreq_name(cr)) == 0 &&
        strcmp(pkg->ver, capreq_ver(cr)) == 0 &&
        strcmp(pkg->rel, capreq_rel(cr)) == 0 &&
        pkg->epoch == capreq_epoch(cr) &&
        cr->cr_relflags & REL_EQ;
}

int pkg_eq_name_prefix(const struct pkg *pkg1, const struct pkg *pkg2)
{
    char *p1, *p2;
    int n;

    if ((p1 = strchr(pkg1->name, '-')) == NULL)
        p1 = strchr(pkg1->name, '\0');

    if ((p2 = strchr(pkg2->name, '-')) == NULL)
        p2 = strchr(pkg2->name, '\0');

n = p1 - pkg1->name;
    if (n - (p2 - pkg2->name) != 0)
        return 0;

    return strncmp(pkg1->name, pkg2->name, n) == 0;
}

int pkg_cmp_name(const struct pkg *p1, const struct pkg *p2)
{
    return strcmp(p1->name, p2->name);
}

int pkg_ncmp_name(const struct pkg *p1, const struct pkg *p2)
{
    return strncmp(p1->name, p2->name, strlen(p2->name));
}

int pkg_cmp_id(const struct pkg *p1, const struct pkg *p2)
{
    return strcmp(pkg_id(p1), pkg_id(p2));
}


int pkg_cmp_ver(const struct pkg *p1, const struct pkg *p2)
{
    register int rc = 0;

    if ((rc = p1->epoch - p2->epoch))
        return rc;

    return pkg_version_compare(p1->ver, p2->ver);
}

int pkg_cmp_evr(const struct pkg *p1, const struct pkg *p2)
{
    int rc = 0;

    n_assert(p1->ver && p2->ver && p1->rel && p2->rel);

    if ((rc = p1->epoch - p2->epoch))
        return rc;

    rc = pkg_version_compare(p1->ver, p2->ver);

    if (rc == 0)
        rc = pkg_version_compare(p1->rel, p2->rel);

    return rc;
}


int pkg_cmp_name_evr(const struct pkg *p1, const struct pkg *p2)
{
    register int rc;

    if ((rc = pkg_cmp_name(p1, p2)))
        return rc;

    return pkg_cmp_evr(p1, p2);
}

int pkg_cmp_name_evr_rev(const struct pkg *p1, const struct pkg *p2)
{
    register int rc;

    if ((rc = pkg_cmp_name(p1, p2)))
        return rc;

    if ((rc = -pkg_cmp_evr(p1, p2)))
	return rc;

    /* if multilib sort by name, arch, evr */
    if (poldek_conf_MULTILIB) {
        rc = -pkg_cmp_arch(p1, p2);
    }

    return rc;
}

int pkg_cmp_name_evr_arch_rev_srcpri(const struct pkg *p1, const struct pkg *p2)
{
    register int rc;

    if ((rc = pkg_cmp_name_evr_rev(p1, p2)) == 0) {
        rc = -pkg_cmp_arch(p1, p2);
    }

    if (rc == 0 && p1->pkgdir != p2->pkgdir)
        rc = p1->pkgdir->pri - p2->pkgdir->pri;

    return rc;
}

#if 0
/* 0.18.x fn see pkgset.c */
int pkg_cmp_name_srcpri(const struct pkg *p1, const struct pkg *p2)
{
    register int rc;

    if ((rc = pkg_cmp_name(p1, p2)) == 0) {
        if (p1->pkgdir != p2->pkgdir)
            rc = p1->pkgdir->pri - p2->pkgdir->pri;

        if (rc == 0)
            rc = -pkg_cmp_evr(p1, p2);
    }

    return rc;
}
#endif


static __inline__
int pkg_deepcmp_(const struct pkg *p1, const struct pkg *p2);

#if 0
int pkg_deepstrcmp_name_evr_debug(const struct pkg *p1, const struct pkg *p2)
{
    int rc = pkg_deepstrcmp_name_evr(p1, p2);

    if (rc && strcmp(p1->name, p2->name) == 0) {
        printf("cmp %d %s %s\n", rc, pkg_snprintf_s(p1), pkg_snprintf_s0(p2));
    }
    return rc;
}
#endif

int pkg_deepstrcmp_name_evr(const struct pkg *p1, const struct pkg *p2)
{
    register int rc = pkg_strcmp_name_evr_rev(p1, p2);

    if (rc == 0)
        return pkg_deepcmp_(p1, p2);
    return rc;
}


int pkg_strcmp_name_evr_rev(const struct pkg *p1, const struct pkg *p2)
{
    register int rc;

    if ((rc = pkg_cmp_name(p1, p2)))
        return rc;

    n_assert(p1->ver && p2->ver && p1->rel && p2->rel);

    if ((rc = p2->epoch - p1->epoch))
        return rc;

    if ((rc = strcmp(p2->ver, p1->ver)) == 0)
        rc = strcmp(p2->rel, p1->rel);

    if (rc == 0 && p1->_arch != p2->_arch)
        return strcmp(pkg_arch(p1), pkg_arch(p2));

    return rc;
}

int pkg_cmp_arch(const struct pkg *p1, const struct pkg *p2)
{
    int diff = p1->_arch - p2->_arch; /* quick comparison */

    if (diff == 0)
        return 0;

    // compares arch scores returned by pm_architecture_score()
    if (p1->_arch && p2->_arch) {
        int s1 = pkg_arch_score(p1);
        int s2 = pkg_arch_score(p2);
        n_assert(s1 && s2);
        // lower score is better fit; reverse result to keep the same
        // behaviour as V-R comparison
        return s2 - s1;
    }

    return diff; /* fallback to just arch index */
}


static __inline__
int pkg_deepcmp_(const struct pkg *p1, const struct pkg *p2)
{
    register int rc;

    if ((rc = pkg_cmp_arch(p1, p2)))
        return rc;

    if ((rc = p1->color - p2->color))
        return rc;

    if ((rc = p1->btime - p2->btime))
        return rc;

    if ((rc = p1->size - p2->size))
        return rc;

    if ((rc = p1->fsize - p2->fsize))
        return rc;

    if (p1->_os && !p2->_os)
        return 11;

    if (!p1->_os && p2->_os)
        return -11;

    if ((rc = strcmp(p1->_os ? pkg_os(p1) : "" , p2->_os ? pkg_os(p2) : "")))
        return rc;

    if (p1->fn && p2->fn == NULL)
        return 12;

    if (p1->fn == NULL && p2->fn)
        return -12;

    rc = strcmp(p1->fn ? p1->fn : "" , p2->fn ? p2->fn : "");
    return rc;
}

int pkg_deepcmp_name_evr_rev(const struct pkg *p1, const struct pkg *p2)
{
    register int rc;

    if ((rc = pkg_cmp_name_evr_rev(p1, p2)))
        return rc;

    return -pkg_deepcmp_(p1, p2);
}

int pkg_cmp_uniq_name(const struct pkg *p1, const struct pkg *p2)
{
    register int rc;

    if ((rc = pkg_cmp_name(p1, p2)) == 0 && poldek_VERBOSE > 1)
        logn(LOGWARN, _("duplicated name %s"), pkg_snprintf_s(p1));

    return rc;
}

int pkg_cmp_uniq_name_evr(const struct pkg *p1, const struct pkg *p2)
{
    register int rc;

#if ENABLE_TRACE
    if (pkg_cmp_name_evr_rev(p1, p2) == 0)
        logn(LOGNOTICE, "uniq %s: keep %s (score %d), removed %s",
             pkg_snprintf_s(p1), pkg_arch(p1), pkg_arch_score(p1),
             pkg_arch(p2));
#endif
    rc = pkg_cmp_name_evr_rev(p1, p2);

    if (rc == 0 && poldek_conf_MULTILIB)
        rc = p1->_arch - p2->_arch;

    if (rc == 0 && poldek_VERBOSE > 1) {
        if (poldek_VERBOSE > 2) {
            logn(LOGNOTICE, "uniq %s: keep %s (score %d), removed %s (score %d)",
                 pkg_snprintf_s(p1), pkg_arch(p1), pkg_arch_score(p1),
                 pkg_arch(p2), pkg_arch_score(p2));
        } else {
            logn(LOGWARN, _("%s%s%s: removed duplicate package"),
                 pkg_snprintf_s(p2), p2->_arch ? ".": "",
                 p2->_arch ? pkg_arch(p2): "");
        }
    }

    return rc;
}

/* used by pkgdir module */
int pkg_cmp_uniq_name_evr_arch(const struct pkg *p1, const struct pkg *p2)
{
    register int rc;

    if ((rc = pkg_cmp_name_evr_rev(p1, p2)) == 0) {
        const char *a1, *a2;

        a1 = pkg_arch(p1); if (a1 == NULL) a1 = "";
        a2 = pkg_arch(p2); if (a2 == NULL) a2 = "";
        if ((rc = strcmp(a1, a2)) == 0 && poldek_VERBOSE > 1) {
            logn(LOGWARN, _("%s%s%s: removed duplicate package"),
                 pkg_snprintf_s(p2), p2->_arch ? ".": "",
                 p2->_arch ? pkg_arch(p2): "");
        }
    }

    return rc;
}

int pkg_eq_name_evr(const struct pkg *p1, const struct pkg *p2)
{
    if (p1 == p2)
        return 1;
    return pkg_cmp_name_evr(p1, p2);
}

int pkg_cmp_pri_name_evr_rev(struct pkg *p1, struct pkg *p2)
{
    register int cmprc = 0;

    if ((cmprc = p1->pri - p2->pri))
        return cmprc;

    return pkg_cmp_name_evr_rev(p1, p2);
}

int pkg_cmp_recno(const struct pkg *p1, const struct pkg *p2)
{
    return p1->recno - p2->recno;
}

int pkg_cmp_seqno(const struct pkg *p1, const struct pkg *p2)
{
    return p1->seqno - p2->seqno;
}

int pkg_nvr_strcmp(struct pkg *p1, struct pkg *p2)
{
    return strcmp(p1->_nvr, p2->_nvr);
}
