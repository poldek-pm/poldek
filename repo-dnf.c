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

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <trurl/narray.h>
#include <trurl/nbuf.h>
#include <trurl/nhash.h>
#include <trurl/nmalloc.h>
#include <trurl/nstr.h>
#include <trurl/n_snprintf.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "conf.h"
#include "conf_intern.h"
#include "poldek_util.h"
#include "repo_intern.h"
#include "pm/rpmorg/arch.h"

/* defined in pkgdir/metadata/basearch.c */
const char *metadata__get_base_arch(void);

/* a value of a list option, split as dnf does: on blanks and commas,
   a backslash escaping the next character */
static tn_array *split_list(const char *value)
{
    return n_str_etokl_ext(value, " \t\n,", "", "", '\\');
}

/* /etc/os-release VERSION_ID, as dnf takes $releasever from the rpmdb */
static char *detect_releasever(void)
{
    static const char *paths[] = { "/etc/os-release", "/usr/lib/os-release", NULL };
    char *re = NULL;
    int i;

    for (i = 0; paths[i]; i++) {
        FILE *f = fopen(paths[i], "r");
        char *line = NULL;
        size_t size = 0;

        if (f == NULL)
            continue;

        while (getline(&line, &size, f) != -1) {
            const char *v;
            size_t n;

            if (strncmp(line, "VERSION_ID=", 11) != 0)
                continue;

            v = line + 11;
            n = strcspn(v, "\r\n");

            if (n >= 2 && (v[0] == '"' || v[0] == '\'') && v[n - 1] == v[0]) {
                v++;
                n -= 2;
            }

            if (n > 0)
                re = n_strdupl(v, n);
            break;
        }

        free(line);
        fclose(f);

        if (re)
            break;
    }

    return re;
}

static void macros_add(tn_hash *macros, const char *name, const char *value)
{
    if (value && *value)
        poldek_conf_set(macros, name, value);
}

/* dnf's variables as conf.c macros: _arch, _basearch, _releasever... */
static tn_hash *macros_new(void)
{
    tn_hash *htconf = conf__new_htconf("global");

    if (htconf == NULL)
        return NULL;

    /* conf__expand_value() looks macros up with poldek_conf_get() */
    tn_hash *macros = poldek_conf_get_section(htconf, "global");

    char *arch = pm_rpm_arch_standalone();
    if (arch) {
        macros_add(macros, "_arch", arch);
        free(arch);
    }

    const char *basearch = metadata__get_base_arch();
    macros_add(macros, "_basearch", basearch);
    macros_add(macros, "_baseos", "linux");

    char *release = detect_releasever();
    if (release) {
        macros_add(macros, "_releasever", release);
        char *dot = strchr(release, '.');
        if (dot) {
            *dot = '\0';
            macros_add(macros, "_releasever_minor", dot + 1);
        }
        macros_add(macros, "_releasever_major", release);
        free(release);
    }

    return htconf;
}

/* the value of dnf's $name, i.e. the _name macro */
static const char *macro_get(tn_hash *macros, const char *name, size_t len)
{
    char key[256];

    if (len + 2 > sizeof(key))
        return NULL;

    key[0] = '_';
    memcpy(key + 1, name, len);
    key[len + 1] = '\0';

    return poldek_conf_get(macros, key, NULL);
}

#define EXPAND_MAX_DEPTH 8

static char *expand_vars_depth(const char *src, tn_hash *macros, int depth);

/* dnf's ${var:-word} and ${var:+word}: word is expanded on its own */
static void expand_add_word(tn_buf *out, const char *word, size_t len,
                            tn_hash *macros, int depth)
{
    char *tmp = n_strdupl(word, len);
    char *expanded = expand_vars_depth(tmp, macros, depth + 1);

    n_buf_puts(out, expanded);
    free(expanded);
    free(tmp);
}

