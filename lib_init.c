/*
  Copyright (C) 2000 - 2005 Pawel A. Gajda <mis@k2.net.pl>

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

#include <stdio.h>
#include <fnmatch.h>
#include <sys/param.h>          /* for PATH_MAX */

#include <trurl/nmalloc.h>
#include <trurl/nassert.h>
#include <trurl/n_snprintf.h>
#include <trurl/nstr.h>

#include "vfile/vfile.h"
#include "sigint/sigint.h"

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

static int poldeklib_init_called = 0;

/* _iflags */
#define SOURCES_SETUPDONE   (1 << 1)
#define CACHEDIR_SETUPDONE  (1 << 2)
#define SOURCES_LOADED      (1 << 3)
#define SETUP_DONE          (1 << 4)  


const char poldek_BUG_MAILADDR[] = "<mis@pld.org.pl>";
const char poldek_VERSION_BANNER[] = PACKAGE " " VERSION " (" VERSION_STATUS ")";
const char poldek_BANNER[] = PACKAGE " " VERSION " (" VERSION_STATUS ")\n"
"Copyright (C) 2000-2004 Pawel A. Gajda <mis@pld.org.pl>\n"
"This program may be freely redistributed under the terms of the GNU GPL v2";

static const char *poldek_logprefix = "poldek";

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

extern
int poldek_load_sources__internal(struct poldek_ctx *ctx, int load_dbdepdirs);


static struct {
    const char *name;
    int op;
    int defaultv;
} default_op_map[] = {
    { "use_sudo",             POLDEK_OP_USESUDO, 0        },
    { "confirm_installation", POLDEK_OP_CONFIRM_INST, 0   },
    { "confirm_removal",      POLDEK_OP_CONFIRM_UNINST, 1 },
    { "keep_downloads",       POLDEK_OP_KEEP_DOWNLOADS, 0 },
    { "choose_equivalents_manually", POLDEK_OP_EQPKG_ASKUSER, 0 },
    { "particle_install",     POLDEK_OP_PARTICLE, 1  },
    { "follow",               POLDEK_OP_FOLLOW, 1    },
    { "obsoletes",            POLDEK_OP_OBSOLETES, 1 },
    { "conflicts",            POLDEK_OP_CONFLICTS, 1 },
    { "mercy",                POLDEK_OP_VRFYMERCY, 1 },
    { "greedy",               POLDEK_OP_GREEDY, 1    },
    { "allow_duplicates",     POLDEK_OP_ALLOWDUPS, 1 },
    { "unique_package_names", POLDEK_OP_UNIQN, 0  },
    { "promoteepoch", POLDEK_OP_PROMOTEPOCH, 0  },
    { NULL, POLDEK_OP_HOLD,   1  },
    { NULL, POLDEK_OP_IGNORE, 1  }, 
    { NULL, 0, 0 }
};

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
        htcnf_sources = poldek_conf_get_section_arr(poldek_cnf, "source");
        rc = get_conf_sources(ctx, srcs_path, srcs_named, htcnf_sources);
    }
    
    
    n_array_free(srcs_named);
    n_array_clean(sources);

    for (i=0; i < n_array_size(srcs_path); i++) {
        struct source *src = n_array_nth(srcs_path, i);
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
            const char *name, *type;
            tn_hash *ht;

            ht = n_array_nth(htcnf_sources, j);

            if (isgroup_matches[j]) /* just for efficiency */
                continue;
            
            type = poldek_conf_get(ht, "type", NULL);
            /* skip not "group" */
            if (type == NULL || n_str_ne(type, source_TYPE_GROUP)) { 
                isgroup_matches[j] = 1;
                continue;
            }

            name = poldek_conf_get(ht, "name", NULL);
            n_assert(name);

            if (htcnf_matches[j] == 0 && fnmatch(s->name, name, 0) == 0) {
                tn_array *names;
                int ii;
                
                names = poldek_conf_get_multi(ht, "sources");
                n_assert(names);
                
                for (ii=0; ii < n_array_size(names); ii++) {
                    DBGF("%s -> %s\n", s->name, n_array_nth(names, ii));
                    struct source *src = source_new(n_array_nth(names, ii), NULL, NULL, NULL);
                    src->no = s->no + 1 + ii; /* XXX: hope we fit (see sources_add()) */
                    n_array_push(sources, src);
                }
                n_hash_replace(expanded_h, s->name, NULL);
                htcnf_matches[j] = 1;
            }
        }
        
        n_array_push(sources, source_link(s));
    }
