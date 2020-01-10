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

#include <stdio.h>
#include <fnmatch.h>
#include <sys/param.h>          /* for PATH_MAX */

#include <trurl/nmalloc.h>
#include <trurl/nassert.h>
#include <trurl/n_snprintf.h>
#include <trurl/nstr.h>

#include "vfile/vfile.h"
#include "sigint/sigint.h"

#include "compiler.h"
#include "pkgdir/pkgdir.h"
#include "pkgdir/pkgdir_intern.h"
#include "pkgset.h"
#include "conf.h"
#include "log.h"
#include "misc.h"
#include "i18n.h"
#include "poldek.h"
#include "poldek_intern.h"
#include "poldek_term.h"
#include "pm/pm.h"
#include "conf_intern.h"

extern int (*poldek_log_say_goodbye)(const char *msg); /* log.c */

static int poldeklib_init_called = 0;

/* _iflags */
#define SOURCES_SETUPDONE   (1 << 1)
#define CACHEDIR_SETUPDONE  (1 << 2)
#define SOURCES_LOADED      (1 << 3)
#define SETUP_DONE          (1 << 4)

const char poldek_BUG_MAILADDR[] = "http://bugs.launchpad.net/poldek";
const char poldek_VERSION_BANNER[] = PACKAGE " " VERSION " (" VERSION_STATUS ")";
const char poldek_BANNER[] = PACKAGE " " VERSION " (" VERSION_STATUS ")\n"
"Copyright (C) 2000-" VERSION_YEAR " Pawel A. Gajda <mis@pld-linux.org>\n"
"This program may be freely redistributed under the terms of the GNU GPL v2";

static int say_goodbye(const char *msg);
int (*poldek_say_goodbye)(const char *msg) = say_goodbye;

static
void (*poldek_assert_hook)(const char *expr, const char *file, int line) = NULL;

static
void (*poldek_malloc_fault_hook)(void) = NULL;

static
void (*poldek_die_hook)(const char *msg) = NULL;

static void register_vf_handlers_compat(const tn_hash *htcnf);
static void register_vf_handlers(const tn_array *fetchers);

static int setup_legacy_sources(tn_hash *htcnf);

static
int get_conf_sources(struct poldek_ctx *ctx, tn_array *sources,
                     tn_array *srcs_named,
                     tn_array *htcnf_sources);

static
int get_conf_opt_list(const tn_hash *htcnf, const char *name, tn_array *tolist);

static
int do_poldek_setup_cachedir(struct poldek_ctx *ctx);


struct default_op_map_ent {
    int config;                 /* configuration var? */
    char *name;
    int op;
    int _defaultv;
    int defaultv;
    int optype;
};

static tn_array *default_op_map = NULL; /* see build_default_op_map() */

int poldek__is_setup_done(struct poldek_ctx *ctx)
{
    return (ctx->_iflags & SETUP_DONE) == SETUP_DONE;
}

static inline void check_if_setup_done(struct poldek_ctx *ctx)
{
    if ((ctx->_iflags & SETUP_DONE) == SETUP_DONE)
        return;

    logn(LOGERR | LOGDIE, "poldek_setup() call is a must...");
}

static
int addsource(tn_array *sources, struct source *src,
              int justaddit, tn_array *srcs_named, int *matches)
{
    int rc = 0;

    if (n_array_size(srcs_named) == 0 || justaddit) {
        sources_add(sources, src);
        rc = 1;

    } else {
        int i;
        int added = 0;

        for (i=0; i < n_array_size(srcs_named); i++) {
            struct source *s = n_array_nth(srcs_named, i);

            if (fnmatch(s->name, src->name, 0) == 0) {
                matches[i]++;
                if (added)
                    continue;

                /* given by name -> clear flags */
                src->flags &= ~(PKGSOURCE_NOAUTO | PKGSOURCE_NOAUTOUP);

                /* reproritize */
                src->no = s->no + matches[i];
                src->pri = 0;

                sources_add(sources, src);
                added = 1;
                rc = 1;
            }
        }
    }

    return rc;
}


static
int prepare_sources(struct poldek_ctx *ctx,
                    tn_hash *poldek_cnf, tn_array *sources)
{
    struct source   *src;
    int             i, rc = 1;
    tn_array        *srcs_path, *srcs_named;


    sources_score(sources);

    srcs_path = n_array_clone(sources);
    srcs_named = n_array_clone(sources);

    for (i=0; i < n_array_size(sources); i++) {
        src = n_array_nth(sources, i);
        /* supplied by -n */
        if ((src->flags & PKGSOURCE_NAMED) && src->path == NULL)
            n_array_push(srcs_named, source_link(src));
        else if (src->path)
            n_array_push(srcs_path, source_link(src));
        else {
            logn(LOGERR, "%s: source without name nor path",
                 src->name ? src->name : "null");
            rc = 0;
        }
    }

    if (poldek_cnf && (n_array_size(srcs_named) > 0 || /* load configured sources? */
                       n_array_size(sources) == 0)) {

        tn_array *htcnf_sources;

        setup_legacy_sources(poldek_cnf);
        htcnf_sources = poldek_conf_get_sections(poldek_cnf, "source");
        rc = get_conf_sources(ctx, srcs_path, srcs_named, htcnf_sources);

    } else if (n_array_size(srcs_named)) {
        for (i=0; i < n_array_size(srcs_named); i++) {
            src = n_array_nth(srcs_named, i);
            logn(LOGERR, _("%s: no such source"), src->name);
        }

        rc = 0;
    }

    n_array_free(srcs_named);
    n_array_clean(sources);

    for (i=0; i < n_array_size(srcs_path); i++) {
        src = n_array_nth(srcs_path, i);
        n_array_push(sources, source_link(src));
    }

    n_array_free(srcs_path);
    n_array_sort(sources);
    n_array_uniq_ex(sources, (tn_fn_cmp)source_cmp_uniq);

    sources_score(sources);

    return rc;
}

static
struct source *do_source_new_htcnf(struct poldek_ctx *ctx,
                                   tn_hash *htcnf, int no)
{
    struct source *src;
    const char *vs;

    /* set the name if missing; all sources loaded from config must be named */
    if ((vs = poldek_conf_get(htcnf, "name", NULL)) == NULL) {
        char name[32];
        n_snprintf(name, sizeof(name), "src%.2d", no);
        poldek_conf_set(htcnf, "name", name);
    }

    src = source_new_htcnf(htcnf);
    if (src == NULL)
        return NULL;

    if (n_array_size(src->exclude_path) == 0 && /* take global exclude path */
        n_array_size(ctx->ts->exclude_path) > 0) {

        n_array_free(src->exclude_path);
        src->exclude_path = n_array_dup(ctx->ts->exclude_path,
                                        (tn_fn_dup)strdup);
    }

    if (n_array_size(src->ign_patterns) == 0 && /* take global  */
        n_array_size(ctx->ts->ign_patterns) > 0) {

        n_array_free(src->ign_patterns);
        src->ign_patterns = n_array_dup(ctx->ts->ign_patterns,
                                        (tn_fn_dup)strdup);
    }

    return src;

}

