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
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include <trurl/nhash.h>
#include <trurl/narray.h>
#include <trurl/nstr.h>
#include <trurl/n_snprintf.h>
#include <trurl/nassert.h>
#include <trurl/nmalloc.h>
#include <trurl/nstream.h>
#include <vfile/vfile.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "conf.h"
#include "conf_intern.h"
#include "misc.h"
#include "poldek.h"
#include "poldek_util.h"
#include "repo.h"
#include "pkgdir/source.h"

static int reposd_filename_from_url(char *buf, size_t size, const char *url)
{
    char cleaned[PATH_MAX];
    const char *u;
    char *p, *q;
    int n;

    *buf = '\0';

    u = vf_url_hidepasswd(cleaned, sizeof(cleaned), url);
    n_snprintf(cleaned, sizeof(cleaned), "%s", u);

    p = strstr(cleaned, "://");
    if (p) {
        q = strchr(p + 3, '@');
        char *slash = strchr(p + 3, '/');
        if (q && (slash == NULL || q < slash))
            memmove(p + 3, q + 1, strlen(q + 1) + 1);
    }

    p = strchr(cleaned, '?');
    if (p)
        *p = '\0';

    n = vf_url_as_dirpath(buf, size, cleaned);
    if (n <= 0)
        return 0;

    for (p = buf; *p; p++)
        if (*p == '/')
            *p = '.';

    while (n > 0 && buf[n - 1] == '.')
        buf[--n] = '\0';

    //n += n_snprintf(&buf[n], size - n, ".conf");
    if (n >= (int) size || n > NAME_MAX) {
        logn(LOGERR, _("%s: URL-derived filename too long"), url);
        return 0;
    }

    return n;
}

static int is_managed_reposd_file(const char *path)
{
    FILE *f;
    char buf[1024];
    static const char prefix[] = "# remote repository managed by poldek";
    size_t len;
    int rc = 0;

    if ((f = fopen(path, "r")) == NULL)
        return 0;

    if (fgets(buf, sizeof(buf), f)) {
        len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n')
            buf[len - 1] = '\0';
        if (strncmp(buf, prefix, sizeof(prefix) - 1) == 0)
            rc = 1;
    }

    fclose(f);
    return rc;
}

static int validate_managed_sources(const char *url, tn_hash *htconf)
{
    tn_array *sources = poldek_conf_get_sections(htconf, "source");
    int i, nerr = 0;

    if (sources == NULL || n_array_size(sources) == 0) {
        logn(LOGERR, _("%s: no [source] section found"), url);
        return 1;
    }

    for (i = 0; i < n_array_size(sources); i++) {
        tn_hash *ht = n_array_nth(sources, i);
        const char *name = poldek_conf_get(ht, "name", NULL);
        const char *path = poldek_conf_get(ht, "path", NULL);

        if (name == NULL || *name == '\0') {
            logn(LOGERR, _("%s: [source] missing explicit name"), url);
            nerr++;
            continue;
        }

        if (path == NULL || *path == '\0') {
            logn(LOGERR, _("%s: [source] missing path"), url);
            nerr++;
            continue;
        }

        char expanded[PATH_MAX];
        const char *epath = conf__expand_value(expanded, sizeof(expanded),
                                               htconf, ht, path);

        if (epath == NULL || *epath == '\0') {
            logn(LOGERR, _("%s: [source] missing path"), url);
            nerr++;
            continue;
        } else if (*epath != '/' && strstr(epath, "://") == NULL) {
            logn(LOGERR, _("%s: [source] path must be absolute or remote URL"),
                 url);
            nerr++;
            continue;
        }

        if (strcmp(epath, path) != 0) {
            poldek_conf_set(ht, "__raw_path", path);
            poldek_conf_set(ht, "path", epath);
        }

        struct source *src = source_new_htcnf(ht);
        if (src == NULL) {
            logn(LOGERR, _("%s: invalid [source] configuration"), url);
            nerr++;
        } else {
            source_free(src);
        }

        const char *raw_path = poldek_conf_get(ht, "__raw_path", NULL);
        if (raw_path) {
            poldek_conf_set(ht, "path", raw_path);
        }
        /* force noauto */
        poldek_conf_set(ht, "auto", "no");
    }

    return nerr;
}

static tn_hash *load_repo_conf(const char *url)
{
    struct vfile *vf = NULL;
    tn_hash *htconf = NULL;
    tn_hash *ht_global;
    char url_dir[PATH_MAX];

    vf = vfile_open(url, VFT_TRURLIO, VFM_RO | VFM_NOEMPTY);
    if (vf == NULL) {
        logn(LOGERR, _("%s: cannot open"), url);
        return NULL;
    }

    htconf = poldek_conf_load(vfile_localpath(vf), POLDEK_LDCONF_NOINCLUDE |
                                                   POLDEK_LDCONF_STRICT);
    vfile_close(vf);
    vf = NULL;

    if (htconf == NULL) {
        logn(LOGERR, _("%s: downloaded configuration is invalid"), url);
        return NULL;
    }

    ht_global = poldek_conf_get_section(htconf, "global");
    if (ht_global == NULL)
        ht_global = poldek_conf_add_section(htconf, "global");
    if (ht_global == NULL) {
        logn(LOGERR, _("%s: cannot create [global] section"), url);
        n_hash_free(htconf);
        return NULL;
    }

    n_snprintf(url_dir, sizeof(url_dir), "%s", url);
    const char *base = n_dirname(url_dir);
    poldek_conf_set(ht_global, "_baseurl", base);
    poldek_conf_set(ht_global, "_origin", url);

    return htconf;
}

