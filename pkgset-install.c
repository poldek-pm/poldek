/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <obstack.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>


#include <rpm/rpmlib.h>
#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>

#include <vfile/vfile.h>
#include <vfile/p_open.h>

#include "log.h"
#include "pkg.h"
#include "pkgset-def.h"
#include "pkgset.h"
#include "misc.h"
#include "rpmadds.h"
#include "rpmhdr.h"
#include "pkgset-req.h"
#include "dbpkg.h"
#include "rpmdb_it.h"


#define INST_INSTALL  1
#define INST_UPGRADE  2

struct upgrade_s {
    tn_array       *avpkgs;     
    tn_array       *install_pkgs; /* pkgs to install */
    tn_hash        *depcache;     /* cache of resolved db dependencies */
    
    tn_array       *uninst_dbpkgs;    /* array of uninst_pkg* */
    tn_hash        *capcache;         /* cache of resolved uninst packages caps */
    tn_array       *orphan_dbpkgs;    /* array of uninst_pkg* */
    
    tn_array       *orphan_pkgs;  /* packages which requires
                                     uninstalled ones */
    tn_array       *orphan_rnos;  /* recnos of above  */

    int            strict;
    int            ndberrs;
    int            ndep;
    int            ninstall;
    int            ndep_err;
    int            ncnfl_err;
    int            nfatal_err;
    struct inst_s  *inst;

    void           *pkgflmod_mark;
};


/* anyone of pkgs is marked? */
__inline__
static int one_is_marked(struct pkg *pkgs[], int npkgs) 
{
    int i;

    for (i=0; i<npkgs; i++) 
        if (pkg_is_marked(pkgs[i])) 
            return 1;
    
    return 0;
}


static int dump_pkgs_fqpns(struct pkgset *ps, struct upgrade_s *upg)
{
    int i;
    FILE *stream = stdout;
    
    if (upg->inst->dumpfile) {
        if ((stream = fopen(upg->inst->dumpfile, "w")) == NULL) {
            log(LOGERR, "fopen %s: %m\n", upg->inst->dumpfile);
            return 0;
        }
        fprintf(stream, "# Packages to install (in the right order)\n");
    }
    
    for (i=0; i<n_array_size(ps->ordered_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->ordered_pkgs, i);
        if (pkg_is_marked(pkg))
            fprintf(stream, "%s\n", pkg_filename_s(pkg));
    }
    
    return 1;
}


int pkgset_fetch_pkgs(struct pkgset *ps, const char *destdir, tn_array *pkgs) 
{
    int rc, i, urltype;
    tn_array *urls = NULL;
    
    if (ps->path == NULL) {
        log(LOGERR, "Oj! Packages URL not set!?\n");
        return 0;
    }

    if ((urltype = vfile_url_type(ps->path)) == VFURL_PATH) {
        log(LOGERR, "Think! Packages path is not remote URL\n");
        return 0;
    }

    if (n_array_size(pkgs) == 1) {
        char path[PATH_MAX];
        
        snprintf(path, sizeof(path), "%s/%s", ps->path,
                 pkg_filename_s(n_array_nth(pkgs, 0)));
        
        return vfile_fetch(destdir, path, urltype);
    } 

    urls = n_array_new(n_array_size(pkgs), NULL, NULL);
    
    for (i=0; i<n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        char path[PATH_MAX], *s;
        int len;

        if (pkg->dn) 
            len = snprintf(path, sizeof(path), "%s/%s/%s", ps->path, pkg->dn,
                           pkg_filename_s(pkg));
        else
            len = snprintf(path, sizeof(path), "%s/%s", ps->path,
                           pkg_filename_s(pkg));
        
        s = alloca(len + 1);
        memcpy(s, path, len);
        s[len] = '\0';
        n_array_push(urls, s);
    }
    
    rc = vfile_fetcha(destdir, urls, urltype);
    if (urls)
        n_array_free(urls);
    return rc;
}


static void process_rpm_output(struct p_open_st *st) 
{
    int c;
    
    //while ((c = fgetc(st->stream)) != EOF) {
    //  printf("%c", c);
        //msg(1, "_%c", c);
    //}
    /*while ((c = fgetc(st->stream)) != EOF)*/
    while (read(st->fd, &c, 1) == 1)
        msg(1, "_%c", c);
}

static void reaper (int sig)
{
    pid_t pid;

    sig = sig;
    while ((pid = waitpid (-1, NULL, WNOHANG)) > 0) {
	msg(0, "SIGCHLD from %d\n", pid);
    }
    
    signal (SIGCHLD, reaper);
}