static
tn_array *expand_sources_group(tn_array *srcs_named, tn_array *htcnf_sources,
                               tn_hash *expanded_h)
{
    int             i, j, *htcnf_matches, *isgroup_matches;
    tn_array        *sources;

    htcnf_matches = alloca(n_array_size(htcnf_sources) * sizeof(int));
    memset(htcnf_matches, 0, n_array_size(htcnf_sources) * sizeof(int));

    isgroup_matches = alloca(n_array_size(htcnf_sources) * sizeof(int));
    memset(isgroup_matches, 0, n_array_size(htcnf_sources) * sizeof(int));

    sources = n_array_clone(srcs_named);

    for (i=0; i < n_array_size(srcs_named); i++) {
        struct source *s = n_array_nth(srcs_named, i);

        for (j=0; j < n_array_size(htcnf_sources); j++) {
            const char *name, *type, *grp;
            tn_hash *ht;

            ht = n_array_nth(htcnf_sources, j);

            if (isgroup_matches[j]) /* just for efficiency */
                continue;

            type = poldek_conf_get(ht, "type", NULL);
            grp = poldek_conf_get(ht, "group", NULL);
            /* skip not "group" */
            if ((type == NULL || n_str_ne(type, source_TYPE_GROUP)) && !grp) {
                isgroup_matches[j] = 1;
                continue;
            }

            name = poldek_conf_get(ht, "name", NULL);
            n_assert(name);

            if (!grp) { // old groups
                if (htcnf_matches[j] == 0 && fnmatch(s->name, name, 0) == 0) {
                    tn_array *names;
                    int ii;

                    names = poldek_conf_get_multi(ht, "sources");
                    n_assert(names);

                    for (ii=0; ii < n_array_size(names); ii++) {
                        struct source *src = source_new(n_array_nth(names, ii), NULL, NULL, NULL);
                        DBGF("%s -> %s\n", s->name, (char*)n_array_nth(names, ii));
                        src->no = s->no + 1 + ii; /* XXX: hope we fit (see sources_add()) */
                        n_array_push(sources, src);
                    }
                    n_hash_replace(expanded_h, s->name, NULL);
                    htcnf_matches[j] = 1;
                }
            } else if (fnmatch(s->name, grp, 0) == 0) { // new groups
                struct source *src = source_new(name, NULL, NULL, NULL);
                src->no = s->no + 1;
                n_array_push(sources, src);
                n_hash_replace(expanded_h, s->name, NULL);
            }
        }

        n_array_push(sources, source_link(s));
    }

#if ENABLE_TRACE
    for (i=0; i < n_array_size(sources); i++) {
        struct source *s = n_array_nth(sources, i);
        DBGF("%d %s\n", i, s->name);
    }
#endif
    return sources;
}

/* legacy: put source to configuration to load it later in std. way */
static int source_to_htconf(struct source *src, int no, tn_hash *htcnf)
{
    void *sect;
    char buf[64], *name, *path_param = "path";

    if (src->path == NULL)
        return 0;

    sect = poldek_conf_add_section(htcnf, "source");

    name = src->name;
    if (name == NULL) {
        n_snprintf(buf, sizeof(buf), "lsrc%.2d", no);
        name = buf;
    }

    poldek_conf_add_to_section(sect, "name", name);

    if (src->type) {
        poldek_conf_add_to_section(sect, "type", src->type);
        if (n_str_eq(src->type, source_TYPE_GROUP))
            path_param = "sources";
    }

    if (src->path)
        poldek_conf_add_to_section(sect, path_param, src->path);

    if (src->pkg_prefix)
        poldek_conf_add_to_section(sect, "prefix", src->pkg_prefix);

    if (src->flags & PKGSOURCE_NOAUTO)
        poldek_conf_add_to_section(sect, "auto", "no");

    if (src->flags & PKGSOURCE_GROUP && src->group)
        poldek_conf_add_to_section(sect, "group", src->group);

    if (src->flags & PKGSOURCE_NOAUTOUP)
        poldek_conf_add_to_section(sect, "autoup", "no");

    if (src->flags & PKGSOURCE_VRFY_SIGN)
        poldek_conf_add_to_section(sect, "signed", "yes");

    if (src->flags & PKGSOURCE_COMPR)
        poldek_conf_add_to_section(sect, "compr", "yes");

    if (src->dscr)
        poldek_conf_add_to_section(sect, "lang", src->dscr);

    return 1;
}

static
struct source *new_legacy_source(const char *pathspec, const char *pkg_prefix)
{
    struct source *src = source_new_pathspec(NULL, pathspec, pkg_prefix);
    if ((src->flags & PKGSOURCE_TYPE) == 0)
        source_set_type(src, "pndir");
    return src;
}

static int setup_legacy_sources(tn_hash *poldek_cnf)
{
    struct source *src;
    tn_array *list;
    tn_hash *htcnf;
    int i, no = 0;

    htcnf = poldek_conf_get_section(poldek_cnf, "global");

    if ((list = poldek_conf_get_multi(htcnf, "source"))) {
        for (i=0; i < n_array_size(list); i++) {
            src = new_legacy_source(n_array_nth(list, i), NULL);
            source_to_htconf(src, no++, poldek_cnf);
            source_free(src);
        }
        n_array_free(list);
    }

    /* source\d+, prefix\d+ pairs  */
    for (i=0; i < 100; i++) {
        const char *src_val;
        char opt[64];

        snprintf(opt, sizeof(opt), "source%d", i);
        if ((src_val = poldek_conf_get(htcnf, opt, NULL))) {
            snprintf(opt, sizeof(opt), "prefix%d", i);
            src = new_legacy_source(src_val, poldek_conf_get(htcnf, opt, NULL));
            source_to_htconf(src, no++, poldek_cnf);
            source_free(src);
        }
    }

    return no;
}

static int get_conf_sources(struct poldek_ctx *ctx, tn_array *sources,
                            tn_array *srcs_named, tn_array *htcnf_sources)
{
    struct source   *src;
    int             i, nerr = 0, getall = 0;
    int             *matches = NULL;
    tn_array        *expanded_srcs_named = NULL;
    tn_hash         *expanded_h  = NULL;

    if (n_array_size(srcs_named) == 0 && n_array_size(sources) == 0)
        getall = 1;

    else if (n_array_size(srcs_named) > 0 && htcnf_sources) {
        expanded_h = n_hash_new(16, NULL);
        expanded_srcs_named = expand_sources_group(srcs_named, htcnf_sources,
                                                   expanded_h);

        srcs_named = expanded_srcs_named;
        matches = alloca(n_array_size(srcs_named) * sizeof(int));
        memset(matches, 0, n_array_size(srcs_named) * sizeof(int));
    }

    if (htcnf_sources) {
        for (i=0; i < n_array_size(htcnf_sources); i++) {
            const char *type;
            tn_hash *ht = n_array_nth(htcnf_sources, i);

            type = poldek_conf_get(ht, "type", NULL);
            if (type && n_str_eq(type, source_TYPE_GROUP)) /* skip "group" */
                continue;

            src = do_source_new_htcnf(ctx, ht, n_array_size(sources));
            if (src == NULL)
                nerr++;
            else if (!addsource(sources, src, getall, srcs_named, matches))
                source_free(src);
        }
    }

    for (i=0; i < n_array_size(srcs_named); i++) {
        src = n_array_nth(srcs_named, i);
        if (matches == NULL ||
            (matches[i] == 0 && !n_hash_exists(expanded_h, src->name))) {
            logn(LOGERR, _("%s: no such source"), src->name);
            nerr++;
        }
    }

    if (nerr == 0 && getall)
        for (i=0; i < n_array_size(sources); i++) {
            src = n_array_nth(sources, i);
            src->no = i;
        }

    n_array_cfree(&expanded_srcs_named);
    if (expanded_h)
        n_hash_free(expanded_h);
    return nerr == 0;
}


