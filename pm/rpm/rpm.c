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
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <rpm/rpmlib.h>
#include <rpm/rpmio.h>
#include <rpm/rpmurl.h>
#include <rpm/rpmmacro.h>

#ifdef HAVE_RPM_4_1
# include <rpm/rpmdb.h>
#endif

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nstr.h>
#include <trurl/nmalloc.h>
#include <trurl/n_snprintf.h>


#include <vfile/vfile.h>

#include "sigint/sigint.h"
#include "i18n.h"

#include "depdirs.h"
#include "misc.h"
#include "log.h"
#include "pkg.h"
#include "capreq.h"
#include "pm_rpm.h"

static int initialized = 0;
int pm_rpm_verbose = 0;

void pm_rpm_destroy(void *pm_rpm) 
{
    struct pm_rpm *pm = pm_rpm;
    
    n_cfree(&pm->rpm);
    n_cfree(&pm->sudo);
    free(pm);
}

void *pm_rpm_init(tn_array *macros) 
{
    struct pm_rpm *pm_rpm;
    char path[PATH_MAX];
    
    if (initialized == 0) {
        if (rpmReadConfigFiles(NULL, NULL) != 0) {
            logn(LOGERR, "failed to read rpmlib configs");
            return 0;
        }
        initialized = 1;
    }

    if (macros) {
        int i;
        
        for (i=0; i<n_array_size(macros); i++) {
            char *def, *macro;
            
            if ((macro = n_array_nth(macros, i)) == NULL)
                continue;
            
            if ((def = strchr(macro, ' ')) == NULL && 
                (def = strchr(macro, '\t')) == NULL) {
                logn(LOGERR, _("%s: invalid macro definition"), macro);
                return 0;
                
            } else {
                char *sav = def;
                
                *def = '\0';
                def++;
                while(isspace(*def))
                    def++;
                msg(4, "addMacro %s %s\n", macro, def);
                addMacro(NULL, macro, NULL, def, RMIL_DEFAULT);
                *sav = ' ';
            }
        }
    }
    
    if (poldek_lookup_external_command(path, sizeof(path), "rpm")) {
        pm_rpm = n_malloc(sizeof(*pm_rpm));
        pm_rpm->rpm = n_strdup(path);
        if (poldek_lookup_external_command(path, sizeof(path), "sudo"))
            pm_rpm->sudo = n_strdup(path);
        else
            pm_rpm->sudo = NULL;
        return pm_rpm;
    }
    
    return NULL;
}



void pm_rpm_define(void *ctx, const char *name, const char *val) 
{
    ctx = ctx;
    addMacro(NULL, name, NULL, val, RMIL_DEFAULT);
}

#define RPM_DBPATH "/var/lib/rpm"
rpmdb pm_rpm_opendb(void *pm_rpm, const char *rootdir, const char *dbpath,
                    mode_t mode)
{
    rpmdb db = NULL;

    pm_rpm = pm_rpm;
    if (dbpath)
        addMacro(NULL, "_dbpath", NULL, dbpath, RMIL_DEFAULT);
    
    if (rpmdbOpen(rootdir ? rootdir : "/", &db, mode, 0) != 0) {
        db = NULL;
        logn(LOGERR, _("%s%s: open rpm database failed"),
             rootdir ? rootdir:"", dbpath ? dbpath : RPM_DBPATH);
    }
    
#if ENABLE_TRACE    
    DBGF("%p %d\n", db, db->nrefs);
    system("ls -l /proc/$(echo `ps aux | grep poldek | grep -v grep` | awk '{print $2}')/fd/ | grep rpm");
    sleep(3);
#endif
    
    return db;
}

char *pm_rpm_dbpath(void *pm_rpm, char *path, size_t size)
{
    char *p;

    pm_rpm = pm_rpm;
    n_assert(initialized);
    p = (char*)rpmGetPath("%{_dbpath}", NULL);
    if (p == NULL || *p == '%')
        n_snprintf(path, size, "%s", RPM_DBPATH);
    else
        n_snprintf(path, size, "%s", p);

    if (p)
        free(p);
        
    return path;
}

time_t pm_rpm_dbmtime(void *pm_rpm, const char *dbpath) 
{
    const char *file = "packages.rpm";
    char path[PATH_MAX];
    struct stat st;

    pm_rpm = pm_rpm;
#ifdef HAVE_RPM_4_0
    file = "Packages";
#endif
    
    snprintf(path, sizeof(path), "%s/%s", dbpath, file);
     
    if (stat(path, &st) != 0)
        return 0;

    return st.st_mtime;
}

void pm_rpm_closedb(rpmdb db) 
{
    DBGF("DB %p close %d\n", db, db->nrefs);
    
    rpmdbClose(db);
    
#if ENABLE_TRACE        
    system("ls -l /proc/$(echo `ps aux | grep poldek | grep -v grep` | awk '{print $2}')/fd/ | grep rpm");
    sleep(3);
#endif
    
    db = NULL;
}



#if defined HAVE_RPMLOG && !defined ENABLE_STATIC
/* hack: rpmlib dumps messges to stdout only... (AFAIK)  */
void rpmlog(int prii, const char *fmt, ...) 
{
    va_list args;
    int pri, mask;
    int rpmlogMask, logpri = LOGERR, verbose_level = -1;

    pri =  RPMLOG_PRI(prii);
    mask = RPMLOG_MASK(pri);
    rpmlogMask = rpmlogSetMask(0); /* get mask */
        
    if ((mask & rpmlogMask) == 0)
	return;

    if (pri <= RPMLOG_ERR)
        logpri = LOGERR;
    
    else if (pri == RPMLOG_WARNING || pri == RPMLOG_NOTICE)
        logpri = LOGWARN;
    
    else {
        logpri = LOGINFO;
        verbose_level = 2;
    }

    va_start(args, fmt);

#if 0    
    printf("%d, v = %d, verbose = %d, pm_rpm_verbose = %d\n", pri,
           verbose_level, verbose, pm_rpm_verbose);
    vprintf(fmt, args);
#endif
    
    if (verbose_level > verbose || verbose_level > pm_rpm_verbose)
        return;
    
    if ((logpri & (LOGERR | LOGWARN)) == 0)
        vlog(logpri, 0, fmt, args);
        
    else {                  /* basename(path) */
        char m[1024], *p, *q;
        int n;

        
        n = n_vsnprintf(m, sizeof(m), fmt, args);

        if (n > 0 && m[n - 1] == '\n')
            m[n - 1] = '\0';
        
        p = m;
        if (*p == '/' && strstr(p, ".rpm")) {
            p++;
            q = p;
            while ((p = strchr(q, '/')))
                q = p + 1;
            p = q;
        }

        if (strstr(m, "md5 OK") || strstr(m, "gpg OK") || strstr(m, "pgp OK"))
            logpri |= LOGFILE;
        
        log(logpri | LOGWARN, "%s\n", p);
    }
        
    va_end(args);
}

#endif /* HAVE_RPMLOG */
    
        
int rpmvercmp(const char *one, const char *two);
int pm_rpm_vercmp(const char *one, const char *two)
{
    return rpmvercmp(one, two);
}