#if ENABLE_TRACE
    for (i=0; i < n_array_size(sources); i++) {
        struct source *s = n_array_nth(sources, i);
        DBG_F("%d %s\n", i, s->name);
    }
#endif    
    return sources;
}

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
    
    if (src->flags & PKGSOURCE_NOAUTOUP)
        poldek_conf_add_to_section(sect, "autoup", "no");
    
    if (src->flags & PKGSOURCE_VRFY_SIGN)
        poldek_conf_add_to_section(sect, "signed", "yes");

    if (src->flags & PKGSOURCE_COMPRESS)
        poldek_conf_add_to_section(sect, "compress", "yes");

    if (src->dscr)
        poldek_conf_add_to_section(sect, "lang", src->dscr);

    return 1;
}

static int setup_legacy_sources(tn_hash *poldek_cnf) 
{
    struct source *src;
    tn_array *list;
    tn_hash *htcnf;
    int i, no = 0;
    
    htcnf = poldek_conf_get_section_ht(poldek_cnf, "global");
    
    if ((list = poldek_conf_get_multi(htcnf, "source"))) {
        for (i=0; i < n_array_size(list); i++) {
            src = source_new_v0_18(n_array_nth(list, i), NULL);
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
            src = source_new_v0_18(src_val, poldek_conf_get(htcnf, opt, NULL));
            source_to_htconf(src, no++, poldek_cnf);
            source_free(src);
        }
    }
    
    return no;
}

static 
int get_conf_sources(struct poldek_ctx *ctx, tn_array *sources, tn_array *srcs_named,
                     tn_array *htcnf_sources)
{
    struct source   *src;
    int             i, nerr = 0, getall = 0;
    int             *matches = NULL;
    tn_array        *expanded_srcs_named = NULL;
    tn_hash         *expanded_h  = NULL;

    if (n_array_size(srcs_named) == 0 && n_array_size(sources) == 0)
        getall = 1;
    
    else if (n_array_size(srcs_named) > 0) {
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
        struct source *src = n_array_nth(srcs_named, i);
        if (matches[i] == 0 && !n_hash_exists(expanded_h, src->name)) {
            logn(LOGERR, _("%s: no such source"), src->name);
            nerr++;
        }
    }

    if (nerr == 0 && getall)
        for (i=0; i < n_array_size(sources); i++) {
            struct source *src = n_array_nth(sources, i);
            src->no = i;
        }

    n_array_cfree(&expanded_srcs_named);
    if (expanded_h)
        n_hash_free(expanded_h);
    return nerr == 0;
}


