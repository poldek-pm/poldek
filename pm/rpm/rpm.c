/*
  Copyright (C) 2000 - 2007 Pawel A. Gajda <mis@pld-linux.org>

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
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/stat.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nstr.h>
#include <trurl/nmalloc.h>
#include <trurl/n_snprintf.h>


#include "vfile/vfile.h"
#include "sigint/sigint.h"
#include "i18n.h"

#include "depdirs.h"
#include "misc.h"
#include "log.h"
#include "pkg.h"
#include "capreq.h"
#include "pkgdir/pkgdir.h"
#include "pm_rpm.h"

static int initialized = 0;
int pm_rpm_verbose = 0;

void pm_rpm_destroy(void *pm_rpm) 
{
    struct pm_rpm *pm = pm_rpm;
    
    n_cfree(&pm->rpm);
    n_cfree(&pm->sudo);
    n_cfree(&pm->default_dbpath);
    n_array_cfree(&pm->caps);
    free(pm);
}

#define RPM_DEFAULT_DBPATH "/var/lib/rpm"

void *pm_rpm_init(void) 
{
    struct pm_rpm *pm_rpm;
    char *p;
    
    if (initialized == 0) {
	/* lp#1644315: save original umask and restore it after rpmReadConfigFiles() call */
	mode_t mask = umask(0);

        if (rpmReadConfigFiles(NULL, NULL) != 0) {
            logn(LOGERR, "failed to read rpmlib configs");
            return 0;
        }
        initialized = 1;

        umask(mask);
    }

    pm_rpm = n_malloc(sizeof(*pm_rpm));
    memset(pm_rpm, 0, sizeof(*pm_rpm));
    pm_rpm->rpm = NULL;
    pm_rpm->sudo = NULL;

    p = (char*)rpmGetPath("%{_dbpath}", NULL);
    if (p == NULL || *p == '%')
        p = RPM_DEFAULT_DBPATH;
    
    pm_rpm->default_dbpath = n_strdup(p);

    pm_rpm->caps = NULL;
    return pm_rpm;
}