static int runrpm(struct pkgset *ps, struct upgrade_s *upg) 
{
    char **argv;
    char *cmd;
    char *local_prefix;
    int i, n, nopts = 0, ec;
    int nv = verbose;
    
    struct p_open_st pst;

    n = 128 + n_array_size(upg->install_pkgs);
    argv = alloca((n + 1) * sizeof(*argv));
    argv[n] = NULL;
    n = 0;
    local_prefix = NULL;
    
    if (pkgset_isremote(ps)) {
        int len;
        char buf[1024];

        vfile_url_as_dirpath(buf, sizeof(buf), ps->path);
        len = strlen(upg->inst->cachedir) + 1 + strlen(buf) + 1;
        local_prefix = alloca(len);
        snprintf(local_prefix, len, "%s/%s", upg->inst->cachedir, buf);
        msg(1, "Downloading...\n");
        if (!pkgset_fetch_pkgs(ps, local_prefix, upg->install_pkgs))
            return 0;
    }
    
    if (upg->inst->instflags & PKGINST_TEST) {
        cmd = "/bin/rpm";
        argv[n++] = "rpm";
    } else if (upg->inst->flags & INSTS_USESUDO) {
        cmd = "/usr/bin/sudo";
        argv[n++] = "sudo";
        argv[n++] = "/bin/rpm";
    } else {
        cmd = "/bin/rpm";
        argv[n++] = "rpm";
    }
    
    if (ps->flags & PSMODE_INSTALL)
        argv[n++] = "--install";
    else if (ps->flags & PSMODE_UPGRADE)
        argv[n++] = "--upgrade";
    else {
        n_assert(0);
        die();
    }

    if (nv > 0) {
        argv[n++] = "-vh";
        nv--;
    }

    if (nv > 0)
        nv--;
    
    while (nv-- > 0) 
        argv[n++] = "-v";
    
    if (upg->inst->instflags & PKGINST_TEST)
        argv[n++] = "--test";
    
    if (upg->inst->instflags & PKGINST_JUSTDB)
        argv[n++] = "--justdb";
        
    if (upg->inst->instflags & PKGINST_FORCE)
        argv[n++] = "--force";
    
    if (upg->inst->instflags & PKGINST_NODEPS)
        argv[n++] = "--nodeps";

    argv[n++] = "--noorder";    /* packages always ordered */
    argv[n++] = "--ignoresize";    /* packages always ordered */
    

    if (upg->inst->rpmacros) 
        for (i=0; i<n_array_size(upg->inst->rpmacros); i++) {
            argv[n++] = "--define";
            argv[n++] = n_array_nth(upg->inst->rpmacros, i);
        }
    
    if (upg->inst->rpmopts) 
        for (i=0; i<n_array_size(upg->inst->rpmopts); i++)
            argv[n++] = n_array_nth(upg->inst->rpmopts, i);
    
    nopts = n;
    for (i=0; i<n_array_size(ps->ordered_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->ordered_pkgs, i);
        if (pkg_is_marked(pkg)) {
            char path[PATH_MAX], *s, *name;
            int len;

            name = pkg_filename_s(pkg);
            if (local_prefix)
                len = snprintf(path, sizeof(path), "%s/%s", local_prefix,
                               name);
            else if (pkg->dn) 
                len = snprintf(path, sizeof(path), "%s/%s/%s", ps->path,
                               pkg->dn, name);
            else if (ps->path) 
                len = snprintf(path, sizeof(path), "%s/%s", ps->path, name);
            
            else
                len = snprintf(path, sizeof(path), "%s", name);

            
            s = alloca(len + 1);
            memcpy(s, path, len);
            s[len] = '\0';
            argv[n++] = s;
        }
    }
    
        
    n_assert(n > nopts); 
    argv[n++] = NULL;

    if (verbose > 0) {
        char buf[1024], *p;
        p = buf;
        
        for (i=0; i<nopts; i++) 
            p += snprintf(p, &buf[sizeof(buf) - 1] - p, " %s", argv[i]);
        *p = '\0';
        msg(1, "Running%s...\n", buf);
    }
    
#define EXEC_RPM 1
#if EXEC_RPM    
    ec = exec_rpm(cmd, argv);
    
#else 
    signal(SIGCHLD, reaper);
    p_st_init(&pst);
    if (p_open(&pst, cmd, argv) == NULL) 
        return 0;
    
    n = 0;
    if (verbose == 0) {
        verbose = 1;
        n = 1;
    }

    process_rpm_output(&pst);
    if ((ec = p_close(&pst) != 0))
        log(LOGERR, "%s", pst.errmsg);

    p_st_destroy(&pst);
#endif
    if (n)
        verbose--;

    return ec == 0;
}

static
int is_installable(struct pkgdb *db, struct pkg *pkg, tn_array *uninst_dbpkgs,
                   struct inst_s *inst) 
{
    int rc, cmprc = 0;
    struct dbrec dbrec = {0, 0};
    
    rc = rpm_is_pkg_installed(db->dbh, pkg, &cmprc, &dbrec);

    if (rc < 0)
        die();
    
    
    if (rc == 0)
        return 1;

    if (rc > 1) {
        log(LOGERR, "%s: multiple instances installed, give up\n", pkg->name);
        rc = 0;
        
    } else if (cmprc <= 0 && (inst->instflags & PKGINST_FORCE) == 0) {
        if ((inst->flags & INSTS_FRESHEN) == 0)
            msg(0, "%s: %s version installed, skiped\n",
                pkg_snprintf_s(pkg), cmprc == 0 ? "equal" : "newer");
        rc = 0;
        
    } else {
        msg(2, "%s will be uninstalled\n", dbrec_snprintf_s(&dbrec));
        n_array_push(uninst_dbpkgs, dbpkg_new(dbrec.recno, dbrec.h, PKG_LDWHOLE));
        rc = 1;
    }
    
