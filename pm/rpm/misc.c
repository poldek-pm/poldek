/*
  Copyright (C) 2000 - 2002 Pawel A. Gajda <mis@k2.net.pl>

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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include <rpm/rpmlib.h>
#if HAVE_RPMDSRPMLIB
# include <rpm/rpmds.h>
#endif

#include <trurl/nassert.h>
#include <trurl/nstr.h>

#include "capreq.h"
#include "i18n.h"
#include "misc.h"
#include "log.h"
#include "pm/pm.h"
#include "pm_rpm.h"

#if HAVE_RPMDSRPMLIB            /* rpmdsRpmlib() => rpm >= 4.4.3 */

static int extract_rpmds(tn_array *caps, rpmds ds)
{
    ds = rpmdsInit(ds);
    while (rpmdsNext(ds) >= 0) {
        const char *name, *evr;
        char tmp[256], *tmpptr;
        struct capreq *cr;
        uint32_t flags, crflags;

        name = rpmdsN(ds);
        evr = rpmdsEVR(ds);
        flags = rpmdsFlags(ds);
        
        if ((flags & RPMSENSE_EQUAL)) {
            n_strncpy(tmp, evr, 128);
            tmpptr = tmp;
            crflags = REL_EQ;
            
        } else {                /* cap without version */
            tmpptr = NULL;
            crflags = 0;
        }
        
        cr = capreq_new_evr(name, tmpptr, crflags, 0);
        if (cr) {
            msgn(3, "  - %s", capreq_snprintf_s(cr));
            n_array_push(caps, cr);
        }
    }
    ds = rpmdsFree(ds);
    return n_array_size(caps);
}

typedef int (*rpmcap_fn)(rpmds *ds, void *);

static int get_rpmlib_caps(tn_array *caps)
{
    rpmds     ds = NULL;
    int       i;
    rpmcap_fn functions[] = {
        rpmdsRpmlib,
#ifdef HAVE_RPMDSCPUINFO
        (rpmcap_fn)rpmdsCpuinfo,
#endif
#ifdef HAVE_RPMDSGETCONF
        (rpmcap_fn)rpmdsGetconf,
#endif
#ifdef HAVE_RPMDSSYSINFO
        (rpmcap_fn)rpmdsSysinfo,
#endif        
#ifdef HAVE_RPMDSUNAME
        (rpmcap_fn)rpmdsUname,
#endif        
        NULL,
    };

    i = 0;
    msgn(3, "Loading internal capabilities");
    while (functions[i]) {
        functions[i++](&ds, NULL);
    }
    
    return extract_rpmds(caps, ds);
}

#endif  /* HAVE_RPMDSRPMLIB */

#if HAVE_RPMGETRPMLIBPROVIDES   /* rpmGetRpmlibProvides() => rpm < 4.4.3 */
static int get_rpmlib_caps_rpm_lt_4_4_3(tn_array *caps) 
{
    char **names = NULL, **versions = NULL, *evr;
    int *flags = NULL, n = 0, i;

    n_assert(caps);
    n = rpmGetRpmlibProvides((const char ***)&names, &flags, (const char ***)&versions);
    if (n <= 0)
        return 0;

    evr = alloca(128);

    for (i=0; i<n; i++) {
        struct capreq *cr;

        n_assert(flags[i] & RPMSENSE_EQUAL);
        n_assert(!(flags[i] & (RPMSENSE_LESS | RPMSENSE_GREATER)));

        n_strncpy(evr, versions[i], 128);
        cr = capreq_new_evr(names[i], evr, REL_EQ, 0);
        if (cr)
            n_array_push(caps, cr);
    }

    n_cfree(&names);
    n_cfree(&flags);
    n_cfree(&versions);
    return 1;
}
#endif

tn_array *pm_rpm_rpmlib_caps(void *pm_rpm) 
{
    tn_array *caps;
    int rc = 0;

    pm_rpm = pm_rpm;
    caps = capreq_arr_new(0);
    
#if HAVE_RPMDSRPMLIB            /* rpm >= 4.4.3 */
    rc = get_rpmlib_caps(caps);
#else
# if HAVE_RPMGETRPMLIBPROVIDES
    rc = get_rpmlib_caps_rpm_lt_4_4_3(caps);
# endif
#endif
    
    if (rc) {
        n_array_sort(caps);
        
    } else {
        n_array_free(caps);
        caps = NULL;
    }
    return caps;
}

#ifdef HAVE_RPMMACHINESCORE
static int machine_score(int tag, const char *val)
{
    int rpmtag = 0, rc;
    
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

    n_assert(rpmtag);
    return rpmMachineScore(rpmtag, val);
}
#else  /* killed rpmMachineScore() (since 4.4.7) */
static int machine_score(int tag, const char *val)
{
    int rc = 0;
    
    switch (tag) {
        case PMMSTAG_ARCH:
            rc = pm_rpm_arch_score(val);
            break;
            
        case PMMSTAG_OS: {
            char *host_val = rpmExpand("%{_host_os}", NULL);

            rc = 9;
            if (host_val) {
                if (strcasecmp(host_val, val) == 0)
                    rc = 1;                 /* exact fit */
                free(host_val);
            }
            break;

        default:
            n_assert(0);
            break;
        }
    }
    return rc;
}
#endif  /* HAVE_RPMMACHINESCORE */

int pm_rpm_machine_score(void *pm_rpm, int tag, const char *val)
{
    pm_rpm = pm_rpm;
    return machine_score(tag, val);
}

/* XXX: function used directly in pkg.c */
int pm_rpm_arch_score(const char *arch)
{
    char *host_arch;
    int rc;
    
    if (arch == NULL)
        return 0;
    
#ifdef HAVE_RPMMACHINESCORE    
    rc = rpmMachineScore(RPM_MACHTABLE_INSTARCH, arch);
#else
    rc = 9;
    if (strcasecmp(arch, "noarch") == 0) {
        rc = 1;
        
    } else {
        host_arch = rpmExpand("%{_host_cpu}", NULL);
        if (host_arch) {
            if (strcasecmp(host_arch, arch) == 0)
                rc = 1;                 /* exact fit */
            free(host_arch);
        }
    }

    return rc;
}
#endif