static int check_source_name_collision(struct poldek_ctx *ctx,
                                       tn_array *new_sources,
                                       const char *dest_path)
{
    tn_hash *cnf;
    tn_array *existing;
    int i, j;
    char new_file[PATH_MAX];

    if (ctx == NULL || new_sources == NULL)
        return 1;

    cnf = poldek_get_config(ctx);
    if (cnf == NULL)
        return 1;

    existing = poldek_conf_get_sections(cnf, "source");
    if (existing == NULL)
        return 1;

    n_snprintf(new_file, sizeof(new_file), "%s", dest_path);

    for (i = 0; i < n_array_size(new_sources); i++) {
        tn_hash *ht_new = n_array_nth(new_sources, i);
        const char *new_name = poldek_conf_get(ht_new, "name", NULL);

        if (new_name == NULL)
            continue;

        for (j = 0; j < n_array_size(existing); j++) {
            tn_hash *ht_ex = n_array_nth(existing, j);
            const char *ex_name = poldek_conf_get(ht_ex, "name", NULL);
            const char *ex_file = poldek_conf_get(ht_ex, "__file", NULL);

            if (ex_name == NULL || strcmp(ex_name, new_name) != 0)
                continue;

            if (ex_file == NULL)
                continue;

            if (strcmp(ex_file, new_file) == 0)
                continue; /* same physical file, allowed */

            logn(LOGERR, _("%s: source name '%s' already defined in %s"),
                 dest_path, new_name, ex_file);
            return 0;
        }
    }

    return 1;
}

int poldek_repo_add(struct poldek_ctx *ctx, const char *url, unsigned flags)
{
    char reposd[PATH_MAX], dest[PATH_MAX];
    char filename[PATH_MAX];
    const char header[] = "# remote repository managed by poldek\n";
    tn_hash *htconf = NULL;
    tn_array *new_sources;
    int rc = 0;

    if (url == NULL || *url == '\0') {
        logn(LOGERR, _("addrepo: empty URL"));
        return 0;
    }

    if (poldek_conf_reposd(reposd, sizeof(reposd)) == NULL) {
        logn(LOGERR, _("addrepo: cannot determine repos.d directory"));
        return 0;
    }

    if (!util__mkdir_p("", reposd)) {
        logn(LOGERR, _("%s: cannot create directory"), reposd);
        return 0;
    }

    if (!reposd_filename_from_url(filename, sizeof(filename), url)) {
        logn(LOGERR, _("%s: cannot derive filename from URL"), url);
        return 0;
    }

    n_snprintf(dest, sizeof(dest), "%s/%s", reposd, filename);

    if ((flags & POLDEK_REPO_FORCE) == 0 && access(dest, F_OK) == 0) {
        logn(LOGERR, _("%s: already exists (use --force to overwrite)"), dest);
        return 0;
    }

    htconf = load_repo_conf(url);
    if (htconf == NULL)
        goto l_end;

    if (validate_managed_sources(url, htconf) != 0)
        goto l_end;

    new_sources = poldek_conf_get_sections(htconf, "source");
    if (!check_source_name_collision(ctx, new_sources, dest))
        goto l_end;

    if (!conf__save(htconf, dest, header))
        goto l_end;

    logn(LOGNOTICE, _("%s has been added"), url);
    rc = 1;

l_end:
    if (htconf)
        n_hash_free(htconf);

    return rc;
}

int poldek_repo_del(struct poldek_ctx *ctx, const char *filename,
                    unsigned flags)
{
    char reposd[PATH_MAX], path[PATH_MAX];
    size_t len;
    (void) ctx;

    if (filename == NULL || *filename == '\0') {
        logn(LOGERR, _("rmrepo: empty filename"));
        return 0;
    }

    if (strchr(filename, '/') != NULL) {
        logn(LOGERR, _("rmrepo: %s: filename must not contain '/'"), filename);
        return 0;
    }

    len = strlen(filename);
    if (len < 5 || strcmp(filename + len - 5, ".conf") != 0) {
        logn(LOGERR, _("rmrepo: %s: not a .conf filename"), filename);
        return 0;
    }

    if (poldek_conf_reposd(reposd, sizeof(reposd)) == NULL) {
        logn(LOGERR, _("rmrepo: cannot determine repos.d directory"));
        return 0;
    }

    n_snprintf(path, sizeof(path), "%s/%s", reposd, filename);

    if (access(path, F_OK) != 0) {
        logn(LOGERR, _("%s: no such file"), path);
        return 0;
    }

    if ((flags & POLDEK_REPO_FORCE) == 0 && !is_managed_reposd_file(path)) {
        logn(LOGERR, _("%s: not a managed repository file (use --force to remove it anyway)"),
             path);
        return 0;
    }

    if (unlink(path) != 0) {
        logn(LOGERR, _("%s: %m"), path);
        return 0;
    }

    logn(LOGNOTICE, _("%s: removed"), path);
    return 1;
}
