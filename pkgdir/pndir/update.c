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

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nbuf.h>
#include <trurl/nstream.h>

#include <vfile/vfile.h>

#define PKGDIR_INTERNAL

#include "i18n.h"
#include "log.h"
#include "pkgdir.h"
#include "pkg.h"
#include "pndir.h"
#include "tags.h"

static char *eat_zlib_ext(char *path)
{
    char *p;

    if ((p = strrchr(n_basenam(path), '.')) != NULL) {
        const char *q = p + 1;
        if (n_str_in(q, COMPR_GZ, COMPR_ZST, NULL))
            *p = '\0';
    }

    return path;
}


/*
  fetch digest and compare it with local one
  RET: 0 - outdated, 1 - up to date, -1 on error
*/
static
int is_uptodate(const char *path, const struct pndir_digest *dg_local,
                struct pndir_digest *dg, const char *pdir_name)
{
    char                   mdpath[PATH_MAX], mdtmpath[PATH_MAX];
    struct pndir_digest    dg_remote;
    int                    fd = 0, n, rc = 0;
    const char             *ext = pndir_digest_ext;

    if (dg)              /* caller wants digest */
        pndir_digest_init(dg);

    pndir_digest_init(&dg_remote);

    if (vf_url_type(path) & VFURL_LOCAL)
        return 1;

    rc = -1;
    if (!(n = vf_mksubdir(mdtmpath, sizeof(mdtmpath), "tmpmd")))
        goto l_end;

    pndir_mkdigest_path(mdpath, sizeof(mdpath), path, ext);

    snprintf(&mdtmpath[n], sizeof(mdtmpath) - n, "/%s", n_basenam(mdpath));
    unlink(mdtmpath);
    mdtmpath[n] = '\0';

    if (!vf_fetch(mdpath, mdtmpath, 0, NULL, pdir_name))
        goto l_end;

    mdtmpath[n] = '/';
    if ((fd = open(mdtmpath, O_RDONLY)) < 0)
        goto l_end;

    if (!pndir_digest_readfd(&dg_remote, fd, mdtmpath))
        goto l_end;

    close(fd);
    fd = 0;

    rc = (memcmp(dg_local->md, &dg_remote.md, sizeof(dg_remote.md)) == 0);

    if (rc == 0 && dg)
        memcpy(dg, &dg_remote, sizeof(dg_remote));

 l_end:
    pndir_digest_destroy(&dg_remote);
    if (fd > 0)
        close(fd);

    return rc;
}

static int update_whole_idx(const struct source *src)
{
    struct pkgdir *pkgdir;
    int rc = 0;

    if ((pkgdir = pkgdir_srcopen(src, PKGDIR_OPEN_REFRESH))) {
        pkgdir_free(pkgdir);
        rc = 1;
    }

    return rc;
}


int pndir_m_update_a(const struct source *src, const char *idxpath,
                     enum pkgdir_uprc *uprc)
{
    struct pndir   *idx;
    struct pkgdir  *pkgdir;
    int            rc = 0;

    idxpath = idxpath;          /* unused */
    pkgdir = pkgdir_srcopen(src, 0);

    *uprc = PKGDIR_UPRC_NIL;
    if (pkgdir == NULL) {       /* broken cached copy */
        rc = update_whole_idx(src);
        if (rc)
            *uprc = PKGDIR_UPRC_UPDATED;
        return rc;
    }

    idx = pkgdir->mod_data;

    if (idx->_vf->vf_flags & VF_FETCHED) {
        pkgdir_free(pkgdir);
        *uprc = PKGDIR_UPRC_UPDATED;
        return 1;
    }

    switch (is_uptodate(idx->idxpath, idx->dg, NULL, idx->srcnam)) {
        case 1:
            rc = 1;
            *uprc = PKGDIR_UPRC_UPTODATE;
            break;

        case -1:
        case 0:
            rc = update_whole_idx(src);
            if (rc)
                *uprc = PKGDIR_UPRC_UPTODATE;
            break;

        default:
            n_assert(0);
    }