static
int get_conf_opt_list(const tn_hash *htcnf, const char *name,
                      tn_array *tolist)
{
    tn_array *list;
    int i = 0;

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


    htcnf = poldek_conf_get_section_ht(ctx->htconf, "global");
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


void poldek__apply_tsconfig(struct poldek_ctx *ctx, struct poldek_ts *ts)
{
    tn_hash           *htcnf = NULL;
    int               i;
    
    if (ctx->htconf == NULL)
        return;
    
    htcnf = poldek_conf_get_section_ht(ctx->htconf, "global");
    i = 0;
    DBGF("ts %p, tsctx %p\n", ts, ctx->ts);
    while (default_op_map[i].name) {
        int op = default_op_map[i].op;
        
        if (!poldek_ts_op_touched(ts, op)) { /* not modified by cmdl opts */
            int v = poldek_conf_get_bool(htcnf,
                                         default_op_map[i].name,
                                         default_op_map[i].defaultv);
            
            DBGF("ldconfig %s(%d) = %d\n", default_op_map[i].name, op, v);
            ts->setop(ts, op, v);
        }

        if (ts != ctx->ts &&    /* child, non internal ts */
            ts->getop(ts, op) != ctx->ts->getop(ctx->ts, op)) {
            if (poldek_ts_op_touched(ts, op)) {
                DBGF("NOT apply %s(%d) = %d\n", default_op_map[i].name,
                     op, ts->getop(ts, op));
                goto l_continue_loop;
            }
            
            
            DBGF("apply %s(%d) = %d\n", default_op_map[i].name,
                 op, ctx->ts->getop(ctx->ts, op));
            ts->setop(ts, op, ctx->ts->getop(ctx->ts, op));
        }
    l_continue_loop:        
        DBGF("ts   %s(%d) = %d\n", default_op_map[i].name, op, ts->getop(ts, op));
        if (ctx->ts != ts)
            DBGF(" ctx %s(%d) = %d\n", default_op_map[i].name, op,
                 ctx->ts->getop(ctx->ts, op));

        i++;
    }
    
    if (ts->getop(ts, POLDEK_OP_CONFIRM_INST) && poldek_VERBOSE < 1)
        poldek_VERBOSE = 1;
    
    if (ts->getop(ts, POLDEK_OP_GREEDY)) {
        int v = poldek_conf_get_bool(htcnf, "aggressive greedy", 1);
        ts->setop(ts, POLDEK_OP_AGGREEDY, v);
        ts->setop(ts, POLDEK_OP_FOLLOW, 1);
    }
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

    do_poldek_setup_cachedir(ctx);
    n_assert(ctx->htconf == NULL);
        
    if ((flags & POLDEK_LOADCONF_NOCONF) == 0) {
        unsigned ldflags = 0;
        
        if (flags & POLDEK_LOADCONF_UPCONF)
            ldflags |= POLDEK_LDCONF_UPDATE;
                
        if (path != NULL)
            ctx->htconf = poldek_conf_load(path, ldflags);
        else 
            ctx->htconf = poldek_conf_loadefault(ldflags);
    }

    if (addon_cnflines)
        ctx->htconf = poldek_conf_addlines(ctx->htconf, "global",
                                           addon_cnflines);
    
    if (ctx->htconf == NULL)
        return 0;
    
    poldek__apply_tsconfig(ctx, ctx->ts);

    htcnf = poldek_conf_get_section_ht(ctx->htconf, "global");
    register_vf_handlers_compat(htcnf);
    register_vf_handlers(poldek_conf_get_section_arr(ctx->htconf, "fetcher"));

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

    if (poldek_conf_get_bool(htcnf, "vfile_external_compress", 0))
        vfile_configure(VFILE_CONF_EXTCOMPR, 1);

    else if (poldek_conf_get_bool(htcnf, "auto_zlib_in_rpm", 0))
        zlib_in_rpm(ctx);

    if ((v = poldek_conf_get_int(htcnf, "vfile_retries", 1000)) > 0)
        vfile_configure(VFILE_CONF_STUBBORN_NRETRIES, v);

    return 1;
}


static void n_malloc_fault(void) 
{
    printf("Something wrong, something not quite right...\n"
           "Memory exhausted\n");
    exit(EXIT_FAILURE);
}


static void n_assert_hook(const char *expr, const char *file, int line) 
{
    printf("Something wrong, something not quite right.\n"
           "Assertion '%s' failed, %s:%d\n"
           "Please report this bug to %s.\n\n",
           expr, file, line, poldek_BUG_MAILADDR);
    abort();
}

static void n_die_hook(const char *msg) 
{
    printf("Something wrong, something not quite right.\ndie: %s\n", msg);
    abort();
}


static
void self_init(void) 
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

static
void init_internal(void) 
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
    if (ctx->htconf)
        n_hash_free(ctx->htconf);
    sigint_destroy();

    n_array_free(ctx->sources);
    
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


int poldek_configure(struct poldek_ctx *ctx, int param, ...) 
{
    va_list ap;
    void    *vv;
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

        case POLDEK_CONF_LOGFILE:
            vv = va_arg(ap, void*);
            if (vv)
                poldek_log_init(vv, stdout, poldek_logprefix);
            break;

        case POLDEK_CONF_LOGTTY:
            vv = va_arg(ap, void*);
            if (vv)
                poldek_log_init(vv, stdout, poldek_logprefix);
            break;


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
    
    vlog(logpri, 0, fmt, ap);
}

void poldeklib_destroy(void)
{
}

int poldeklib_init(void)
{
    char *path;
    
    if (poldeklib_init_called)
        return 1;
    poldeklib_init_called = 1;
    
    pmmodule_init();
    poldek_set_verbose(0);
    poldek_log_init(NULL, stdout, poldek_logprefix);
    self_init();

    bindtextdomain(PACKAGE, NULL);
    textdomain(PACKAGE);

    poldek_term_init();
    init_internal();
    pkgdirmodule_init();
    
    vfile_configure(VFILE_CONF_SIGINT_REACHED, sigint_reached_reset);
    vfile_configure(VFILE_CONF_VERBOSE, &poldek_VERBOSE);
    vfile_configure(VFILE_CONF_LOGCB, poldek_vf_vlog_cb);

    /* Kind of egg and chicken problem with cachedir;
       on start set it to $TMPDIR. */

    path = setup_cachedir(NULL);
    n_assert(path);
    vfile_configure(VFILE_CONF_CACHEDIR, path);
    free(path);

#ifdef PKGLIBDIR
    {
        char *path, buf[PATH_MAX];
        if ((path = getenv("PATH")) == NULL)
            path = "/bin:/usr/bin:/usr/local/bin";
        
        n_snprintf(buf, sizeof(buf), "%s:%s", path, PKGLIBDIR);
#ifdef HAVE_SETENV        
        setenv("PATH", buf, 1);
#else
        {
            int len = strlen("PATH") + strlen(path) + 3;
            char *tmp = n_malloc(len);
            n_snprintf(tmp, len, "%s=%s", PATH, path);
            putenv(tmp);
        }
#endif
    }
#endif  /* PKGLIBDIR */
    return 1;
}


static
int poldek_init(struct poldek_ctx *ctx, unsigned flags)
{
    struct poldek_ts *ts;
    int i;
    
    flags = flags;

    memset(ctx, 0, sizeof(*ctx));
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
    
    i = 0;
    while (default_op_map[i].op) {
        poldek_ts_xsetop(ts, default_op_map[i].op,
                         default_op_map[i].defaultv, 0);
        i++;
    }

    if (getuid() != 0)
        poldek_ts_setop(ts, POLDEK_OP_USESUDO, 1);
    
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
    do_poldek_setup_cachedir(ctx);
    
    ctx->_iflags |= CACHEDIR_SETUPDONE;
    return 1;
}

static
int do_poldek_setup_cachedir(struct poldek_ctx *ctx)
{
    char *path;
    
    path = setup_cachedir(ctx->ts->cachedir);
    DBGF("%s -> %s\n", ctx->ts->cachedir, path);
    n_assert(path);
    if (ctx->ts->cachedir)
        free(ctx->ts->cachedir);
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
        htcnf = poldek_conf_get_section_ht(ctx->htconf, "global");
        autoupa = poldek_conf_get_bool(htcnf, "autoupa", 1);
    }
    
    for (i=0; i < n_array_size(ctx->sources); i++) {
        struct source *src = n_array_nth(ctx->sources, i);
        if (autoupa)
            src->flags |= PKGSOURCE_AUTOUPA;
        source_set_default_type(src);
    }
    
    ctx->_iflags |= SOURCES_SETUPDONE;
    return 1;
}