    dbrec_clean(&dbrec);
    return rc;
}

int uninstpkgs_provides(struct upgrade_s *upg, struct capreq *req) 
{
    int i;

    if (n_hash_exists(upg->capcache, capreq_name(req))) {
        msg(4, "capcahe hit %s\n", capreq_name(req));
        return 1;
    }
    
    for (i=0; i<n_array_size(upg->uninst_dbpkgs); i++) {
        struct dbpkg *dbpkg = n_array_nth(upg->uninst_dbpkgs, i);
        if (pkg_match_req(dbpkg->pkg, req, 0)) {
            n_hash_insert(upg->capcache, capreq_name(req), NULL);
            return 1;
        }
    }
    return 0;
}

    

static void mapfn_clean_pkg_color(struct pkg *pkg) 
{
    pkg_set_color(pkg, PKG_COLOR_WHITE);
}

#define PROCESS_DEPS       0 
#define PROCESS_ORPHANS    1

static int process_deps(struct pkgset *ps, tn_array *pkgs,
                        struct upgrade_s *upg, int how) 
{
    int i, j, ndepadds = 0;
    tn_array *markarr[2], *tmparr;
    int nmarkarr = 0, nmarked, nloop;
    
    tmparr = pkgs;
    nmarked = n_array_size(tmparr);
    markarr[0] = n_array_new(64, NULL, NULL);
    markarr[1] = n_array_new(64, NULL, NULL);
    
    nloop = 0;
    
    while (nmarked > 0) {
        nmarked = 0;
        for (i=0; i<n_array_size(tmparr); i++) {
            struct pkg *pkg = n_array_nth(tmparr, i);
            if (!pkg_is_color(pkg, PKG_COLOR_WHITE))
                continue;
            
            pkg_set_color(pkg, PKG_COLOR_GRAY); /* processed */
            if (pkg->reqs == NULL)
                continue;
            
            msg_i(2, nloop, "%s\n", pkg_snprintf_s(pkg));

            for (j=0; j<n_array_size(pkg->reqs); j++) {
                struct capreq *req;
                struct pkg **suspkgs, pkgsbuf[1024], *tomark = NULL;
                int nsuspkgs = 0;
                char *reqname;
                int reqnover;
                
                req = n_array_nth(pkg->reqs, j);
                reqname = capreq_name(req);
                reqnover = capreq_has_ver(req) == 0;
            
                if (reqnover && n_hash_exists(upg->depcache, reqname)) {
                    msg_i(4, nloop, "in cache %s\n", reqname);
                    continue;
                }

                
                if (how == PROCESS_ORPHANS && !uninstpkgs_provides(upg, req)) {
                    msg(5, "skiped %s\n", reqname);
                    continue;
                }
                
                /* lookup in pkgset */
                if (psreq_lookup(ps, req, &suspkgs, (struct pkg **)pkgsbuf,
                                 &nsuspkgs)) {
                    int nmatched = 0;
                    struct pkg **matches;
                    
                    if (nsuspkgs == 0)
                        continue;

                    matches = alloca(sizeof(*matches) * nsuspkgs);
                    
                    if (psreq_match_pkgs(pkg, req, upg->strict, suspkgs,
                                         nsuspkgs, matches, &nmatched)) {
                        
                        /* already marked for upgrade */
                        if (nmatched == 0 || one_is_marked(matches, nmatched)){
                            msg_i(2, nloop, " %s satisfied by install set\n",
                                  capreq_snprintf_s(req));
                            if (reqnover)
                                n_hash_insert(upg->depcache, reqname, (void*)1);
                            continue;
                            
                        } else if ((upg->inst->flags & INSTS_FRESHEN) == 0) {
                            /* save candidate */
                            tomark = matches[0];
                        }
                    }
                } 
            
                if (pkgdb_match_req(upg->inst->db, req, upg->strict,
                                    upg->uninst_dbpkgs)) {
                    msg_i(2, nloop, " %s satisfied by db\n",
                          capreq_snprintf_s(req));
                    if (reqnover)
                        n_hash_insert(upg->depcache, reqname, (void*)1);
                
                } else if (tomark) {
                    if (how == PROCESS_DEPS) {
                        if (verbose > 1) {
                            msg_i(2, nloop, " %s marks %s (cap %s)\n",
                                  pkg_snprintf_s(pkg), pkg_snprintf_s0(tomark),
                                  capreq_snprintf_s(req));
                        } else if (verbose) {
                            msg(1, "%s marks %s (cap %s)\n",
                                pkg_snprintf_s(pkg), pkg_snprintf_s0(tomark), 
                                capreq_snprintf_s(req));
                        }
                    
                        if (!is_installable(upg->inst->db, tomark,
                                            upg->uninst_dbpkgs, 
                                            upg->inst)) {
                            upg->nfatal_err++; 
                            ndepadds = 0;
                            goto l_end;
                        }
                    
                        pkg_dep_mark(tomark);
                        n_array_push(markarr[nmarkarr], tomark);
                        nmarked++;
                        upg->ndep++;
                        upg->ninstall++;
                        ndepadds++;
                    }
                    
                    if (reqnover)
                        n_hash_insert(upg->depcache, reqname, (void*)1);
                    
                } else {
                    if (how == PROCESS_DEPS) 
                        log(LOGERR, "%s: req %s not found\n",
                            pkg_snprintf_s(pkg), capreq_snprintf_s(req));
                    else if (how == PROCESS_ORPHANS)
                        log(LOGERR, "%s is required by %s\n", 
                            capreq_snprintf_s(req), pkg_snprintf_s(pkg));
                    else
                        n_assert(0);
                    pkg_set_badreqs(pkg);
                    upg->ndep_err++;
                }
            }
        }

        for (i=0; i<n_array_size(markarr[nmarkarr]); i++) 
            n_array_push(upg->install_pkgs, n_array_nth(markarr[nmarkarr], i));
        
        /* swap tables */
        tmparr = markarr[nmarkarr];
        nmarkarr++;
        nmarkarr %= 2;
        n_array_clean(markarr[nmarkarr]);
        nloop++;
        }
    