    pkgdir_free(pkgdir);
    if (!rc)
        *uprc = PKGDIR_UPRC_ERR_UNKNOWN;
    return rc;
}

static int parse_toc_line(char *line, time_t *tsp, char **mdp)
{
    char *p, *md;
    unsigned long ts;

    p = line;

    *tsp = 0;
    *mdp = NULL;

    while (*p && isspace(*p))
        p++;

    if (*p == '#')
        return 1;


    if ((p = strchr(p, ' ')) == NULL)
        return 0;

    while (*p && isspace(*p))
        *p++ = '\0';

    if (sscanf(p, "%lu", &ts) != 1) /* read ts */
        return 0;

    if ((p = strchr(p, ' ')) == NULL)
        return 0;

    while (*p && isspace(*p))
        *p++ = '\0';

    md = p;                 /* read orig md */
    if ((p = strchr(p, ' ')) == NULL)
        return 0;

    *p = '\0';

    if (p - md != TNIDX_DIGEST_SIZE)
        return 0;

    *tsp = ts;
    *mdp = md;

    return 1;
}


int pndir_m_update(struct pkgdir *pkgdir, enum pkgdir_uprc *uprc)
{
    char                idxpath[PATH_MAX], tmpath[PATH_MAX], path[PATH_MAX];
    struct vfile        *vf;
    struct pndir_digest dg_remote;
    struct pndir        *idx;
    char                line[1024], *dn, *bn;
    int                 nread, nerr = 0, rc, npatch, first_patch_found;
    const char          *errmsg_broken_difftoc = _("%s: broken patch list");
    char                current_md[TNIDX_DIGEST_SIZE + 1];

    idx = pkgdir->mod_data;
    if (idx->_vf->vf_flags & VF_FETCHED)
        return 1;

    switch (is_uptodate(pkgdir->idxpath, idx->dg, &dg_remote, idx->srcnam)) {
        case 1:
            *uprc = PKGDIR_UPRC_UPTODATE;
            rc = 1;
            //if ((pkgdir->flags & PKGDIR_VERIFIED) == 0)
            //    rc = pndir_digest_verify(idx->dg, idx->vf);
            return rc;
            break;

        case -1:
            *uprc = PKGDIR_UPRC_ERR_UNKNOWN;
            return 0;

        case 0:                 /* diff updateable? */
            if (dg_remote.flags & PNDIGEST_BRANDNEW) { /* nope */
                *uprc = PKGDIR_UPRC_ERR_DESYNCHRONIZED;
                return 0;
            }
            break;

        default:
            n_assert(0);
    }

    *uprc = PKGDIR_UPRC_ERR_UNKNOWN;
    /* open diff toc */

    snprintf(idxpath, sizeof(idxpath), "%s", pkgdir->idxpath);
    eat_zlib_ext(idxpath);
    snprintf(tmpath, sizeof(tmpath), "%s", idxpath);
    n_basedirnam(tmpath, &dn, &bn);

    int none_compr = n_str_eq(dg_remote.compr, COMPR_NONE);

    snprintf(path, sizeof(path), "%s/%s/%s%s%s%s", dn,
             pndir_packages_incdir, bn, pndir_difftoc_suffix,
             none_compr ? "" : ".",
             none_compr ? "" : dg_remote.compr);

    vf = vfile_open_ul(path, VFT_TRURLIO, VFM_RO, pkgdir->name);
    if (vf == NULL)
        return 0;

    n_assert(strlen(idx->dg->md) == TNIDX_DIGEST_SIZE);
    memcpy(current_md, idx->dg->md, TNIDX_DIGEST_SIZE + 1);

    off_t mdsize = 0, mdpatchsize = 0;
    struct vf_stat stats;

    char *idx_path, *idx_ext;
    n_strdupap(pkgdir->idxpath, &idx_path);

    idx_ext = strstr(idx_path, ".ndir.");
    if (idx_ext != NULL) {
        *idx_ext = '\0';
        idx_ext += 5;           /* extension with dot */
    } else {
        idx_ext = "";
    }

    // to keep quiet vf_stat
    vfile_configure(VFILE_CONF_VERBOSE, 0);

    snprintf(path, sizeof(path), "%s.ndir%s", idx_path, idx_ext);
    if (vf_stat(path, tmpath, &stats, pkgdir->name)) {
        mdsize += stats.vf_size;
        msgn(3, "_\n%lld bytes %s\n", (long long)stats.vf_size, path);
    }

    snprintf(path, sizeof(path), "%s.ndir.dscr%s", idx_path, idx_ext);
    if (vf_stat(path, tmpath, &stats, pkgdir->name)) {
        mdsize += stats.vf_size;
        msgn(3, "_\n%lld bytes %s\n", (long long)stats.vf_size, path);
    }

    snprintf(path, sizeof(path), "%s.ndir.dscr.i18n%s", idx_path, idx_ext);
    if (vf_stat(path, tmpath, &stats, pkgdir->name)) {
        mdsize += stats.vf_size;
        msgn(3, "_\n%lld bytes %s\n", (long long)stats.vf_size, path);
    }

    msgn(2, "pndir_m_update idxsize: %lld\n", (long long)mdsize);

    while ((nread = n_stream_gets(vf->vf_tnstream, line, sizeof(line))) > 0) {
        char *md, *pdate;
        time_t ts;

        if (!parse_toc_line(line, &ts, &md))
            break;

        if (md == NULL) /* i.e comment */
            continue;

        if (ts <= pkgdir->ts)   /* skip diffs created before me */
            continue;

        if (!first_patch_found) { /* skip first patch - it is already applied */
            if (memcmp(md, current_md, TNIDX_DIGEST_SIZE) == 0)
                first_patch_found = 1;
	    else
		continue;
        }

        pdate = strstr(line, ".ndir.");
	*pdate = '\0';
	pdate += 6;

        snprintf(path, sizeof(path), "%s/%s/%s.ndir.%s",
		 dn, pndir_packages_incdir, line, pdate);
	if (vf_stat(path, tmpath, &stats, pkgdir->name)) {
	  mdpatchsize += stats.vf_size;
	  msgn(3, "_\n%lld bytes %s\n", (long long)stats.vf_size, path);
	}
        snprintf(path, sizeof(path), "%s/%s/%s.ndir.dscr.%s",
		 dn, pndir_packages_incdir, line, pdate);
	if (vf_stat(path, tmpath, &stats, pkgdir->name)) {
	  mdpatchsize += stats.vf_size;
	  msgn(3, "_\n%lld bytes %s\n", (long long)stats.vf_size, path);
	}
        snprintf(path, sizeof(path), "%s/%s/%s.ndir.dscr.i18n.%s",
		 dn, pndir_packages_incdir, line, pdate);
	if (vf_stat(path, tmpath, &stats, pkgdir->name)) {
	  mdpatchsize += stats.vf_size;
	  msgn(3, "_\n%lld bytes %s\n", (long long)stats.vf_size, path);
	}

	msgn(2, "pndir_m_update idxpatches/idxsize: %lld/%lld bytes\n",
             (long long)mdpatchsize, (long long)mdsize);

	if (mdpatchsize * 9 / 10 > mdsize) {
	    vfile_close(vf);
	    msgn(1, _("Index patches size too big\n"));
	    msgn(1, _("Retrieving whole index ...\n"));
	    rc = update_whole_idx(pkgdir->src);
	    if (rc)
	      *uprc = PKGDIR_UPRC_UPTODATE;
	    return rc;
	}
    }

    vfile_configure(VFILE_CONF_VERBOSE, &poldek_VERBOSE);
    n_stream_seek(vf->vf_tnstream, 0L, SEEK_SET); // to the begining

    first_patch_found = 0;
    npatch = 0;
    while ((nread = n_stream_gets(vf->vf_tnstream, line, sizeof(line))) > 0) {
        struct pkgdir *diff;
        char *md;
        time_t ts;

        if (!parse_toc_line(line, &ts, &md)) {
            logn(LOGERR, errmsg_broken_difftoc, path);
            nerr++;
            break;
        }

        if (md == NULL) /* i.e comment */
            continue;

        if (ts <= pkgdir->ts)   /* skip diffs created before me */
            continue;

        if (!first_patch_found) { /* skip first patch - it is already applied */
            if (memcmp(md, current_md, TNIDX_DIGEST_SIZE) == 0)
                first_patch_found = 1;
            else {
                msgn(2, _("Check diff (ts = %ld, %ld) %s (searching %s)\n"),
                     (long)pkgdir->ts, (long)ts, md, current_md);

                if (poldek_verbose() > 3) {
                    msgn(4, "ts = %ld, %ld", (long)pkgdir->ts, (long)ts);
                    msgn(4, "md dir  %s", idx->dg->md);
                    msgn(4, "md last %s", md);
                    msgn(4, "md curr %s", current_md);
                    logn(LOGERR, _("%s: no patches available(fake)"),
                         pkgdir_pr_idxpath(pkgdir));
                }
                continue;
            }
        }

        // empty line between Applying..
        //msg(1, "_\n");
        snprintf(path, sizeof(path), "%s/%s/%s", dn, pndir_packages_incdir, line);
        diff = pkgdir_open_ext(path, NULL, pkgdir->type, "diff", NULL,
                               PKGDIR_OPEN_DIFF, pkgdir->lc_lang);
        if (diff == NULL) {
            nerr++;
            break;
        }

        if ((pkgdir->flags & PKGDIR_LOADED) == 0) {
            if (!pkgdir_load(pkgdir, NULL, 0)) {
                logn(LOGERR, _("%s: load failed"), pkgdir->idxpath);
                nerr++;
                break;
            }
        }
        msgn(1, _("Applying %s..."), n_basenam(diff->idxpath));
        pkgdir_load(diff, NULL, 0);
        pkgdir_patch(pkgdir, diff);
        pkgdir_free(diff);

        npatch++;
    }

    vfile_close(vf);
    if (npatch == 0) {        /* outdated and no patches */
        *uprc = PKGDIR_UPRC_ERR_DESYNCHRONIZED;
        nerr++;
    }

    if (nerr == 0)
        if (pkgdir__uniq(pkgdir) > 0) { /* duplicates? -> error */
            *uprc = PKGDIR_UPRC_ERR_UNKNOWN;
            nerr++;
        }


    if (nerr == 0) {
        struct pndir_digest dg;

        *uprc = PKGDIR_UPRC_UPDATED;
        pndir_digest_calc_pkgs(&dg, pkgdir->pkgs);
        DBGF("md.remote  %s\nmd.local  %s\n", dg_remote.md, dg.md);
        if (memcmp(dg.md, dg_remote.md, sizeof(dg.md)) != 0) {
            logn(LOGWARN, _("%s: desynchronized index, try --upa"),
                 pkgdir_pr_idxpath(pkgdir));
            *uprc = PKGDIR_UPRC_ERR_DESYNCHRONIZED;
            nerr++;
        }
    }


    if (nerr == 0) {
        snprintf(path, sizeof(path), "%s/%s", dn, pndir_packages_incdir);
        if (vf_localdirpath(tmpath, sizeof(tmpath), path) < (int)sizeof(tmpath)) {
            int v = poldek_set_verbose(-1);
            pkgdir__rmf(tmpath, NULL, 0);
            poldek_set_verbose(v);
        }
        msg(1, "_\n");
    }

    return nerr == 0;
}