static int get_conf_opt_list(const tn_hash *htcnf, const char *name,
                             tn_array *tolist)
{
    tn_array *list;
    int i = 0;

    /* already set, manually */
    if (n_array_size(tolist) > 0)
        return 0;

    if ((list = poldek_conf_get_multi(htcnf, name))) {
        for (i=0; i < n_array_size(list); i++)
            n_array_push(tolist, n_strdup(n_array_nth(list, i)));

        n_array_free(list);
    }

    n_array_sort(tolist);
    n_array_uniq(tolist);
    return i;
}


static char *extract_handler_name(char *name, int size, const char *cmd)
{
    char *p;

    n_snprintf(name, size, "%s", cmd);
    if ((p = strchr(name, ' ')))
        *p = '\0';

    name = n_basenam(name);
    return name;
}


static void register_vf_handlers_compat(const tn_hash *htcnf)
{
    char name[128];
    const char *vs;
    tn_array *protocols;

    protocols = n_array_new(2, NULL, (tn_fn_cmp)strcmp);

    if ((vs = poldek_conf_get(htcnf, "ftp_http_get", NULL))) {
        extract_handler_name(name, sizeof(name), vs);
        n_array_clean(protocols);
        n_array_push(protocols, "ftp");
        n_array_push(protocols, "http");
        //vfile_cnflags |= VFILE_USEXT_FTP | VFILE_USEXT_HTTP;
        vfile_register_ext_handler(name, protocols, vs);
    }

    if ((vs = poldek_conf_get(htcnf, "ftp_get", NULL))) {
        //vfile_cnflags |= VFILE_USEXT_FTP;
        extract_handler_name(name, sizeof(name), vs);
        n_array_clean(protocols);
        n_array_push(protocols, "ftp");
        vfile_register_ext_handler(name, protocols, vs);
    }

    if ((vs = poldek_conf_get(htcnf, "http_get", NULL))) {
        //vfile_cnflags |= VFILE_USEXT_HTTP;
        extract_handler_name(name, sizeof(name), vs);
        n_array_clean(protocols);
        n_array_push(protocols, "http");
        vfile_register_ext_handler(name, protocols, vs);
    }

    if ((vs = poldek_conf_get(htcnf, "https_get", NULL))) {
        //vfile_cnflags |= VFILE_USEXT_HTTPS;
        extract_handler_name(name, sizeof(name), vs);
        n_array_clean(protocols);
        n_array_push(protocols, "https");
        vfile_register_ext_handler(name, protocols, vs);
    }

    if ((vs = poldek_conf_get(htcnf, "rsync_get", NULL))) {
        extract_handler_name(name, sizeof(name), vs);
        n_array_clean(protocols);
        n_array_push(protocols, "rsync");
        vfile_register_ext_handler(name, protocols, vs);
    }

    if ((vs = poldek_conf_get(htcnf, "cdrom_get", NULL))) {
        extract_handler_name(name, sizeof(name), vs);
        n_array_clean(protocols);
        n_array_push(protocols, "cdrom");
        vfile_register_ext_handler(name, protocols, vs);
    }
    n_array_free(protocols);
}


static void register_vf_handlers(const tn_array *fetchers)
{
    char name[128];
    tn_array *protocols;
    int i;

    if (fetchers == NULL)
        return;

    protocols = n_array_new(2, NULL, (tn_fn_cmp)strcmp);

    for (i=0; i<n_array_size(fetchers); i++) {
        const char *nam, *cmd;
        tn_hash  *ht;

        ht = n_array_nth(fetchers, i);

        nam = poldek_conf_get(ht, "name", NULL);
        cmd  = poldek_conf_get(ht, "cmd", NULL);

        if (cmd == NULL)
            continue;

        if (nam == NULL)
            nam = extract_handler_name(name, sizeof(name), cmd);

        if (nam == NULL)
            continue;

        n_array_clean(protocols);
        get_conf_opt_list(ht, "proto", protocols);
        if (n_array_size(protocols) == 0)
            continue;

        vfile_register_ext_handler(nam, protocols, cmd);
    }
    n_array_free(protocols);
}

int set_default_vf_fetcher(int tag, const char *confvalue)
{
    const char **tl, **tl_save, *name;
    char  *p, *val;
    int   len;

    len = strlen(confvalue) + 1;
    val = alloca(len);
    memcpy(val, confvalue, len);

    if ((p = strchr(val, ':')) == NULL || *(p + 1) == '/')
        return 0;

    *p = '\0';
    p++;
    while (isspace(*p))
        p++;
    name = p;

    tl = tl_save = n_str_tokl(val, ", \t");
    while (*tl) {
        vfile_configure(tag, *tl, name);
        DBGF("%d (%s) -> (%s)\n", tag, *tl, name);
        tl++;
    }

    n_str_tokl_free(tl_save);
    return 1;
}


static void zlib_in_rpm(struct poldek_ctx *ctx)
{
    char              *argv[2], cmd[256];
    const char        *libdir;
    struct p_open_st  pst;
    tn_hash           *htcnf;
    int               ec;


    htcnf = poldek_conf_get_section(ctx->htconf, "global");
    libdir = poldek_conf_get(htcnf, "_libdir", NULL);
    if (libdir == NULL)
        libdir = "/usr/lib";

    n_snprintf(cmd, sizeof(cmd), "%s/poldek/zlib-in-rpm.sh", libdir);

    argv[0] = cmd;
    argv[1] = NULL;

    p_st_init(&pst);
    if (p_open(&pst, 0, cmd, argv) == NULL) {
        p_st_destroy(&pst);
        return;
    }

    if ((ec = p_close(&pst)) == 0) {
        logn(LOGNOTICE, "zlib-in-rpm detected, enabling workaround");
        vfile_configure(VFILE_CONF_EXTCOMPR, 1);
    }

    p_st_destroy(&pst);
}

int default_op_map_ent_cmp(struct default_op_map_ent *e1,
                           struct default_op_map_ent *e2)
{
    return strcmp(e1->name, e2->name);
}