 l_end:
    
    n_array_free(markarr[0]);
    n_array_free(markarr[1]);
    
    return ndepadds;
}

/* add to upg->uninst_dbpkgs packages obsoleted by pkg */
static
int add_obsoleted_pkgs(const struct pkg *pkg, struct upgrade_s *upg) 
{
    struct capreq *self_cap;
    int i, k, n = 0;
    int j, idx;
    
    idx = k = n_array_size(upg->uninst_dbpkgs);
    self_cap = capreq_new(pkg->name, 0, NULL, NULL, 0);
    rpm_get_obsoletedby_cap(upg->inst->db->dbh, upg->uninst_dbpkgs, self_cap,
                            PKG_LDWHOLE);
    capreq_free(self_cap);
    n = n_array_size(upg->uninst_dbpkgs) - k;

    if (n) {
        for (j=idx; j<n_array_size(upg->uninst_dbpkgs); j++)
            msg(1, "%s obsoleted by %s\n",
                dbpkg_snprintf_s(n_array_nth(upg->uninst_dbpkgs, j)),
                pkg_snprintf_s(pkg));
    }
    
    if (pkg->cnfls == NULL)
        return n;
    
    k = n_array_size(upg->uninst_dbpkgs);
    
    for (i=0; i < n_array_size(pkg->cnfls); i++) {
        struct capreq *cnfl = n_array_nth(pkg->cnfls, i);
        
        
        if (!cnfl_is_obsl(cnfl))
            continue;

        idx = n_array_size(upg->uninst_dbpkgs);
        if (rpm_get_obsoletedby_cap(upg->inst->db->dbh, upg->uninst_dbpkgs,
                                    cnfl, PKG_LDWHOLE)) {
            
            for (j=idx; j<n_array_size(upg->uninst_dbpkgs); j++)
                msg(1, "%s obsoleted by %s\n",
                    dbpkg_snprintf_s(n_array_nth(upg->uninst_dbpkgs, j)),
                    pkg_snprintf_s(pkg));
        }
    }

    n += n_array_size(upg->uninst_dbpkgs) - k;
    return n;
}

/* add to upg->orphan_dbpkgs packages which are required by uninst_dbpkgs */
static
int add_orphans(struct upgrade_s *upg) 
{
    int i, j, k, n = 0;
    unsigned ldflags = PKG_LDNEVR | PKG_LDREQS;

    //tn_array *uninst_caps = capreq_arr_new();
    
    mem_info(1, "add_orphans:");
    for (i=0; i < n_array_size(upg->uninst_dbpkgs); i++) {
        struct dbpkg *dbpkg = n_array_nth(upg->uninst_dbpkgs, i);
        struct pkg *pkg = dbpkg->pkg;
        rpmdb dbh = upg->inst->db->dbh;
        
        if (dbpkg->flags & DBPKG_ORPHANS_PROCESSED)
            continue;
        
        n++;
        dbpkg->flags |= DBPKG_ORPHANS_PROCESSED;
        
        rpm_get_pkgs_requires_capn(dbh, upg->orphan_dbpkgs, pkg->name,
                                   upg->uninst_dbpkgs, ldflags);
        
        if (pkg->caps)
            for (j=0; j<n_array_size(pkg->caps); j++) {
                struct capreq *cap = n_array_nth(pkg->caps, j);
                rpm_get_pkgs_requires_capn(dbh, upg->orphan_dbpkgs,
                                           capreq_name(cap),
                                           upg->uninst_dbpkgs, ldflags);
            }

        if (pkg->fl)
            for (j=0; j<n_array_size(pkg->fl); j++) {
                struct pkgfl_ent *flent = n_array_nth(pkg->fl, j);
                char path[PATH_MAX], *endp;

                endp = path;
                if (*flent->dirname != '/')
                    *endp++ = '/';

                endp = n_strncpy(endp, flent->dirname, sizeof(path));
                
                for (k=0; k<flent->items; k++) {
                    struct flfile *file = flent->files[k];
                    int path_left_size;

                    if (*(endp - 1) != '/')
                        *endp++ = '/';

                    path_left_size = sizeof(path) - (endp - path);
                    n_strncpy(endp, file->basename, path_left_size);
                    rpm_get_pkgs_requires_capn(dbh, upg->orphan_dbpkgs, path,
                                               upg->uninst_dbpkgs, ldflags);
                    if (S_ISLNK(file->mode)) {
                        char *linkto = strchr(file->basename, '\0') + 1;
                        n_strncpy(endp, linkto, path_left_size);
                        rpm_get_pkgs_requires_capn(dbh, upg->orphan_dbpkgs, path,
                                                   upg->uninst_dbpkgs, ldflags);
                    }
                }
            }
    }
    
    return n;
}

