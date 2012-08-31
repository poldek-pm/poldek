/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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

#ifdef HAVE_RPM_5
# include <rpm/rpmtypes.h>
# include <rpm/rpmtag.h>
# include <rpm/rpmio.h>
#else
# include <rpm/rpmlib.h>
#endif

#if HAVE_RPM_4_1
# define _RPMPRCO_INTERNAL
# include <rpm/rpmds.h>
#endif

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

typedef int (*rpmcap_fn)(rpmds *ds, void *);


#ifdef HAVE_RPMDSSYSINFO
static int pm_rpmdsSysinfo(rpmds * dsp, const char * fn) {
	int ret;
    
# ifndef HAVE_RPM_VERSION_GE_4_4_8
    ret = rpmdsSysinfo(dsp, fn);
    
# else    
	rpmPRCO PRCO = rpmdsNewPRCO(NULL);
	PRCO->Pdsp = dsp;
	ret = rpmdsSysinfo(PRCO, fn);
	PRCO->Pdsp = NULL;
	rpmdsFreePRCO(PRCO);
# endif
	return ret;
}
#endif

static int get_rpm_internal_caps(tn_array *caps)
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
        (rpmcap_fn)pm_rpmdsSysinfo,
#endif        
#ifdef HAVE_RPMDSUNAME
        (rpmcap_fn)rpmdsUname,
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
#endif  /* HAVE_RPMDSRPMLIB */


#if HAVE_RPMGETRPMLIBPROVIDES   /* rpmGetRpmlibProvides() => rpm < 4.4.3 */
static int get_rpm_internal_caps_rpm_lt_4_4_3(tn_array *caps) 
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
        cr = capreq_new_evr(NULL, names[i], evr, REL_EQ, 0);
        if (cr)
            n_array_push(caps, cr);
    }

    n_cfree(&names);
    n_cfree(&flags);
    n_cfree(&versions);
    return 1;
}
#endif

static tn_array *load_internal_caps(void *pm_rpm) 
{
    tn_array *caps;
    int rc = 0;

    pm_rpm = pm_rpm;
    caps = capreq_arr_new(0);
    
#if HAVE_RPMDSRPMLIB            /* rpm >= 4.4.3 */
    rc = get_rpm_internal_caps(caps);
#else
# if HAVE_RPMGETRPMLIBPROVIDES
    rc = get_rpm_internal_caps_rpm_lt_4_4_3(caps);
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

static int rpmioaccess_satisfies(const struct capreq *req)
{
    int rc = 0;
    
#if HAVE_RPMIOACCESS
    const char *name = NULL;
    int n = 0;
    
    if (capreq_versioned(req))
        return 0;

    name = capreq_name(req);
    n = strlen(name);
    
    /* code copied from lib/depends.c:563 */
    if (n > 5 && name[n - 1] == ')' &&
        ((strchr("Rr_", name[0]) != NULL && 
          strchr("Ww_", name[1]) != NULL && 
          strchr("Xx_", name[2]) != NULL &&
          name[3] == '(') ||	!strncmp(name, "exists(", sizeof("exists(")-1)
         ||	!strncmp(name, "executable(", sizeof("executable(")-1)
         ||	!strncmp(name, "readable(", sizeof("readable(")-1)
         ||	!strncmp(name, "writable(", sizeof("writable(")-1)
         )) {
        
        rc = (rpmioAccess(name, NULL, X_OK) == 0);
    }
#endif  /* HAVE_RPMIOACCESS */
    return rc;
}

int pm_rpm_satisfies(void *pm_rpm, const struct capreq *req)
{
    struct pm_rpm *pm = pm_rpm;
    struct capreq *cap = NULL;
    
    /* internal caps have names like name(feature) */
    if (!capreq_is_rpmlib(req) && strstr(capreq_name(req), "(") == NULL)
        return 0;

    if (rpmioaccess_satisfies(req))
        return 1;

    if (pm->caps == NULL)
        if ((pm->caps = load_internal_caps(pm_rpm)) == NULL)
            return 0;
    
    cap = n_array_bsearch_ex(pm->caps, req,
                             (tn_fn_cmp)capreq_cmp_name);
    
    if (cap && cap_match_req(cap, req, 1))
        return 1;

    return 0;
}

static void get_host_cpu_vendor_os(const char **acpu, const char **avendor,
                                   const char **aos) 
{
    static char *cpu = NULL, *vendor = NULL, *os = NULL; /* XXX static variable */
    
    if (cpu == NULL) {
        cpu = rpmExpand("%{_host_cpu}", NULL);
        vendor = rpmExpand("%{_host_vendor}", NULL);
        os = rpmExpand("%{_host_os}", NULL);
    }
    
    if (acpu)
        *acpu = cpu;

    if (avendor)
        *avendor = vendor;

    if (aos)
        *aos = os;
}


#ifdef HAVE_RPMPLATFORMSCORE    /* rpm >= 4.4.9 */
static int machine_score(int tag, const char *val) {
    const char *cpu = NULL, *vendor = NULL, *os = NULL;
    int rc;

    get_host_cpu_vendor_os(&cpu, &vendor, &os);
    
    if (! (cpu && vendor && os) ) {
        rc = rpmPlatformScore(val, platpat, nplatpat);
        
    } else {
	    int size = strlen(cpu) + strlen(vendor) + strlen(os) + 3;
	    char *p = alloca(size);
	    switch (tag) {
		    case PMMSTAG_ARCH:
			    n_snprintf(p, size, "%s-%s-%s", val, vendor, os);
                DBGF("ARCH %s\n", p);
			    break;
                
		    case PMMSTAG_OS:
                n_snprintf(p, size, "%s-%s-%s", cpu, vendor, val);
                DBGF("OS %s\n", p);
			    break;
                
		    default:
                n_assert(0);
			    break;
	    }
        
        rc = rpmPlatformScore(p, platpat, nplatpat);

        /* do not trust PlatformScore() to much and just strcmp -> OS never used
           for scoring */
        if (rc == 0 && tag == PMMSTAG_OS) {
            rc = (strcasecmp(os, val) == 0); 
            DBGF("cmp %s, %s => %d\n", os, val, rc);
        }
        
    }
    
    return rc;
}

#elif defined(HAVE_RPMMACHINESCORE)
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
#else  /* !HAVE_RPMPLATFORMSCORE && !HAVE_RPMMACHINESCORE;
          killed rpmMachineScore() (since 4.4.7) */
static int machine_score(int tag, const char *val)
{
    int rc = 0;
    
    switch (tag) {
        case PMMSTAG_ARCH:
            if (strcasecmp(val, "noarch") == 0) {
                rc = 1;
        
            } else {
                const char *host_arch = NULL;
                get_host_cpu_vendor_os(&host_arch, NULL, NULL);
                if (host_arch) {
                    if (strcasecmp(host_arch, val) == 0)
                        rc = 1;                 /* exact fit */
                }
            }
            break;
            
        case PMMSTAG_OS: {
            const char *host_os = NULL;
            get_host_cpu_vendor_os(NULL, NULL, &host_os);
            
            rc = 9;
            if (host_os && strcasecmp(host_os, val) == 0)
                rc = 1;                 /* exact fit */
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
    if (arch == NULL)
        return 0;

    return machine_score(PMMSTAG_ARCH, arch);
}