/* rewrite dnf's $arch, $basearch, $releasever into poldek macros
   (%{_arch}, ...) and resolve ${var:-word}/${var:+word}, which conf.c
   cannot; unknown variables become %{_name} too, so that conf.c does
   not take them from the environment (source.path is CONF_TYPE_F_ENV) */
static char *expand_vars_depth(const char *src, tn_hash *macros, int depth)
{
    tn_buf *out;
    const char *p = src;
    char *re;

    if (depth > EXPAND_MAX_DEPTH)
        return n_strdup(src);

    out = n_buf_new(strlen(src) + 1);

    while (*p) {
        const char *name, *name_end, *end, *colon;

        /* dnf's substitution unescapes only '\$' and '\\'; any other
           backslash is left for the option's list parser */
        if (*p == '\\' && (p[1] == '$' || p[1] == '\\')) {
            n_buf_putc(out, p[1]);
            p += 2;
            continue;
        }

        if (*p != '$') {
            n_buf_putc(out, *p);
            p++;
            continue;
        }

        if (p[1] == '{') {
            const char *close = strchr(p + 2, '}');

            if (close == NULL) {
                n_buf_putc(out, *p);
                p++;
                continue;
            }
            name = p + 2;
            name_end = close;
            end = close + 1;
            colon = memchr(name, ':', close - name);

        } else {
            name = p + 1;
            end = name;
            while (isalnum((unsigned char)*end) || *end == '_')
                end++;
            name_end = end;
            colon = NULL;
        }

        if (name_end == name) {
            n_buf_putc(out, *p);
            p++;
            continue;
        }

        if (colon && colon + 1 < end &&
            (colon[1] == '-' || colon[1] == '+')) {

            const char *word = colon + 2;
            int is_set = macro_get(macros, name, colon - name) != NULL;

            if (colon[1] == '+' ? is_set : !is_set)
                expand_add_word(out, word, end - 1 - word, macros, depth);
            else if (colon[1] == '-')
                n_buf_printf(out, "%%{_%.*s}", (int)(colon - name), name);

            p = end;
            continue;
        }

        n_buf_printf(out, "%%{_%.*s}", (int)((colon ? colon : name_end) - name),
                     name);
        if (colon)       /* an unknown expansion mode stays literal */
            n_buf_printf(out, ":%.*s", (int)(end - 1 - (colon + 1)), colon + 1);
        p = end;
    }

    n_buf_write_z(out, "", 0);  /* n_buf_ptr() must be NUL-terminated */
    re = n_strdup(n_buf_ptr(out));
    n_buf_free(out);
    return re;
}

static char *expand_vars(const char *src, tn_hash *macros)
{
    char expanded[PATH_MAX];

    char *rewritten = expand_vars_depth(src, macros, 0);
    const char *value = conf__expand_value(expanded, sizeof(expanded),
                                           NULL, macros, rewritten);
    if (value != rewritten) {
        free(rewritten);
        rewritten = n_strdup(value);
    }

    return rewritten;
}

/* the section name of "[id]"; NULL if the header is malformed or the id
   has characters dnf does not allow (letters, digits, '-', '_', '.' and
   ':'), so the first ']' ends the name and the rest of the line is left
   alone */
static char *parse_section(const char *line, const char *url, int nline)
{
    const char *p = line + 1;
    const char *end = strchr(p, ']');

    if (end == NULL) {
        logn(LOGERR, _("%s:%d: missing ']'"), url, nline);
        return NULL;
    }

    if (end == p) {
        logn(LOGERR, _("%s:%d: empty section name"), url, nline);
        return NULL;
    }

    for (const char *q = p; q < end; q++) {
        if (!isalnum((unsigned char)*q) && strchr("-_.:", *q) == NULL) {
            logn(LOGERR, _("%s:%d: invalid repository id '%.*s'"),
                 url, nline, (int)(end - p), p);
            return NULL;
        }
    }

    return n_strdupl(p, end - p);
}