static
void process_dependecies(struct pkgset *ps, struct upgrade_s *upg) 
{
    tn_array *opkgs;
    
    opkgs = n_array_new(128, NULL, (tn_fn_cmp)pkg_cmp_name_evr_rev);
    
    while (1) {
        int i, norphans_added = 0;
        
        process_deps(ps, upg->install_pkgs, upg, PROCESS_DEPS);
        
        for (i=0; i<n_array_size(upg->install_pkgs); i++) {
            struct pkg *pkg = n_array_nth(upg->install_pkgs, i);
            if (pkg_is_color(pkg, PKG_COLOR_GRAY)) {
                add_obsoleted_pkgs(pkg, upg);
                pkg_set_color(pkg, PKG_COLOR_BLACK);
            }
        }
        
        norphans_added = add_orphans(upg);
        if (norphans_added == 0)
            break;
        
        for (i=0; i<n_array_size(upg->orphan_dbpkgs); i++) {
            struct dbpkg *dbpkg = n_array_nth(upg->orphan_dbpkgs, i);
            msg(2, "orphaned %u %s\n", dbpkg->recno,
                pkg_snprintf_s(dbpkg->pkg));
            n_array_push(opkgs, dbpkg->pkg);
        }
        
        process_deps(ps, opkgs, upg, PROCESS_ORPHANS);
        n_array_clean(opkgs);
    }
    
    n_array_free(opkgs);
}

#ifdef ENABLE_FILES_CONFLICTS
static
int is_file_conflict(const struct pkg *pkg,
                     const char *dirname, const struct flfile *flfile,
                     struct dbpkg *dbpkg, int strict) 
{
    struct rpmhdr_fl hdrfl;
    int i, is_cnfl = 0;
    
    for (i=0; i<hdrfl.nbnames; i++) {
        char *dn;

        if (strcmp(hdrfl.bnames[i], flfile->basename) != 0)
            continue;

        dn = hdrfl.dnames[hdrfl.diridxs[i]];
        if (*(dn+1) != '\0') {    /* skip leading '/' */
            char *p;
            
            dn++;
            if ((p = strrchr(dn, '/')))
                *p = '\0';
        }
        
        if (strcmp(dn, dirname) != 0)
            continue;
        
        is_cnfl = flfile_cnfl2(flfile, hdrfl.sizes[i], hdrfl.modes[i],
                              hdrfl.symlinks ? hdrfl.symlinks[i] : NULL,
                              strict);
        
        if (is_cnfl) {
            log(LOGERR, "%s: /%s/%s%s: conflicts with %s's one\n",
                pkg_snprintf_s(pkg),
                dirname, flfile->basename,
                S_ISDIR(flfile->mode) ? "/" : "", dbpkg_snprintf_s(dbpkg));
            
        } else if (verbose > 1) {
            msg(2, "/%s/%s%s: shared between %s and %s\n",
                dirname, flfile->basename,
                S_ISDIR(flfile->mode) ? "/" : "",
                pkg_snprintf_s(pkg), dbpkg_snprintf_s(dbpkg));
        }
    }
    
    rpmhdr_fl_free(&hdrfl);
    return is_cnfl;
}
    

static 
int find_file_conflicts(struct pkgfl_ent *flent, struct pkg *pkg,
                        struct pkgdb *db, tn_array *uninst_dbpkgs,
                        int strict) 
{
    int i, j, ncnfl = 0;
    
    for (i=0; i<flent->items; i++) {
        tn_array *cnfldbpkgs;
        char path[PATH_MAX];
        
        snprintf(path, sizeof(path), "/%s/%s", flent->dirname,
                 flent->files[i]->basename);
        
        cnfldbpkgs = rpm_get_file_conflicted_dbpkgs(db->dbh, flent->files[i]->basename, 
                                                    uninst_dbpkgs, PKG_LDWHOLE);
        if (cnfldbpkgs == NULL)
            continue;

        for (j=0; j<n_array_size(cnfldbpkgs); j++)
            ncnfl += is_file_conflict(pkg, flent->dirname, flent->files[i],
                                      n_array_nth(cnfldbpkgs, j), strict);
        n_array_free(cnfldbpkgs);
    }
    
    return ncnfl;
}



