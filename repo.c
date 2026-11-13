/*
  Copyright (C) 2026 PLD Linux Team <feedback@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <errno.h>
#include <limits.h>             /* for NAME_MAX */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>

#include <trurl/nhash.h>
#include <trurl/narray.h>
#include <trurl/nstr.h>
#include <trurl/n_snprintf.h>
#include <trurl/nassert.h>
#include <trurl/nmalloc.h>
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
#include "repo_intern.h"
#include "pkgdir/source.h"

static int reposd_filename_from_url(char *buf, size_t size, const char *url)
{
    char cleaned[PATH_MAX];
    const char *u;
    char *p, *q;
    int n;

    *buf = '\0';

    /* hidepasswd() fills cleaned only when the URL carries a password,
       otherwise u is the URL itself; cleaned must never be copied onto
       itself, the copy would be undefined and truncate the name */
    u = vf_url_hidepasswd(cleaned, sizeof(cleaned), url);
    if (u != cleaned)
        n_snprintf(cleaned, sizeof(cleaned), "%s", url);

    /* drop login:password@, so the name does not depend on credentials */
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

    /* turns every character outside [alnum-+] into '.', so no '/' and no
       ".." survive: the name cannot leave repos.d */
    n = vf_url_as_dirpath(buf, size, cleaned);
    if (n <= 0)
        return 0;

    /* a leading dot would make the file hidden from ls and from *.conf
       globs, although our own readdir() would still load it */
    for (p = buf; *p == '.'; p++)
        ;
    if (p != buf) {
        n -= p - buf;
        memmove(buf, p, n + 1);
    }

    while (n > 0 && buf[n - 1] == '.')
        buf[--n] = '\0';

    if (n >= 5 && strcasecmp(buf + n - 5, ".repo") == 0)
        memcpy(buf + n - 5, ".conf", 5);

    if (n > NAME_MAX) {
        logn(LOGERR, _("%s: URL-derived filename too long"), url);
        return 0;
    }

    return n;
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

        /* a downloaded file must not decide that it is loaded: the source
           is only used when asked for by name (-s/-n) */
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
    tn_array *sources_ht = poldek_conf_get_sections(htconf, "source");
    if (!sources_ht) {
        logn(LOGERR, _("%s: downloaded configuration is invalid"), url);
        n_hash_free(htconf);
        return NULL;
    }

    for (int i = 0; i < n_array_size(sources_ht); i++) {
        tn_hash *ht = n_array_nth(sources_ht, i);
        n_hash_remove(ht, "ignore");
        n_hash_remove(ht, "hold");
        n_hash_remove(ht, "auto");
        n_hash_remove(ht, "noauto");
    }

    ht_global = poldek_conf_get_section(htconf, "global");
    if (ht_global == NULL) {
        ht_global = poldek_conf_add_section(htconf, "global");
    }

    if (ht_global == NULL) {
        logn(LOGERR, _("%s: cannot create [global] section"), url);
        n_hash_free(htconf);
        return NULL;
    }

    n_snprintf(url_dir, sizeof(url_dir), "%s", url);
    const char *base = n_dirname(url_dir);
    poldek_conf_set(ht_global, "_baseurl", base ? base : ".");
    poldek_conf_set(ht_global, "_origin", url);

    return htconf;
}