static tn_array *build_default_op_map(void)
{
    struct poldek_conf_tag *tags = NULL;
    tn_array *op_map;
    int i = 0;
    struct default_op_map_ent *ent;
    struct default_op_map_ent addon[] = {
        { 0, "(!no)hold", POLDEK_OP_HOLD,   1,  1, CONF_TYPE_BOOLEAN  },
        { 0, "(!no)ingore", POLDEK_OP_IGNORE, 1, 1, CONF_TYPE_BOOLEAN  },
        { 0, NULL, 0, 0, 0, 0 }
    };

    op_map = n_array_new(32, free, (tn_fn_cmp)default_op_map_ent_cmp);

    while (poldek_conf_sections[i].name) {
        if (n_str_eq(poldek_conf_sections[i].name, "global")) {
            tags = poldek_conf_sections[i].tags;
            break;
        }
        i++;
    }
    n_assert(tags);

    i = 0;
    while (tags[i].name) {
        if (tags[i]._op_no == 0) {
            i++;
            continue;
        }

        ent = n_malloc(sizeof(*ent));
        n_assert(tags[i].flags & (CONF_TYPE_BOOLEAN | CONF_TYPE_BOOLEAN3));
        ent->config = 1;
        ent->name = tags[i].name;
        ent->op = tags[i]._op_no;

        if (tags[i].flags & CONF_TYPE_BOOLEAN) {
            ent->_defaultv = poldek_util_parse_bool(tags[i].defaultv);
            ent->defaultv = ent->_defaultv;
            n_assert(ent->defaultv == 0 || ent->defaultv == 1);
            ent->optype = CONF_TYPE_BOOLEAN;

        } else if (tags[i].flags & CONF_TYPE_BOOLEAN3) {
            ent->_defaultv = poldek_util_parse_bool3(tags[i].defaultv);
            n_assert(ent->_defaultv == 0 || ent->_defaultv == 1 ||
                     ent->_defaultv == 2);

            ent->defaultv = ent->_defaultv;
            ent->optype = CONF_TYPE_BOOLEAN3;

        } else {
            n_assert(0);
        }

        n_array_push(op_map, ent);
        i++;
    }

    i = 0;
    while (addon[i].name) {
        ent = n_malloc(sizeof(*ent));
        ent->config = 0;
        ent->name = addon[i].name;
        ent->op = addon[i].op;
        ent->_defaultv = ent->defaultv = addon[i].defaultv;
        ent->optype = addon[i].optype;
        n_array_push(op_map, ent);
        i++;
    }
#if ENABLE_TRACE
    for (i=0; i<n_array_size(op_map); i++) {
        struct default_op_map_ent *ent = n_array_nth(op_map, i);
        DBGF("%s %d\n", ent->name, ent->_defaultv);
    }
#endif
    n_array_sort(op_map);
    return op_map;
}

static
void poldek__ts_apply_config(struct poldek_ctx *ctx, struct poldek_ts *ts)
{
    tn_hash           *htcnf = NULL;
    int               i;

    n_assert(default_op_map);

    if (ctx->htconf == NULL)
        return;

    htcnf = poldek_conf_get_section(ctx->htconf, "global");

    DBGF("ts %p, tsctx %p\n", ts, ctx->ts);
    DBGF("default_op_map size = %d\n", n_array_size(default_op_map));

    for (i=0; i < n_array_size(default_op_map); i++) {
        struct default_op_map_ent *ent = n_array_nth(default_op_map, i);

        DBGF("op[%d] %s (config %d)\n", i, ent->name,  ent->config);

        if (ent->config == 0)   /* omit non-config options */
            continue;


        n_assert(ent->defaultv >= 0);

        if (poldek_ts_op_touched(ts, ent->op)) { /* modified by cmdl opts */
            DBGF(" %d. - ldconfig %s(%d) = %d\n", i, ent->name, ent->op,
                 ts->getop(ts, ent->op));

        } else {
            int v = -1;

            if (ent->optype == CONF_TYPE_BOOLEAN)
                v = poldek_conf_get_bool(htcnf, ent->name, ent->defaultv);

            else if (ent->optype == CONF_TYPE_BOOLEAN3)
                v = poldek_conf_get_bool3(htcnf, ent->name, ent->defaultv);
            else
                n_assert(0);

            if (ent->optype == CONF_TYPE_BOOLEAN3 && v == 2) { /* auto */
                if (ent->defaultv != 0 && ent->defaultv != 1)
                    continue;
                v = ent->defaultv;
            }

            n_assert(v == 0 || v == 1);
            DBGF(" %d. + ldconfig %s(%d) = %d\n", i, ent->name, ent->op, v);
            poldek_ts_xsetop(ts, ent->op, v, 0);
        }
    }

    if (ts->getop(ts, POLDEK_OP_GREEDY)) {
        int v = poldek_conf_get_bool(htcnf, "aggressive greedy", 1);
        poldek_ts_xsetop(ts, POLDEK_OP_AGGREEDY, v, 0);
        poldek_ts_xsetop(ts, POLDEK_OP_FOLLOW, 1, 0);
    }
}

#if ENABLE_TRACE
void poldek__ts_dump_settings(struct poldek_ctx *ctx, struct poldek_ts *ts)
{
    int i;

    n_assert(default_op_map);
    DBGF("ts %p, tsctx %p\n", ts, ctx->ts);

    for (i=0; i<n_array_size(default_op_map); i++) {
        struct default_op_map_ent *ent = n_array_nth(default_op_map, i);

        DBGF("%% %s=%s (ctx=%s)\n", ent->name,
             ts->getop(ts, ent->op) ? "y" : "n",
             ctx->ts->getop(ctx->ts, ent->op) ? "y" : "n");
    }
}
#endif

static
void poldek__ts_apply_poldek_settings(struct poldek_ctx *ctx,
                                      struct poldek_ts *ts)
{
    int i = 0;

    n_assert(default_op_map);
    DBGF("ts %p, tsctx %p\n", ts, ctx->ts);

    for (i=0; i<n_array_size(default_op_map); i++) {
        struct default_op_map_ent *ent = n_array_nth(default_op_map, i);

        if (poldek_ts_op_touched(ts, ent->op)) { /* modified by cmdl opts */
            DBGF("  - skip touched %s(%d) = %d, ctxv %d\n", ent->name, ent->op,
                   ts->getop(ts, ent->op), ctx->ts->getop(ctx->ts, ent->op));
            continue;
        }

        if (poldek_ts_op_touched(ctx->ts, ent->op)) {
            DBGF("  + apply %s(%d) = %d (was %d)\n", ent->name, ent->op,
                   ctx->ts->getop(ctx->ts, ent->op), ts->getop(ts, ent->op));

            ts->setop(ts, ent->op, ctx->ts->getop(ctx->ts, ent->op));

        } else {
            DBGF("  - NOT apply %s(%d) = %d (touched=%d)\n", ent->name,
                 ent->op, ctx->ts->getop(ctx->ts, ent->op),
                 poldek_ts_op_touched(ctx->ts, ent->op));
        }

    }