static
int find_db_files_conflicts(struct pkg *pkg, struct pkgdb *db,
                            tn_array *uninst_dbpkgs, int strict)
{
    tn_array *fl;
    int i, ncnfl = 0;
    if (pkg->fl)
        for (i=0; i < n_array_size(pkg->fl); i++) {
            ncnfl += find_file_conflicts(n_array_nth(pkg->fl, i), pkg, db,
                                         uninst_dbpkgs, strict);
        }

    if (ncnfl)                  /* skip the rest conflicts test */
        return ncnfl;
    
    n_assert(pkg->pkg_stream != NULL);
    if (pkg->pkg_stream == NULL)
        die();
    
    fseek(pkg->pkg_stream, pkg->other_files_offs, SEEK_SET);
    fl = pkgfl_restore_f(pkg->pkg_stream);
    
    for (i=0; i < n_array_size(fl); i++) {
        ncnfl += find_file_conflicts(n_array_nth(fl, i), pkg, db,
                                     uninst_dbpkgs, strict);
    }
    
    n_array_free(fl);
    return ncnfl;
}
#endif


int find_db_conflicts(const struct pkg *pkg, const struct capreq *cnfl,
                      tn_array *dbpkgs, int strict) 
{
    int i, ncnfl = 0;
    
    for (i=0; i<n_array_size(dbpkgs); i++) {
        struct dbpkg *dbpkg = n_array_nth(dbpkgs, i);
        
        msg(6, "%s (%s) <-> %s ?\n", pkg_snprintf_s(pkg),
            capreq_snprintf_s(cnfl), pkg_snprintf_s0(dbpkg->pkg));
        
        if (pkg_match_req(dbpkg->pkg, cnfl, strict)) {
            log(LOGERR, "%s (%s) conflicts with installed %s\n",
                pkg_snprintf_s(pkg), capreq_snprintf_s(cnfl), 
                pkg_snprintf_s0(dbpkg->pkg));
            
            ncnfl++;
        }
    }
    
    return ncnfl;
}

int find_db_conflicts2(const struct pkg *pkg, const struct capreq *cap,
                       tn_array *dbpkgs, int strict) 
{
    int i, j, ncnfl = 0;


    strict = strict;
    
    for (i=0; i<n_array_size(dbpkgs); i++) {
        struct dbpkg *dbpkg = n_array_nth(dbpkgs, i);
        
        msg(6, "%s (%s) <-> %s ?\n", pkg_snprintf_s(pkg),
            capreq_snprintf_s(cap), pkg_snprintf_s0(dbpkg->pkg));
        
        for (j=0; j<n_array_size(dbpkg->pkg->cnfls); j++) {
            struct capreq *cnfl = n_array_nth(dbpkg->pkg->cnfls, j);
            if (cap_match_req(cap, cnfl, 0))
                log(LOGERR, "%s (%s) conflicts with installed %s (%s)\n",
                    pkg_snprintf_s(pkg), capreq_snprintf_s(cap), 
                    pkg_snprintf_s0(dbpkg->pkg),capreq_snprintf_s0(cnfl));
            ncnfl++;
        }
    }
    
    return ncnfl;
}


int find_conflicts(struct upgrade_s *upg, int *install_set_cnfl) 
{
    int i, j, ncnfl = 0, nisetcnfl = 0;
    rpmdb dbh;

    dbh = upg->inst->db->dbh;
    
    for (i=0; i<n_array_size(upg->install_pkgs); i++) {
        struct pkg *pkg = n_array_nth(upg->install_pkgs, i);
        
        msg(2, " checking %s\n", pkg_snprintf_s(pkg));
        
        if (pkg->cnflpkgs != NULL)
            for (j = 0; j < n_array_size(pkg->cnflpkgs); j++) {
                struct cnflpkg *cpkg = n_array_nth(pkg->cnflpkgs, j);
                
                if (pkg_is_marked(cpkg->pkg)) {
                    log(LOGERR, "%s conflicts with %s\n",
                        pkg_snprintf_s(pkg), pkg_snprintf_s0(cpkg->pkg));
                    ncnfl++;
                    nisetcnfl++;
                }
            }

        for (j = 0; j < n_array_size(pkg->caps); j++) {
            struct capreq *cap = n_array_nth(pkg->caps, j);
            tn_array *dbpkgs;

            msg_i(2, 3, "cap %s\n", capreq_snprintf_s(cap));
            dbpkgs = rpm_get_conflicted_dbpkgs(dbh, cap,
                                               upg->uninst_dbpkgs, PKG_LDWHOLE);
            if (dbpkgs == NULL)
                continue;
            
            
            ncnfl += find_db_conflicts2(pkg, cap, dbpkgs, 0);
            n_array_free(dbpkgs);
        }
        
        
        if (pkg->cnfls != NULL)
            for (j = 0; j < n_array_size(pkg->cnfls); j++) {
                struct capreq *cnfl = n_array_nth(pkg->cnfls, j);
                tn_array *dbpkgs;
                
                if (cnfl_is_obsl(cnfl))
                    continue;

                msg_i(2, 3, "cnfl %s\n", capreq_snprintf_s(cnfl));
                
                dbpkgs = rpm_get_provides_dbpkgs(dbh, cnfl,
                                                 upg->uninst_dbpkgs, PKG_LDWHOLE);
                if (dbpkgs != NULL) {
                    ncnfl += find_db_conflicts(pkg, cnfl, dbpkgs, 1);
                    n_array_free(dbpkgs);
                }
            }
        
#ifdef ENABLE_FILES_CONFLICTS  /* too slow, needs rpmlib API modifcations */
        msg_i(1, 3, "files...\n");
        ncnfl += find_db_files_conflicts(pkg, upg->inst->db,
                                         upg->uninst_dbpkgs, upg->strict);
#endif        
    }
    
    *install_set_cnfl = nisetcnfl;
    return ncnfl;
}

    
/* process packages to install:
   - check dependencies
   - mark unresolved dependencies finded in pkgset
   - check conflicts
 */