static void pending_flush(char **keyp, tn_buf **valp, tn_hash *opts)
{
    char *value;
    size_t len;

    if (*keyp == NULL)
        return;

    n_buf_write_z(*valp, "", 0);        /* n_buf_ptr() must be NUL-terminated */
    value = n_buf_ptr(*valp);
    len = strlen(value);

    if (len > 1 && (value[0] == '"' || value[0] == '\'') &&
        value[len - 1] == value[0]) {

        value[len - 1] = '\0';
        memmove(value, value + 1, len - 1);
    }

    /* "key =" and 'key = ""' mean the option is not set: nothing is
       stored for them, so n_hash_get() below returns either NULL or a
       non-empty value */
    if (opts && *value)
        n_hash_replace(opts, *keyp, n_strdup(value));

    n_free(*keyp);
    *keyp = NULL;
    n_buf_free(*valp);
    *valp = NULL;
}

/* Parse a dnf repository file: section id -> (key -> value).
 *
 * This is not poldek_conf_load(..., POLDEK_LDCONF_FOREIGN): conf.c
 * accepts only alnum/'-' in section names, so ids like "my_repo" or
 * "copr:copr.fedorainfracloud.org:..." are truncated; it has no
 * indented continuations; it strips an inline '#' from a value; and
 * for a multi-valued section it appends a repeated section instead of
 * merging it, while dnf merges a repeated [id] and lets the last value
 * win. dnf's enabled= and per-repo semantics need the mapping below
 * anyway, so only conf.c's macro expander (conf__expand_value()) is
 * reused. */
static tn_hash *parse_file(const char *url, const char *path)
{
    FILE *f = fopen(path, "r");
    tn_hash *sections;
    tn_hash *cur_opts = NULL;
    char *cur_key = NULL;
    tn_buf *cur_val = NULL;
    char *line = NULL;
    size_t linelen = 0;
    int nline = 0;

    if (f == NULL) {
        logn(LOGERR, _("%s: %m"), url);
        return NULL;
    }

    sections = n_hash_new(8, (tn_fn_free)n_hash_free);

    while (getline(&line, &linelen, f) != -1) {
        char *begin, *p, *key, *value;

        nline++;

        begin = line;
        if (nline == 1 && strncmp(line, "\xEF\xBB\xBF", 3) == 0)
            begin += 3;

        /* skip the indentation of a continuation; the whole line is
           trimmed, so "p != begin" below spots one and the pieces taken
           out of the line are already free of trailing whitespace */
        p = n_str_strip_ws(begin);

        if (*p == '\0' || *p == '#' || *p == ';') {
            pending_flush(&cur_key, &cur_val, cur_opts);
            continue;
        }

        if (*p == '[') {
            char *name;

            pending_flush(&cur_key, &cur_val, cur_opts);
            if ((name = parse_section(p, url, nline)) == NULL)
                goto l_err;

            if (strcmp(name, "main") == 0) {
                cur_opts = NULL;

            } else {
                cur_opts = n_hash_get(sections, name);
                if (cur_opts == NULL) {
                    cur_opts = n_hash_new(8, free);
                    n_hash_insert(sections, name, cur_opts);
                }
            }
            free(name);
            continue;
        }

        if (p != begin) {       /* a continuation of the value above */
            if (cur_key == NULL) {
                logn(LOGERR, _("%s:%d: an indented line without an option"),
                     url, nline);
                goto l_err;
            }

            n_buf_putc(cur_val, '\n');
            n_buf_puts(cur_val, p);
            continue;
        }

        value = strchr(p, '=');
        if (value == NULL) {
            logn(LOGERR, _("%s:%d: missing '='"), url, nline);
            goto l_err;
        }

        pending_flush(&cur_key, &cur_val, cur_opts);

        *value++ = '\0';
        key = n_str_strip_ws(p);
        for (char *q = key; *q; q++)
            *q = tolower((unsigned char)*q);

        value = n_str_strip_ws(value);

        cur_key = n_strdup(key);
        cur_val = n_buf_new(64);
        n_buf_puts(cur_val, value);
    }

    pending_flush(&cur_key, &cur_val, cur_opts);
    free(line);
    fclose(f);
    return sections;

 l_err:
    pending_flush(&cur_key, &cur_val, cur_opts);
    free(line);
    fclose(f);
    n_hash_free(sections);
    return NULL;
}