    if (ts->getop(ts, POLDEK_OP_CONFIRM_INST) && poldek_VERBOSE < 1)
        poldek_set_verbose(1);

    if (ts->getop(ts, POLDEK_OP_GREEDY))
        ts->setop(ts, POLDEK_OP_FOLLOW, 1);
}

void poldek__ts_postconf(struct poldek_ctx *ctx, struct poldek_ts *ts)
{
    poldek__ts_apply_poldek_settings(ctx, ts);
    poldek__ts_apply_config(ctx, ts);
}

/* for testing purposes only - test:// fetcher is needed to test
   remote config access */
static int preload_conf(const char *path)
{
    tn_hash *htconf = NULL;

    if ((htconf = poldek_conf_load(path, POLDEK_LDCONF_NOINCLUDE)) == NULL)
        return 0;

    register_vf_handlers(poldek_conf_get_sections(htconf, "fetcher"));
    n_hash_free(htconf);
    return 1;
}

static
tn_hash *do_load_conf(struct poldek_ctx *ctx, const char *path, unsigned flags)
{
    unsigned ldflags = 0;
    tn_hash *htconf = NULL;

    n_assert((flags & POLDEK_LOADCONF_NOCONF) == 0);

    /* setup cachedir if not set */
    if ((ctx->_iflags & CACHEDIR_SETUPDONE) == 0) {
        int succeed = 0;
        n_assert(ctx->ts->cachedir == NULL);

        /* remote config -> setup any cachedir */
        if (path && vf_url_type(path) != VFURL_PATH) {
            succeed = do_poldek_setup_cachedir(ctx);

        } else {  /* remote files may be included, get cachedir first */
            tn_hash *cnf, *globalcnf;
            const char *dir;

            cnf = poldek_conf_load(path, POLDEK_LDCONF_GLOBALONLY);
            if (cnf == NULL)
                return NULL;

            globalcnf = poldek_conf_get_section(cnf, "global");
            n_assert(globalcnf);

            if ((dir = poldek_conf_get(globalcnf, "cachedir", NULL))) {
                poldek_configure(ctx, POLDEK_CONF_CACHEDIR, dir);
                succeed = poldek_setup_cachedir(ctx);

            } else {
                succeed = do_poldek_setup_cachedir(ctx);
            }

            n_hash_free(cnf);
        }

        if (!succeed)
            return NULL;
    }


    if (flags & POLDEK_LOADCONF_UPCONF)
        ldflags |= POLDEK_LDCONF_UPDATE;

    if (path != NULL)
        htconf = poldek_conf_load(path, ldflags);
    else
        htconf = poldek_conf_load_default(ldflags);

    return htconf;
}



int poldek_load_config(struct poldek_ctx *ctx, const char *path,
                       tn_array *addon_cnflines, unsigned flags)
{
    tn_hash           *htcnf = NULL;
    const char        *vs;
    int               v;
    tn_array          *list;

    if (poldek__is_setup_done(ctx))
        logn(LOGERR | LOGDIE, "load_config() called after setup()");

    n_assert(ctx->htconf == NULL);

    if ((flags & POLDEK_LOADCONF_NOCONF) == 0)
        ctx->htconf = do_load_conf(ctx, path, flags);

    if (addon_cnflines)
        ctx->htconf = poldek_conf_addlines(ctx->htconf, "global",
                                           addon_cnflines);

    if (ctx->htconf == NULL)
        return 0;

    poldek__ts_apply_config(ctx, ctx->ts);

    htcnf = poldek_conf_get_section(ctx->htconf, "global");
    register_vf_handlers_compat(htcnf);
    register_vf_handlers(poldek_conf_get_sections(ctx->htconf, "fetcher"));

    if ((list = poldek_conf_get_multi(htcnf, "default_fetcher"))) {
        int i;
        for (i=0; i < n_array_size(list); i++)
            set_default_vf_fetcher(VFILE_CONF_DEFAULT_CLIENT,
                                   n_array_nth(list, i));
        n_array_free(list);
    }

    if ((list = poldek_conf_get_multi(htcnf, "proxy"))) {
        int i;
        for (i=0; i < n_array_size(list); i++)
            set_default_vf_fetcher(VFILE_CONF_PROXY, n_array_nth(list, i));
        n_array_free(list);
    }

    if ((list = poldek_conf_get_multi(htcnf, "noproxy"))) {
        int i;
        for (i=0; i < n_array_size(list); i++)
            vfile_configure(VFILE_CONF_NOPROXY, n_array_nth(list, i));
        n_array_free(list);
    }

    get_conf_opt_list(htcnf, "rpmdef", ctx->ts->rpmacros);
    get_conf_opt_list(htcnf, "hold", ctx->ts->hold_patterns);
    get_conf_opt_list(htcnf, "ignore", ctx->ts->ign_patterns);
    get_conf_opt_list(htcnf, "exclude path", ctx->ts->exclude_path);

    if ((ctx->_iflags & CACHEDIR_SETUPDONE) == 0) /* don't overwrite if set */
        if ((vs = poldek_conf_get(htcnf, "cachedir", NULL))) {
            poldek_configure(ctx, POLDEK_CONF_CACHEDIR, vs);
            poldek_setup_cachedir(ctx);
        }


    if (poldek_conf_get_bool(htcnf, "vfile_ftp_sysuser_as_anon_passwd", 0))
        vfile_configure(VFILE_CONF_SYSUSER_AS_ANONPASSWD, 1);

    if ((vs = poldek_conf_get(htcnf, "default_index_type", NULL)))
        poldek_conf_PKGDIR_DEFAULT_TYPE = n_strdup(vs);

    if ((vs = poldek_conf_get(htcnf, "default_index_compr", NULL)))
        poldek_conf_PKGDIR_DEFAULT_COMPR = n_strdup(vs);

    if (poldek_conf_get_bool(htcnf, "vfile_external_compress", 0))
        vfile_configure(VFILE_CONF_EXTCOMPR, 1);

    else if (poldek_conf_get_bool(htcnf, "auto_zlib_in_rpm", 0))
        zlib_in_rpm(ctx);

    if ((v = poldek_conf_get_int(htcnf, "vfile_retries", 100)) > 0)
        vfile_configure(VFILE_CONF_STUBBORN_NRETRIES, v);

    return 1;
}

static int say_goodbye(const char *msg)
{
    printf("%s", msg);
    return 1;                   /* let the caller die() itself */
}

static void n_malloc_fault(void)
{
    if (poldek_say_goodbye("Something wrong, something not quite right...\n"
                           "Memory exhausted\n"))
        exit(EXIT_FAILURE);
}


static void n_assert_hook(const char *expr, const char *file, int line)
{
    char msg[1024];
    n_snprintf(msg, sizeof(msg), "Something wrong, something not quite right"
               " with %s\n"
               "Assertion '%s' failed, %s:%d\n"
               "Please report this bug to: %s\n\n",
               VERSION " (" VERSION_STATUS ")",
               expr, file, line,  poldek_BUG_MAILADDR);
    if (poldek_say_goodbye(msg))
        abort();
}