static
int pkgset_do_install(struct pkgset *ps, struct upgrade_s *upg)
{
    int i, ncnfl = 0, ndbcnfl = 0, rc;
    
    msg(1, "Processing dependencies...\n");

    n_array_map(ps->pkgs, (tn_fn_map1)mapfn_clean_pkg_color);
    process_dependecies(ps, upg);
    if (upg->nfatal_err)
        return 0;
    
    msg(1, "There are %d package(s) to install, %d marked by dependencies:\n",
        upg->ninstall, upg->ndep);
    
    for (i=0; i<n_array_size(upg->install_pkgs); i++) {
        struct pkg *pkg = n_array_nth(upg->install_pkgs, i);
        msg(1, "_%c %s\n", pkg_is_dep_marked(pkg) ? 'D' : 'I', 
            pkg_snprintf_s(pkg));
    }
    
    msg(1, "Verifying conflicts...\n");
    ncnfl = find_conflicts(upg, &ndbcnfl);
    
    if (ndbcnfl) {
        log(LOGERR, "There are conflicts in install set, give up\n");
        return 0;
    }

    /* insert check conflicts against db there ... */
    if (upg->inst->instflags & PKGINST_FORCE) {
        ncnfl = 0;
        ndbcnfl = 0;
    }

    if (ndbcnfl && (upg->inst->instflags & PKGINST_TEST) == 0) {
        log(LOGERR, "Stop\n"); 
        return 0;
    }

    if (upg->ndep_err) {
        log(LOGERR, "There are %d unresolved dependencies.\n", upg->ndep_err);
        if ((upg->inst->instflags & (PKGINST_TEST | PKGINST_NODEPS)) == 0)
            return 0;
        
        if (upg->inst->instflags & PKGINST_NODEPS)
            upg->ndep_err = 0;
    }
    
    
    if (upg->inst->ask_fn) {
        if (!upg->inst->ask_fn("Proceed?"))
            return 1;
    }
    
    pkgdb_closedb(upg->inst->db);
    if (upg->inst->flags & INSTS_JUSTPRINT) {
        rc = dump_pkgs_fqpns(ps, upg);
        
    } else if (upg->inst->flags & INSTS_JUSTFETCH) {
        rc = pkgset_fetch_pkgs(ps, upg->inst->fetchdir, upg->install_pkgs);
        
    } else {
        rc = runrpm(ps, upg);
    }
    
    return rc;
}


/* save in upg->install_pkgs if newer version finded */
static 
void mapfn_chk_newer_pkg(unsigned recno, void *h, void *upgptr) 
{
    struct upgrade_s *upg = upgptr;
    uint32_t *epoch;
    struct pkg *pkg, tmpkg;
    
    if (!rpmhdr_nevr(h, &tmpkg.name, &epoch, &tmpkg.ver, &tmpkg.rel)) {
        log(LOGERR, "db package header corrupted (!?)\n");
        upg->ndberrs++;
        
    } else {
        int i, cmprc;
        tmpkg.epoch = epoch ? *epoch : 0;
        
        i = n_array_bsearch_idx_ex(upg->avpkgs, &tmpkg,
                                   (tn_fn_cmp)pkg_cmp_name); 
        if (i < 0) {
            msg(3, "%-32s not found in repository\n", pkg_snprintf_s(&tmpkg));
            return;
        }

        pkg = n_array_nth(upg->avpkgs, i);
        cmprc = pkg_cmp_evr(pkg, &tmpkg);
        if (verbose) {
            if (cmprc == 0) 
                msg(3, "%-32s up to date\n", pkg_snprintf_s(&tmpkg));
                
            else if (cmprc < 0)
                msg(3, "%-32s newer than repository one\n",
                    pkg_snprintf_s(&tmpkg));
                
            else
                msg(1, "%-32s |-> %-30s\n", pkg_snprintf_s(&tmpkg),
                    pkg_snprintf_s0(pkg));
        }
        
        if (cmprc > 0) {
            pkg_hand_mark(pkg);
            n_array_push(upg->install_pkgs, pkg);
            n_array_push(upg->uninst_dbpkgs, dbpkg_new(recno, h, PKG_LDWHOLE));
        }
    }
}