/* the directory a repository URL lives in; a local relative path is made
   absolute, "foo.repo" means the current directory */
static char *url_directory(const char *url)
{
    char cwd[PATH_MAX];
    char *dir, *slash, *path;
    size_t size;

    dir = n_strdup(url);
    slash = strrchr(dir, '/');
    if (slash == NULL) {
        free(dir);
        if (getcwd(cwd, sizeof(cwd)) == NULL)
            return n_strdup(".");
        return n_strdup(cwd);
    }
    *slash = '\0';

    if (dir[0] == '\0' || dir[0] == '/' || strstr(dir, "://") != NULL)
        return dir;

    if (getcwd(cwd, sizeof(cwd)) == NULL)
        return dir;

    size = strlen(cwd) + strlen(dir) + 2;
    path = n_malloc(size);
    n_snprintf(path, size, "%s/%s", cwd, dir);
    free(dir);
    return path;
}

/* path of a baseurl relative to the repository file, as yum did */
static char *resolve_baseurl(const char *baseurl, const char *url)
{
    char *dir, *path;
    size_t size;

    if (strstr(baseurl, "://") != NULL || baseurl[0] == '/')
        return n_strdup(baseurl);

    dir = url_directory(url);
    size = strlen(dir) + strlen(baseurl) + 2;
    path = n_malloc(size);
    n_snprintf(path, size, "%s/%s", dir, baseurl);
    free(dir);
    return path;
}

static void warn_unsupported(const char *url, const char *id, tn_hash *opts,
                             const char *key)
{
    if (n_hash_exists(opts, key))
        logn(LOGWARN, _("%s: [%s]: %s is not supported, ignored"),
             url, id, key);
}