static void n_die_hook(const char *msg)
{
    char buf[1024];
    n_snprintf(buf, sizeof(buf),
               "Something wrong, something not quite right.\n%s; die: %s\n",
               poldek_VERSION_BANNER, msg);

    if (poldek_say_goodbye(buf))
        abort();
}

static void verify_setuid(void)
{
    uid_t uid;

    uid = getuid();
    if (uid != geteuid() || getgid() != getegid()) {
        logn(LOGERR, _("I'm set*id'ed, give up"));
        exit(EXIT_FAILURE);
    }
#if 0
    if (uid == 0) {
        logn(LOGWARN, _("Running me as root is not a good habit"));
        sleep(1);
    }
#endif
}

static void init_internal(void)
{
#ifdef HAVE_MALLOPT
# include <malloc.h>

#if defined HAVE_MALLOC_H && defined POLDEK_MEM_DEBUG
    old_malloc_hook = __malloc_hook;
    __malloc_hook = Fnn;
#endif
    mallopt(M_MMAP_THRESHOLD, 1024);
    //mallopt(M_MMAP_MAX, 0);

#endif /* HAVE_MALLOPT */

    if (poldek_malloc_fault_hook == NULL)
        poldek_malloc_fault_hook = n_malloc_fault;

    if (poldek_assert_hook == NULL)
        poldek_assert_hook = n_assert_hook;

    if (poldek_die_hook == NULL)
        poldek_die_hook = n_die_hook;

    n_assert_set_hook(poldek_assert_hook);
    n_die_set_hook(poldek_die_hook);
    n_malloc_set_failhook(poldek_malloc_fault_hook);
}

static
void poldek_destroy(struct poldek_ctx *ctx)
{
    ctx = ctx;

    vfile_destroy();

    if (ctx->htconf)
        n_hash_free(ctx->htconf);
    sigint_destroy();

    n_array_free(ctx->sources);
    n_array_free(ctx->dest_sources);

    if (ctx->pkgdirs) {
        n_array_free(ctx->pkgdirs);
        ctx->pkgdirs = NULL;
    }

    if (ctx->ps)
        pkgset_free(ctx->ps);

    poldek_ts_free(ctx->ts);
    n_hash_free(ctx->_cnf);
    if (ctx->pmctx)
        pm_free(ctx->pmctx);

}

void poldek_free(struct poldek_ctx *ctx)
{
    if (ctx->_refcnt > 0) {
        ctx->_refcnt--;
        return;
    }

    poldek_destroy(ctx);
    free(ctx);
}


static
void fclose_void_callback(void *file)
{
    fclose(file);
}

int poldek_configure(struct poldek_ctx *ctx, int param, ...)
{
    va_list ap;
    void    *vv;
    char    *vs;
    int rc = 1;

    va_start(ap, param);

    switch (param) {
        case POLDEK_CONF_SOURCE:
            vv = va_arg(ap, void*);
            if (vv) {
                struct source *src = (struct source*)vv;
                if (src->path)
                    src->path = poldek__conf_path(src->path, NULL);
                sources_add(ctx->sources, src);
            }
            break;

        case POLDEK_CONF_DESTINATION:
            vv = va_arg(ap, void*);
            if (vv) {
                struct source *src = (struct source*)vv;
                if (src->path)
                    src->path = poldek__conf_path(src->path, NULL);
                sources_add(ctx->dest_sources, src);
            }
            break;

        case POLDEK_CONF_PM:
            vv = va_arg(ap, void*);
            if (vv)
                n_hash_replace(ctx->_cnf, "pm", n_strdup(vv));
            break;

        /*
          XXX: log appenders are incomplete: a) there is no way to remove
               particular one; b) poldek_log_set_appender()
               removes all appenders added before.
         */
        case POLDEK_CONF_LOGFILE:
            if ((vs = va_arg(ap, char*))) {
                FILE *stream = fopen(vs, "a");
                if (stream == NULL) {
                    log(LOGERR, "%s: open failed: %m", vs);
                    rc = 0;
                } else {
                    poldek_log_add_appender("_FILE", stream,
                                            fclose_void_callback,
                                            0,  NULL);
                }
            }

            break;

        case POLDEK_CONF_LOGTTY:
            poldek_log_set_default_appender("_TTY", NULL, NULL);
            break;

        case POLDEK_CONF_PROGRESS:
            ctx->ts->setop(ctx->ts, POLDEK_OP_PROGRESS_NONE, 1);
            vfile_configure(VFILE_CONF_PROGRESS_NONE, 1);
            break;

        case POLDEK_CONF_VFILEPROGRESS:
            if ((vv = va_arg(ap, void*)))
                vfile_configure(VFILE_CONF_PROGRESS, vv);
            break;

        case POLDEK_CONF_LAZY_DEPPROCESS:
            vv = va_arg(ap, char*);
            if (vv) {
                ctx->_ps_setup_flags |= PSET_NODEPS;
            }
            break;

        case POLDEK_CONF_CONFIRM_CB:
            if ((vv = va_arg(ap, void*)))
                ctx->confirm_fn = vv;

            if ((vv = va_arg(ap, void*)))
                ctx->data_confirm_fn = vv;

            break;

        case POLDEK_CONF_TSCONFIRM_CB:
            if ((vv = va_arg(ap, void*)))
                ctx->ts_confirm_fn = vv;

            if ((vv = va_arg(ap, void*)))
                ctx->data_ts_confirm_fn = vv;
            break;

        case POLDEK_CONF_CHOOSEEQUIV_CB:
            if ((vv = va_arg(ap, void*)))
                ctx->choose_equiv_fn = vv;

            if ((vv = va_arg(ap, void*)))
                ctx->data_choose_equiv_fn = vv;
            break;

        case POLDEK_CONF_CHOOSESUGGESTS_CB:
            if ((vv = va_arg(ap, void*)))
                ctx->choose_suggests_fn = vv;

            if ((vv = va_arg(ap, void*)))
                ctx->data_choose_suggests_fn = vv;
            break;

        case POLDEK_CONF_GOODBYE_CB:
            vv = va_arg(ap, void*);
            if (vv) {
                poldek_say_goodbye = vv;
                poldek_log_say_goodbye = vv;
            }
            /* fallthru */ /* FIXME */
        default:
            rc = poldek_ts_vconfigure(ctx->ts, param, ap);
            break;
    }

    va_end(ap);
    return rc;
}

static void poldek_vf_vlog_cb(int pri, const char *fmt, va_list ap)
{
    int logpri = 0;

    if (pri & VFILE_LOG_INFO)
        logpri |= LOGINFO;

    if (pri & VFILE_LOG_WARN)
        logpri |= LOGWARN;

    else if (pri & VFILE_LOG_ERR)
        logpri |= LOGERR;

    if (pri & VFILE_LOG_TTY)
        logpri |= LOGTTY;

    else {
        //snprintf(logfmt, "vf: %s", fmt);
        //fmt = logfmt;
    }

    poldek_vlog(logpri, 0, fmt, ap);
}

