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

#include <trurl/nassert.h>
#include <trurl/nstr.h>

#include "capreq.h"
#include "i18n.h"
#include "misc.h"
#include "log.h"
#include "pm/pm.h"

tn_array *pm_rpm_rpmlib_caps(void) 
{
    char **names = NULL, **versions = NULL, *evr;
    int *flags = NULL, n = 0, i;
    tn_array *caps;
    
#if HAVE_RPMGETRPMLIBPROVIDES
    n = rpmGetRpmlibProvides((const char ***)&names, &flags, (const char ***)&versions);
#endif
    
    if (n <= 0)
        return NULL;

    caps = capreq_arr_new(0);
    
    evr = alloca(128);
    
    for (i=0; i<n; i++) {
        struct capreq *cr;

        n_assert(flags[i] & RPMSENSE_EQUAL);
        n_assert(!(flags[i] & (RPMSENSE_LESS | RPMSENSE_GREATER)));

        n_strncpy(evr, versions[i], 128);
        cr = capreq_new_evr(names[i], evr, REL_EQ, 0);
        n_array_push(caps, cr);
    }

    if (names)
        free(names);
    
    if (flags)
        free(flags);

    if (versions)
        free(versions);
    
    n_array_sort(caps);
    return caps;
}

const char *pm_rpm_get_arch(void *pm_rpm) 
{
    pm_rpm = pm_rpm;
    return rpmGetVar(RPM_MACHTABLE_INSTARCH);
}

int pm_rpm_machine_score(void *pm_rpm, int tag, const char *val)
{
    int rpmtag;
    
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

