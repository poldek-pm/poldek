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

#if HAVE_RPMDSRPMLIB            /* rpmdsRpmlib() => rpm >= 4.4.3 */
static int get_rpmlib_caps(tn_array *caps)
{
    rpmds ds = NULL;
    
    if (rpmdsRpmlib(&ds, NULL) != 0)
        return 0;
    
    ds = rpmdsInit(ds);
    while (rpmdsNext(ds) >= 0) {
        const char *name, *evr;
        char tmp[256];
        struct capreq *cr;
        uint32_t flags;

        name = rpmdsN(ds);
        evr = rpmdsEVR(ds);
        flags = rpmdsFlags(ds);
        
        n_assert(flags & RPMSENSE_EQUAL);
        n_assert((flags & (RPMSENSE_LESS | RPMSENSE_GREATER)) == 0);

        n_strncpy(tmp, evr, 128);
        cr = capreq_new_evr(name, tmp, REL_EQ, 0);
        if (cr) 
            n_array_push(caps, cr);
    }
    ds = rpmdsFree(ds);
    return n_array_size(caps);
}
#endif  /* HAVE_RPMDSRPMLIB */

#if HAVE_RPMGETRPMLIBPROVIDES   /* rpmGetRpmlibProvides() => rpm < 4.4.3 */
static int get_rpmlib_caps_rpm_lt_4_4_3(tn_array *caps) 
{
    char **names = NULL, **versions = NULL, *evr;
    int *flags = NULL, n = 0, i;

    n = rpmGetRpmlibProvides((const char ***)&names, &flags, (const char ***)&versions);
    if (n <= 0)
        return 0;

    caps = capreq_arr_new(0);
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

tn_array *pm_rpm_rpmlib_caps(void) 
{
    tn_array *caps;
    int rc = 0;
    
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


const char *pm_rpm_get_arch(void *pm_rpm) 
{
    pm_rpm = pm_rpm;
    return rpmGetVar(RPM_MACHTABLE_INSTARCH);
}

int pm_rpm_machine_score(void *pm_rpm, int tag, const char *val)
{
    int rpmtag = 0;
    
    pm_rpm = pm_rpm;
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


int pm_rpm_arch_score(const char *arch)
{
    if (arch == NULL)
        return 0;
    
    return rpmMachineScore(RPM_MACHTABLE_INSTARCH, arch);
}