int pm_rpm_configure(void *pm_rpm, const char *key, void *val)
{
    struct pm_rpm *pm = pm_rpm;
    
    if (*key == '%') {           /* macro */
        key++;
        msg(4, "addMacro %s %s\n", key, (char*)val);
        addMacro(NULL, key, NULL, val, RMIL_DEFAULT);
        
    } else if (n_str_eq(key, "pmcmd")) {
        n_cfree(&pm->rpm);
        if (val)
            pm->rpm = n_strdup(val);
        DBGF("%s %s\n", key, val);

    } else if (n_str_eq(key, "sudocmd")) {
        n_cfree(&pm->sudo);
        if (val)
            pm->sudo = n_strdup(val);
        
    } else if (n_str_eq(key, "macros")) {
        tn_array *macros = val;
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
    return 1;
}

int pm_rpm_conf_get(void *pm_rpm, const char *key, char *value, int vsize)
{
    int n = 0;

    pm_rpm = pm_rpm;
    
    if (*key == '%') {
        char *v = rpmExpand(key, NULL);
        
        if (v == NULL)
            return 0;

        if (strlen(v))          /* rpmExpand returns empty strings */
            n = n_snprintf(value, vsize, "%s", v);

        free(v);
    }
    
    return n;
}


static int db_exists(void *pm_rpm, const char *rootdir, const char *dbpath)
{
    char            rpmdb_path[PATH_MAX], tmp[PATH_MAX];

    if (!dbpath) 
        dbpath = pm_rpm_dbpath(pm_rpm, tmp, sizeof(tmp));
    
    if (!dbpath)
        return 0;
    
    *rpmdb_path = '\0';
    n_snprintf(rpmdb_path, sizeof(rpmdb_path), "%s%s",
               rootdir ? (*(rootdir + 1) == '\0' ? "" : rootdir) : "",
               dbpath != NULL ? dbpath : "");
    
    if (*rpmdb_path == '\0')
        return 0;

    if (access(rpmdb_path, R_OK) != 0)
        return 0;

    if (pm_rpm_dbmtime(pm_rpm, rpmdb_path) == 0)
        return 0;

    return 1;
}

rpmdb pm_rpm_opendb(void *pm_rpm, void *dbh,
                    const char *rootdir, const char *dbpath,
                    mode_t mode, tn_hash *kw)
{
    struct pm_rpm *pm = pm_rpm;
    rpmdb db = NULL;
    int rc = 1;
    
    dbh = dbh; kw = kw;
    
    if (dbpath)
        addMacro(NULL, "_dbpath", NULL, dbpath, RMIL_DEFAULT);
    
    DBGF("root %s, dir %s, mode %d\n", rootdir, dbpath, mode);
        
    /* a workaround, rpmdbOpen succeeds even if database does not exists */
    if (mode == O_RDONLY && !db_exists(pm_rpm, rootdir, dbpath)) {
        logn(LOGERR, _("%s%s: rpm database does not exists"),
             rootdir ? rootdir:"", dbpath ? dbpath : pm->default_dbpath);
        rc = 0;
    }

    if (rc && rpmdbOpen(rootdir ? rootdir : "/", &db, mode, 0) != 0) {
        logn(LOGERR, _("%s%s: open rpm database failed"),
             rootdir ? rootdir:"", dbpath ? dbpath : pm->default_dbpath);
        rc = 0;
		abort(); // XXX maybe re-exec ourselves after poldek binary upgrade?
    }
    
#if ENABLE_TRACE    
    DBGF("%p %d\n", db, db->nrefs);
    system("ls -l /proc/$(echo `ps aux | grep poldek | grep -v grep` | awk '{print $2}')/fd/ | grep rpm");
    sleep(3);
#endif
    
    if (rc == 0)
        db = NULL;

    /* restore non-default dbpath */
    if (dbpath && strcmp(dbpath, pm->default_dbpath) != 0)
        addMacro(NULL, "_dbpath", NULL, pm->default_dbpath, RMIL_DEFAULT);
    
    return db;
}

char *pm_rpm_dbpath(void *pm_rpm, char *path, size_t size)
{
    char *p;

    pm_rpm = pm_rpm;
    n_assert(initialized);
    p = (char*)rpmGetPath("%{_dbpath}", NULL);
    if (p == NULL || *p == '%')
        n_snprintf(path, size, "%s", RPM_DEFAULT_DBPATH);
    else
        n_snprintf(path, size, "%s", p);

    if (p)
        free(p);
        
    return path;
}

time_t pm_rpm_dbmtime(void *pm_rpm, const char *dbpath) 
{
    const char *file = "Packages";
    char path[PATH_MAX];
    struct stat st;

    pm_rpm = pm_rpm;
    
    snprintf(path, sizeof(path), "%s/%s", dbpath, file);
     
    if (stat(path, &st) != 0)
        return 0;

    return st.st_mtime;
}

void pm_rpm_closedb(rpmdb db) 
{
    DBGF("DB %p\n", db);
    
    rpmdbClose(db);
    
#if ENABLE_TRACE        
    system("ls -l /proc/$(echo `ps aux | grep poldek | grep -v grep` | awk '{print $2}')/fd/ | grep rpm");
    sleep(3);
#endif
    
    db = NULL;
}

struct pkgdir *pm_rpm_db_to_pkgdir(void *pm_rpm, const char *rootdir,
                                   const char *dbpath, unsigned pkgdir_ldflags,
                                   tn_hash *kw)
{
    char            rpmdb_path[PATH_MAX], tmpdbpath[PATH_MAX];
    const char      *lc_lang;
    struct pkgdir   *dir = NULL;

    kw = kw;

    if (!dbpath) 
        dbpath = pm_rpm_dbpath(pm_rpm, tmpdbpath, sizeof(tmpdbpath));
    
    if (!dbpath)
        return NULL;

    *rpmdb_path = '\0';
    n_snprintf(rpmdb_path, sizeof(rpmdb_path), "%s%s",
               rootdir ? (*(rootdir + 1) == '\0' ? "" : rootdir) : "",
               dbpath != NULL ? dbpath : "");
    
    if (*rpmdb_path == '\0')
        return NULL;

    lc_lang = lc_messages_lang();
    dir = pkgdir_open_ext(rpmdb_path, NULL, "rpmdb", dbpath, NULL, 0, lc_lang);
    if (dir != NULL) {
        struct pkgdir *prev_pkgdir = NULL;
        if (kw && (prev_pkgdir = n_hash_get(kw, "prev_pkgdir")))
            dir->prev_pkgdir = prev_pkgdir;
        
        if (!pkgdir_load(dir, NULL, pkgdir_ldflags | PKGDIR_LD_NOUNIQ)) {
            pkgdir_free(dir);
            dir = NULL;
        }
    }

    return dir;
}

#if !defined ENABLE_STATIC && (defined HAVE_RPMLOG || defined HAVE_VRPMLOG)
/* XXX hack: rpmlib dumps messges to stdout only... (AFAIK)  */
#if defined HAVE_RPMLOG
void rpmlog(int prii, const char *fmt, ...) __attribute__ ((visibility("default")));

void rpmlog(int prii, const char *fmt, ...)
{
    va_list args;
#elif defined HAVE_VRPMLOG
void vrpmlog(unsigned prii, const char *fmt, va_list args) __attribute__ ((visibility("default")));

void vrpmlog(unsigned prii, const char *fmt, va_list args)
{
#endif
    int pri, mask;
    int rpmlogMask, logpri = LOGERR, verbose_level = -1;

    pri =  RPMLOG_PRI(prii);
    mask = RPMLOG_MASK(pri);
    rpmlogMask = rpmlogSetMask(0); /* get mask */
        
    if ((mask & rpmlogMask) == 0)
	return;

    if (pri <= RPMLOG_ERR)
        logpri = LOGERR;
    
    else if (pri == RPMLOG_WARNING)
        logpri = LOGWARN;
    
    else if (pri == RPMLOG_NOTICE) {
        logpri = LOGNOTICE;
        verbose_level = 2;
        
    } else {
        logpri = LOGINFO;
        verbose_level = 2;
    }

#if defined HAVE_RPMLOG
    va_start(args, fmt);
#endif

#if 0
    printf("%d, v = %d, verbose = %d, pm_rpm_verbose = %d\n", pri,
           verbose_level, poldek_VERBOSE, pm_rpm_verbose);
    vprintf(fmt, args);
#endif
    
    if (verbose_level > poldek_VERBOSE || verbose_level > pm_rpm_verbose)
        return;
    
    if ((logpri & (LOGERR | LOGWARN)) == 0)
        poldek_vlog(logpri, 0, fmt, args);
        
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
        
#if defined HAVE_RPMLOG
    va_end(args);
#endif
}
#endif /* HAVE_RPMLOG */

int pm_rpm_vercmp(const char *one, const char *two)
{
    DBGF("(%s) (%s) %d\n", one, two, rpmvercmp(one, two));
    return rpmvercmp(one, two);
}