/*  */
static int setup_pm(struct poldek_ctx *ctx) 
{
    const char *pm = n_hash_get(ctx->_cnf, "pm");
    n_assert(pm);


    if (strcmp(pm, "rpm") == 0) {
        ctx->pmctx = pm_new(pm);
        pm_configure(ctx->pmctx, "macros", ctx->ts->rpmacros);
                         
    } else if (strcmp(pm, "pset") == 0) {
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
        }

        if (ctx->pmctx) {
            ctx->ts->setop(ctx->ts, POLDEK_OP_CONFLICTS, 0);
            ctx->ts->setop(ctx->ts, POLDEK_OP_OBSOLETES, 0);
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

        htcnf = poldek_conf_get_section_ht(ctx->htconf, "global");
        if ((op = poldek_conf_get(htcnf, "pm command", NULL)))
            pm_configure(ctx->pmctx, "pmcmd", (void*)op);

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
    
    poldek_setup_cachedir(ctx);
    
    rc = setup_sources(ctx);
    
    if (rc && !setup_pm(ctx))
        rc = 0;

    vfile_setup();
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
    
    if (ctx->_iflags & SOURCES_LOADED)
        return 1;
    
    rc = poldek_load_sources__internal(ctx, 1);
    ctx->_iflags |= SOURCES_LOADED;
    return rc;
}

struct pkgdir *poldek_load_destination_pkgdir(struct poldek_ctx *ctx)
{
    return pkgdb_to_pkgdir(ctx->pmctx, ctx->ts->rootdir, NULL, NULL);
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



