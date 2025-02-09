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

#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <trurl/nassert.h>
#include <trurl/nstr.h>

#include "capreq.h"
#include "i18n.h"
#include "misc.h"
#include "log.h"
#include "pm/pm.h"
#include "pm_rpm.h"

static int extract_rpmds(tn_array *caps, rpmds ds)
{
    ds = rpmdsInit(ds);
    while (rpmdsNext(ds) >= 0) {
        const char *evr, *p;
        char *name, tmp[256], *tmpptr;
        struct capreq *cr;
        uint32_t flags, crflags;

        p = rpmdsDNEVR(ds)+2;
        n_strdupap(p, &name);
        if ((p = strchr(name, ' '))) /* cut afer name */
            *(char*)p = '\0';

        evr = rpmdsEVR(ds);
        flags = rpmdsFlags(ds);

        DBGF("%s, %s\n", name, evr);

        if ((flags & RPMSENSE_EQUAL)) {
            n_strncpy(tmp, evr, 128);
            tmpptr = tmp;
            crflags = REL_EQ;

        } else {                /* cap without version */
            tmpptr = NULL;
            crflags = 0;
        }

        cr = capreq_new_evr(NULL, name, tmpptr, crflags, 0);
        if (cr) {
            msgn(3, "  - %s", capreq_snprintf_s(cr));
            n_array_push(caps, cr);
        }
    }
    ds = rpmdsFree(ds);
    return n_array_size(caps);
}

typedef int (*rpmcap_fn)(rpmds *ds, const void *);

/* pm_ wrappers to unify prototype */
static int pm_rpmdsUname(rpmds *ds, const void *unused) {
    (void)unused;
    return rpmdsUname(ds);
}

static int pm_rpmdsCpuinfo(rpmds *ds, const void *unused) {
    (void)unused;
    return rpmdsCpuinfo(ds);
}

static int get_rpm_internal_caps(tn_array *caps)
{
    rpmds     ds = NULL;
    int       i;
    rpmcap_fn functions[] = {
        rpmdsRpmlib,
#ifdef HAVE_RPMDSUNAME
        pm_rpmdsUname,
#endif
#ifdef HAVE_RPMDSCPUINFO
        pm_rpmdsCpuinfo,
#endif
        NULL,
    };

    i = 0;
    msgn(3, _("Loading internal capabilities"));
    while (functions[i]) {
        functions[i++](&ds, NULL);
    }

    return extract_rpmds(caps, ds);
}

static tn_array *load_internal_caps(void *pm_rpm)
{
    tn_array *caps;
    int rc = 0;

    pm_rpm = pm_rpm;
    caps = capreq_arr_new(0);

    rc = get_rpm_internal_caps(caps);

    if (rc) {
        n_array_sort(caps);

    } else {
        n_array_free(caps);
        caps = NULL;
    }
    return caps;
}

int pm_rpm_satisfies(void *pm_rpm, const struct capreq *req)
{
    struct pm_rpm *pm = pm_rpm;
    int i;

    /* internal caps have names like name(feature) */
    if (!capreq_is_rpmlib(req) && strstr(capreq_name(req), "(") == NULL)
        return 0;

    if (pm->caps == NULL)
        if ((pm->caps = load_internal_caps(pm_rpm)) == NULL)
            return 0;

    i = n_array_bsearch_idx_ex(pm->caps, req, (tn_fn_cmp)capreq_cmp_name);

    if (i >= 0) {
	while (i < n_array_size(pm->caps)) {
	    struct capreq *cap = n_array_nth(pm->caps, i++);

	    if (capreq_cmp_name(cap, req) != 0)
		break;

	    if (cap_match_req(cap, req, 1))
		return 1;
	}
    }

    return 0;
}

static int machine_score(int tag, const char *val)
{
    int rpmtag = 0;

    switch (tag) {
        case PMMSTAG_ARCH:
            rpmtag = RPM_MACHTABLE_INSTARCH;
            break;

        case PMMSTAG_OS:
            rpmtag = RPM_MACHTABLE_INSTOS;
            break;

        default:
            n_assert(0);
            break;
    }

    return rpmMachineScore(rpmtag, val);
}

int pm_rpm_machine_score(void *pm_rpm, int tag, const char *val)
{
    pm_rpm = pm_rpm;
    return machine_score(tag, val);
}

/* XXX: function used directly in pkg.c */
int pm_rpm_arch_score(const char *arch)
{
    if (arch == NULL)
        return 0;

    return machine_score(PMMSTAG_ARCH, arch);
}