void poldeklib_destroy(void)
{
    if (poldeklib_init_called == 0)
        return;
    poldeklib_init_called = 0;

    n_array_free(default_op_map);
    default_op_map = NULL;

    poldek_log_reset_appenders();
}

int poldeklib_init(void)
{
    if (poldeklib_init_called)
        return 1;

    poldeklib_init_called = 1;

    pmmodule_init();
    poldek_set_verbose(0);
    verify_setuid();

    default_op_map = build_default_op_map();
    n_assert(default_op_map);

    bindtextdomain(PACKAGE, NULL);
    textdomain(PACKAGE);

    poldek_term_init();
    init_internal();
    pkgdirmodule_init();

    vfile_configure(VFILE_CONF_SIGINT_REACHED, sigint_reached_reset);
    vfile_configure(VFILE_CONF_VERBOSE, &poldek_VERBOSE);
    vfile_configure(VFILE_CONF_LOGCB, poldek_vf_vlog_cb);

    if (poldek__is_in_testing_mode()) {
        const char *path = getenv("POLDEK_TESTING_PRELOAD_CONF");
        if (path && !preload_conf(path))
            n_die("%s: not preloaded", path);
    }

    /* add libdir/poldek to PATH */
#ifdef PKGLIBDIR
    {
        char *path, buf[PATH_MAX];
        if ((path = getenv("PATH")) == NULL)
            path = "/bin:/usr/bin:/usr/local/bin";

        n_snprintf(buf, sizeof(buf), "%s:%s", path, PKGLIBDIR);
# ifdef HAVE_SETENV
        setenv("PATH", buf, 1);
# else
        {
            int len = strlen("PATH") + strlen(path) + 3;
            char *tmp = n_malloc(len);
            n_snprintf(tmp, len, "%s=%s", PATH, path);
            putenv(tmp);
        }
# endif  /* HAVE_SETENV */
    }
#endif  /* PKGLIBDIR */
    return 1;
}


static int poldek_init(struct poldek_ctx *ctx, unsigned flags)
{
    struct poldek_ts *ts;
    int i;

    flags = flags;

    memset(ctx, 0, sizeof(*ctx));
    poldek_log_set_default_appender("_TTY", NULL, NULL);
    poldek__setup_default_ask_callbacks(ctx);

    ctx->sources = n_array_new(4, (tn_fn_free)source_free,
                               (tn_fn_cmp)source_cmp);

    ctx->dest_sources = n_array_new(4, (tn_fn_free)source_free,
                                    (tn_fn_cmp)source_cmp);
    ctx->ps = NULL;
    ctx->_cnf = n_hash_new(16, free);
    n_hash_insert(ctx->_cnf, "pm", n_strdup("rpm")); /* default pm */

    ctx->pmctx = NULL;

    ctx->ts = poldek_ts_new(NULL, 0);
    ts = ctx->ts;

    /* set default option values without "touching" them in ts */
    for (i=0; i < n_array_size(default_op_map); i++) {
        struct default_op_map_ent *ent = n_array_nth(default_op_map, i);
        int v = ent->_defaultv;

        if (v == 2) {      /* "auto" */
            switch (ent->op) {
                case POLDEK_OP_USESUDO:
                    v = 0;
                    if (getuid() != 0)
                        v = 1;
                    break;

                case POLDEK_OP_MULTILIB: /* do not touch, will
                                            be determined later */
                    break;

                case POLDEK_OP_AUTODIRDEP:
                    v = 0;
#ifdef HAVE_RPMDSUNAME          /* rpmdsUname - rpm 4.4.6  */
                    v = 1;
#endif
                    break;

                default:
                    logn(LOGERR, "unhandled %s", ent->name);
                    n_assert(0);
            }
            DBGF("auto %s  = %d\n",  ent->name,  v);
            ent->defaultv = v;
        }

        if (v != 2)
            poldek_ts_xsetop(ts, ent->op, v, 0);
    }
    DBGF("default_op_map size = %d\n", n_array_size(default_op_map));
    return 1;
}

struct poldek_ctx *poldek_new(unsigned flags)
{
    struct poldek_ctx *ctx;

    if (!poldeklib_init_called) {
        logn(LOGERR | LOGDIE, "poldeklib_init() call is a must...");
        return 0;
    }

    ctx = n_malloc(sizeof(*ctx));
    if (poldek_init(ctx, flags))
        return ctx;

    free(ctx);
    return NULL;
}

struct poldek_ctx *poldek_link(struct poldek_ctx *ctx)
{
    ctx->_refcnt++;
    return ctx;
}


int poldek_setup_cachedir(struct poldek_ctx *ctx)
{
    if (ctx->_iflags & CACHEDIR_SETUPDONE)
        return 1;

    if (!do_poldek_setup_cachedir(ctx))
        return 0;

    ctx->_iflags |= CACHEDIR_SETUPDONE;
    return 1;
}

static
int do_poldek_setup_cachedir(struct poldek_ctx *ctx)
{
    char *path;

    path = util__setup_cachedir(ctx->ts->cachedir);

    if (poldek_VERBOSE > 0 && poldek__is_in_testing_mode()) {
        if (ctx->ts->cachedir && path && n_str_eq(ctx->ts->cachedir, path))
            msgn(1, "cachedir: %s", path);
        else
            msgn(1, "cachedir: %s -> %s", ctx->ts->cachedir, path);
    }

    if (path == NULL) {
        n_cfree(&ctx->ts->cachedir);
        return 0;
    }

    n_assert(path);

    if (poldek_VERBOSE > 1) {
        if (ctx->ts->cachedir == NULL) /* not configured */
            msgn(2, "Setting temporary cache directory path to %s", path);
        else
            msgn(2, "Setting cache directory path to %s", path);
    }
    n_cfree(&ctx->ts->cachedir);
    ctx->ts->cachedir = path;
    vfile_configure(VFILE_CONF_CACHEDIR, path);

    return 1;
}


static
int setup_sources(struct poldek_ctx *ctx)
{
    int i, autoupa = 0;
    tn_hash *htcnf = NULL;

    if (ctx->_iflags & SOURCES_SETUPDONE)
        return 1;

    if (!prepare_sources(ctx, ctx->htconf, ctx->sources))
        return 0;

    if (n_array_size(ctx->dest_sources) > 0) {
        if (!prepare_sources(ctx, ctx->htconf, ctx->dest_sources))
            return 0;
    }

    if (ctx->htconf) {
        htcnf = poldek_conf_get_section(ctx->htconf, "global");
        autoupa = poldek_conf_get_bool(htcnf, "autoupa", 1);
    }

    for (i=0; i < n_array_size(ctx->sources); i++) {
        struct source *src = n_array_nth(ctx->sources, i);
        if (autoupa)
            src->flags |= PKGSOURCE_AUTOUPA;

        source_set_defaults(src);
    }

    ctx->_iflags |= SOURCES_SETUPDONE;
    return 1;
}