static int section_to_source(tn_hash *htconf, const char *url, const char *id,
                             tn_hash *opts, tn_hash *macros)
{
    const char *value, *gpgcheck;
    tn_array *baseurls;
    tn_hash *sec;
    char *path;

    value = n_hash_get(opts, "enabled");
    if (value) {
        int enabled = poldek_util_parse_bool(value);

        if (enabled == 0) {
            msgn(1, _("%s: [%s] is disabled, skipped"), url, id);
            return 1;

        } else if (enabled < 0) {
            logn(LOGWARN, _("%s: [%s]: invalid enabled value '%s'"),
                 url, id, value);
        }
    }

    value = n_hash_get(opts, "baseurl");
    if (value == NULL) {
        if (n_hash_exists(opts, "mirrorlist") ||
            n_hash_exists(opts, "metalink")) {

            logn(LOGWARN, _("%s: [%s]: mirrorlist and metalink are not "
                            "supported, skipped"), url, id);
        } else {
            logn(LOGWARN, _("%s: [%s]: no baseurl, skipped"), url, id);
        }
        return 1;
    }

    char *expanded = expand_vars(value, macros);
    baseurls = split_list(expanded);
    free(expanded);

    int nbaseurls = n_array_size(baseurls);
    if (nbaseurls == 0) {
        n_array_free(baseurls);
        logn(LOGWARN, _("%s: [%s]: empty baseurl, skipped"), url, id);
        return 1;
    }

    if (nbaseurls > 1)
        logn(LOGWARN, _("%s: [%s]: %d baseurls, only the first is used: %s"),
             url, id, nbaseurls, (const char *)n_array_nth(baseurls, 0));

    path = resolve_baseurl(n_array_nth(baseurls, 0), url);
    n_array_free(baseurls);

    if ((sec = poldek_conf_add_section(htconf, "source")) == NULL) {
        free(path);
        return -1;
    }

    poldek_conf_set(sec, "name", id);
    poldek_conf_set(sec, "path", path);
    poldek_conf_set(sec, "type", "metadata");
    free(path);

    gpgcheck = n_hash_get(opts, "gpgcheck");
    if (gpgcheck == NULL)
        gpgcheck = n_hash_get(opts, "pkg_gpgcheck");

    if (gpgcheck) {
        int enabled = poldek_util_parse_bool(gpgcheck);

        if (enabled >= 0)
            poldek_conf_set(sec, "signed", enabled ? "yes" : "no");
        else
            logn(LOGWARN, _("%s: [%s]: invalid gpgcheck value '%s'"),
                 url, id, gpgcheck);
    }

    value = n_hash_get(opts, "gpgkey");
    if (value) {
        tn_array *keys = split_list(value);

        if (n_array_size(keys) > 0) {
            poldek_conf_set(sec, "signkey", n_array_nth(keys, 0));
            if (n_array_size(keys) > 1)
                logn(LOGWARN, _("%s: [%s]: %d gpgkeys, only the first is "
                                "used"), url, id, n_array_size(keys));
        }
        n_array_free(keys);
    }

    value = n_hash_get(opts, "priority");
    if (value) {
        char *end = NULL;
        long priority = strtol(value, &end, 10);

        if (*end == '\0' && priority >= INT_MIN && priority <= INT_MAX) {
            char buf[32];

            n_snprintf(buf, sizeof(buf), "%ld", priority);
            poldek_conf_set(sec, "pri", buf);
        } else {
            logn(LOGWARN, _("%s: [%s]: invalid priority value '%s'"),
                 url, id, value);
        }
    }

    static const char *unsupported[] = {
        "excludepkgs", "exclude", "includepkgs", "repo_gpgcheck",
        "sslverify", "sslcacert", "sslclientcert", "sslclientkey",
        "username", "password", "proxy",
        NULL
    };

    for (int i = 0; unsupported[i]; i++)
        warn_unsupported(url, id, opts, unsupported[i]);

    return 0;
}

int repo_is_dnf(const char *url)
{
    size_t n = strcspn(url, "?#");
    return n > 5 && strncasecmp(url + n - 5, ".repo", 5) == 0;
}

tn_hash *repo_dnf_to_conf(const char *url, const char *path)
{
    tn_hash *sections, *htconf, *global;
    tn_hash *macros, *macros_htconf;
    tn_array *ids;
    char *base;
    int i, nerr = 0, nsrc = 0;

    if ((sections = parse_file(url, path)) == NULL)
        return NULL;

    if ((htconf = conf__new_htconf("global")) == NULL) {
        n_hash_free(sections);
        return NULL;
    }

    global = poldek_conf_get_section(htconf, "global");
    base = url_directory(url);
    poldek_conf_set(global, "_baseurl", base);
    free(base);
    poldek_conf_set(global, "_origin", url);

    macros_htconf = macros_new();
    if (macros_htconf == NULL) {
        n_hash_free(sections);
        n_hash_free(htconf);
        return NULL;
    }
    macros = poldek_conf_get_section(macros_htconf, "global");
    ids = n_hash_keys(sections);
    n_array_sort(ids);

    for (i = 0; i < n_array_size(ids); i++) {
        const char *id = n_array_nth(ids, i);
        tn_hash *opts = n_hash_get(sections, id);
        int rc;

        rc = section_to_source(htconf, url, id, opts, macros);
        if (rc < 0)
            nerr++;
        else if (rc == 0)
            nsrc++;
    }

    n_array_free(ids);
    n_hash_free(macros_htconf);
    n_hash_free(sections);

    if (nerr) {
        n_hash_free(htconf);
        return NULL;
    }

    if (nsrc == 0) {
        logn(LOGERR, _("%s: no enabled repository with a baseurl"), url);
        n_hash_free(htconf);
        return NULL;
    }

    return htconf;
}