static void init_upgrade_s(struct upgrade_s *upg, struct pkgset *ps,
                           struct inst_s *inst)
{

    memset(upg, 0, sizeof(*upg));
    upg->pkgflmod_mark = pkgflmodule_allocator_push_mark();
    upg->avpkgs = ps->pkgs;
    upg->install_pkgs = n_array_new(128, NULL, NULL);
    upg->uninst_dbpkgs = dbpkg_array_new(128);
    upg->orphan_dbpkgs = dbpkg_array_new(128);
    
    upg->ndberrs = 0;
    upg->inst = inst;
    upg->depcache = n_hash_new(1003, NULL);
    upg->capcache = n_hash_new(1003, NULL);
    upg->strict = ps->flags & PSVERIFY_MERCY ? 0 : 1;
}


static void destroy_upgrade_s(struct upgrade_s *upg)
{
    upg->avpkgs = NULL;
    n_array_free(upg->install_pkgs);
    n_array_free(upg->uninst_dbpkgs);
    n_array_free(upg->orphan_dbpkgs);
    upg->inst = NULL;
    n_hash_free(upg->depcache);
    n_hash_free(upg->capcache);
    pkgflmodule_allocator_pop_mark(upg->pkgflmod_mark);
    memset(upg, 0, sizeof(*upg));
}


int pkgset_upgrade_dist(struct pkgset *ps, struct inst_s *inst) 
{
    int rc = 1;
    struct upgrade_s upg;

    init_upgrade_s(&upg, ps, inst);
    
    
    msg(1, "_Looking up packages for upgrade...\n");
    
    //set_capreq_allocfn(malloc, free, &capreq_alloc_fn, &capreq_free_fn);

    /* find packages to upgrade */
    pkgdb_map(inst->db, mapfn_chk_newer_pkg, &upg);
    n_array_sort(upg.uninst_dbpkgs);

    // set_capreq_allocfn(capreq_alloc_fn, capreq_free_fn, NULL, NULL);

    if (upg.ndberrs) {
        log(LOGERR, "There are database errors (?), give up\n");
        n_array_free(upg.install_pkgs);
        return 0;
    }
    
    if (n_array_size(upg.install_pkgs) == 0) {
        msg(1, "All packages are up to date, nothing to do\n");
        
    } else {
        rc = pkgset_do_install(ps, &upg);
    }
    
    destroy_upgrade_s(&upg);
    return rc;
}



int pkgset_install(struct pkgset *ps, struct inst_s *inst)
{
    int i, rc = 1, cmprc, is_upgrade = 0;
    struct upgrade_s upg;

    is_upgrade = ps->flags & PSMODE_UPGRADE;
    
    mem_info(1, "ENTER pkgset_install:");
    init_upgrade_s(&upg, ps, inst);
    
    for (i=0; i<n_array_size(ps->pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->pkgs, i);
        int npkgs, install;
        struct dbrec dbrec = {0, 0};
        
        if (!pkg_is_marked(pkg))
            continue;

        install = 0;
        npkgs = rpm_is_pkg_installed(inst->db->dbh, pkg, &cmprc, &dbrec);
        
        if (npkgs < 0) {
            rc = 0;
            
        } else if (npkgs == 0) {
            install = ((inst->flags & INSTS_FRESHEN) == 0);
            
        } else if (npkgs == 1) {
            if (cmprc <= 0 && ((inst->instflags & PKGINST_FORCE) == 0)) {
                if ((inst->flags & INSTS_FRESHEN) == 0)
                    msg(0, "%s: %s version installed, skiped\n",
                        pkg_snprintf_s(pkg), cmprc == 0 ? "equal" : "newer");
            } else {
                install = 1;
            }
            
        } else {
            log(LOGERR, "%s: multiple instances installed, give up\n",
                pkg->name);
            rc = 0;
        }

        if (!install)
            pkg_unmark(pkg);
        else {
            n_array_push(upg.install_pkgs, pkg);
            if (npkgs == 1) {
                n_array_push(upg.uninst_dbpkgs, dbpkg_new(dbrec.recno,
                                                          dbrec.h, PKG_LDWHOLE));
                msg(1, "%s obsoleted by %s\n",
                    dbrec_snprintf_s(&dbrec), pkg_snprintf_s(pkg));
            }
            
            upg.ninstall++;
        }
        dbrec_clean(&dbrec);
    }
    
    n_array_sort(upg.uninst_dbpkgs);

    if (upg.ninstall)
        rc = pkgset_do_install(ps, &upg);
    
    destroy_upgrade_s(&upg);
    mem_info(1, "RETURN pkgset_install:");
    return rc;
}