static tn_hash *load_dnf_repo_conf(const char *url)
{
    struct vfile *vf;
    tn_hash *htconf;

    vf = vfile_open(url, VFT_TRURLIO, VFM_RO | VFM_NOEMPTY);
    if (vf == NULL) {
        logn(LOGERR, _("%s: cannot open"), url);
        return NULL;
    }

    htconf = repo_dnf_to_conf(url, vfile_localpath(vf));
    vfile_close(vf);

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

            /* a source created from a global `source = ...` line has no
               __file: its name is taken all the same, or the configuration
               would not load */
            if (ex_file == NULL) {
                logn(LOGERR, _("%s: source name '%s' already defined in %s"),
                     dest_path, new_name, "[global]");
                return 0;
            }

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

    if (repo_is_dnf(url))
        htconf = load_dnf_repo_conf(url);
    else
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

/* a repository file found in repos.d and the _origin saved in it */
struct repo_match {
    char    *origin;            /* NULL when the file has no _origin */
    char    *path;
};

static struct repo_match *repo_match_new(const char *origin, const char *path)
{
    struct repo_match *m = n_malloc(sizeof(*m));

    m->origin = origin ? n_strdup(origin) : NULL;
    m->path = n_strdup(path);
    return m;
}

static void repo_match_free(void *ptr)
{
    struct repo_match *m = ptr;

    n_free(m->origin);
    n_free(m->path);
    n_free(m);
}

/* RET: the repositories the origin names: one whose _origin (or file name)
   is origin, else those whose _origin contains it */
static tn_array *find_repos_by_origin(const char *reposd, const char *origin)
{
    tn_array *matches = n_array_new(2, repo_match_free, NULL);
    tn_array *files = poldek_util_readdir(reposd, "*.conf");

    if (files == NULL)
        return matches;

    for (int i = 0; i < n_array_size(files); i++) {
        const char *path = n_array_nth(files, i);
        tn_hash *htconf;
        tn_hash *ht_global;
        const char *o;

        htconf = poldek_conf_load(path, POLDEK_LDCONF_NOVALIDATE |
                                  POLDEK_LDCONF_NOINCLUDE);
        if (htconf == NULL)
            continue;

        ht_global = poldek_conf_get_section(htconf, "global");
        o = ht_global ? poldek_conf_get(ht_global, "_origin", NULL) : NULL;

        if ((o && strcmp(o, origin) == 0) || strcmp(n_basenam(path), origin) == 0) {
            n_array_clean(matches);
            n_array_push(matches, repo_match_new(o, path));
            n_hash_free(htconf);
            break;

        } else if (o && strstr(o, origin)) {
            n_array_push(matches, repo_match_new(o, path));
        }

        n_hash_free(htconf);
    }

    n_array_free(files);

    return matches;
}

/* a URL is shown as typed/saved, but without the password */
static const char *url_for_display(char *buf, size_t size, const char *url)
{
    return vf_url_hidepasswd(buf, size, url);
}

int poldek_repo_del(struct poldek_ctx *ctx, const char *url)
{
    char reposd[PATH_MAX];
    struct repo_match *m;
    tn_array *matches;
    int i;

    (void) ctx;

    if (poldek_conf_reposd(reposd, sizeof(reposd)) == NULL) {
        logn(LOGERR, _("cannot determine repos.d directory"));
        return 0;
    }

    matches = find_repos_by_origin(reposd, url);

    if (n_array_size(matches) == 0) {
        char shown[PATH_MAX];

        logn(LOGERR, _("%s: no such repo"),
             url_for_display(shown, sizeof(shown), url));
        n_array_free(matches);
        return 0;
    }

    if (n_array_size(matches) > 1) {
        char shown[PATH_MAX];

        logn(LOGERR, _("%s: ambiguous name, matches %d repositories:"),
             url_for_display(shown, sizeof(shown), url), n_array_size(matches));

        for (i = 0; i < n_array_size(matches); i++) {
            m = n_array_nth(matches, i);
            msgn(0, "  %s", url_for_display(shown, sizeof(shown),
                                            m->origin ? m->origin : m->path));
        }

        n_array_free(matches);
        return 0;
    }

    m = n_array_nth(matches, 0);

    if (access(m->path, F_OK) != 0) {
        logn(LOGERR, _("%s: no such file"), m->path);
        goto l_err;
    }

    if (unlink(m->path) != 0) {
        logn(LOGERR, _("%s: %m"), m->path);
        goto l_err;
    }

    char shown[PATH_MAX];

    logn(LOGNOTICE, _("%s repo has been removed"),
         url_for_display(shown, sizeof(shown), m->origin ? m->origin : m->path));

    n_array_free(matches);
    return 1;

 l_err:
    n_array_free(matches);
    return 0;
}