static void __setup_multilib(struct poldek_ctx *ctx)
{
    struct default_op_map_ent *ent, tmp;
    char     scolor[64];
    int      color;
    int      multilib = 2; /* 'auto' */


    if (!pm_conf_get(ctx->pmctx, "%{_transaction_color}", scolor, sizeof(scolor)))
        return;

    if (sscanf(scolor, "%u", &color) != 1)
        return;

    if (ctx->htconf) {
        tn_hash *htcnf = poldek_conf_get_section(ctx->htconf, "global");
        multilib = poldek_conf_get_bool3(htcnf, "multilib", 2);
    }

    if (color && multilib == 0) {
        logn(LOGWARN, _("RPM works in multilib mode, while poldek not"));
        return;

    } else if (!color && multilib == 1) {
        logn(LOGWARN, _("poldek works in multilib mode, while rpm not"));
        return;
    }

    if (multilib == 2) {        /* auto */
        ctx->_rpm_tscolor = color;

        tmp.name = "multilib";
        ent = n_array_bsearch(default_op_map, &tmp);
        n_assert(ent);

        ent->defaultv = 0;
        if (color)
            ent->defaultv = 1;

        if (color)
            msgn(2, "Enabling multilib mode, transaction color = %d", color);

        DBGF("multilib(auto) = %d\n", ent->defaultv);
        poldek_ts_xsetop(ctx->ts, ent->op, ent->defaultv, 0);
    }
}


/*  */
static int setup_pm(struct poldek_ctx *ctx)
{
    const char *pm = n_hash_get(ctx->_cnf, "pm");
    n_assert(pm);


    if (strcmp(pm, "rpm") == 0) {
        ctx->pmctx = pm_new(pm);
        pm_configure(ctx->pmctx, "macros", ctx->ts->rpmacros);
        __setup_multilib(ctx);

    } else if (strcmp(pm, "pset") == 0) {
        if (poldek__is_in_testing_mode()) /* need rpm_machine_score for testing */
            pm_new("rpm");

        n_array_sort_ex(ctx->dest_sources, (tn_fn_cmp)source_cmp_no);
        if (n_array_size(ctx->dest_sources) == 0) {
            logn(LOGERR, "%s: missing destination source", pm);

        } else {
            int i;

            ctx->pmctx = pm_new(pm);

            for (i=0; i < n_array_size(ctx->dest_sources); i++) {
                struct source *dest = n_array_nth(ctx->dest_sources, i);
                if (source_is_remote(dest) && 0) {
                    logn(LOGERR, "%s: destination source could not be remote",
                         source_idstr(dest));
                    continue;
                }
                pm_configure(ctx->pmctx, "source", dest);
            }

            if (ctx->ts->getop(ctx->ts, POLDEK_OP_AUTODIRDEP))
                pm_configure(ctx->pmctx, "autodirdep", NULL);

        }

        if (ctx->pmctx) {
            if (poldek_VERBOSE > 1)
                logn(LOGNOTICE, "Depends on your destination repository you may need "
                     "to disable conflicts and/or obsoletes processing");
        }

    } else {
        logn(LOGERR, "%s: unknown PM type", pm);
        return 0;
    }

    if (ctx->pmctx == NULL) {
        logn(LOGERR, "%s: PM setup failed", pm);
        return 0;
    }

    if (ctx->htconf) {
        const char *op;
        tn_hash *htcnf;

        htcnf = poldek_conf_get_section(ctx->htconf, "global");
        if ((op = poldek_conf_get(htcnf, "pm command", NULL))) {
            if (strchr(op, ' ') == NULL) { /* with options? */
                pm_configure(ctx->pmctx, "pmcmd", (void*)op);

            } else {
                const char **tl, **tl_save;
                tl = tl_save = n_str_tokl(op, " \t");
                pm_configure(ctx->pmctx, "pmcmd", (void*)*tl);
                tl++;
                while (*tl) {
                    poldek_ts_configure(ctx->ts, POLDEK_CONF_RPMOPTS, *tl);
                    tl++;
                }
                n_str_tokl_free(tl_save);
            }
        }

        if ((op = poldek_conf_get(htcnf, "sudo command", NULL)))
            pm_configure(ctx->pmctx, "sudocmd", (void*)op);
    }

    return ctx->pmctx != NULL;
}


int poldek_setup(struct poldek_ctx *ctx)
{
    int rc = 1;

    if ((ctx->_iflags & SETUP_DONE) == SETUP_DONE)
        return 1;

    if (!poldek_setup_cachedir(ctx))
        return 0;

    rc = setup_sources(ctx);

    if (rc && !setup_pm(ctx))
        rc = 0;

    vfile_setup();

    ctx->_depsolver = 3; /* XXX: should be extracted from conf_sections.c */
    if (ctx->htconf) {
        tn_hash *htcnf = poldek_conf_get_section(ctx->htconf, "global");
        ctx->_depsolver = poldek_conf_get_int(htcnf, "dependency solver",
                                              ctx->_depsolver);
    }

    ctx->_iflags |= SETUP_DONE;
    return rc;
}


int poldek_is_sources_loaded(struct poldek_ctx *ctx)
{
    return ctx->_iflags & SOURCES_LOADED;
}

int poldek_load_sources(struct poldek_ctx *ctx)
{
    int rc;

    check_if_setup_done(ctx);

    if (ctx->_iflags & SOURCES_LOADED) {
        return 1;
    }

    rc = poldek__load_sources_internal(ctx, ctx->_ps_setup_flags);
    ctx->_iflags |= SOURCES_LOADED;
    return rc;
}

/* if lazy deps processing */
int poldek_setup_pkgset_deps(struct poldek_ctx *ctx) {
    return pkgset_setup_deps(ctx->ps, 0);
}

struct pkgdir *poldek_load_destination_pkgdir(struct poldek_ctx *ctx,
                                              unsigned ldflags)
{
    return pkgdb_to_pkgdir(ctx->pmctx, ctx->ts->rootdir, NULL, ldflags, NULL);
}

int poldek_is_interactive_on(const struct poldek_ctx *ctx)
{
    return ctx->ts->getop_v(ctx->ts, POLDEK_OP_CONFIRM_INST,
                            POLDEK_OP_CONFIRM_UNINST,
                            POLDEK_OP_EQPKG_ASKUSER, 0);
}

struct pm_ctx *poldek_get_pmctx(struct poldek_ctx *ctx)
{
    return ctx->pmctx;
}

tn_hash *poldek_get_config(struct poldek_ctx *ctx)
{
    return ctx->htconf;
}


const char *poldek_version(void)
{
    return VERSION;
}

/* for autoconf */
void PACKAGE_VERSION_FUNCNAME (void)
{}


/* for autoconf */
void PACKAGE_SERIES_FUNCNAME (void)
{}
