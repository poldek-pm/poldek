/*
  Copyright (C) 2000 - 2002 Pawel A. Gajda <mis@pld.org.pl>

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

#include <trurl/nmalloc.h>
#include <trurl/nassert.h>
#include <trurl/n_snprintf.h>
#include <trurl/nstr.h>

#include "vfile/vfile.h"
#include "sigint/sigint.h"

#include "pkgdir/source.h"
#include "pkgset.h"
#include "conf.h"
#include "log.h"
#include "misc.h"
#include "i18n.h"
#include "poldek.h"
#include "poldek_term.h"

/* _iflags */
#define POLDEKCTX_SETUP_DONE      (1 << 0)
#define POLDEKCTX_SOURCES_LOADED  (1 << 1)

const char poldek_BUG_MAILADDR[] = "<mis@pld.org.pl>";
const char poldek_VERSION_BANNER[] = PACKAGE " " VERSION " (" VERSION_STATUS ")";
const char poldek_BANNER[] = PACKAGE " " VERSION " (" VERSION_STATUS ")\n"
"Copyright (C) 2000-2003 Pawel A. Gajda <mis@pld.org.pl>\n"
"This program may be freely redistributed under the terms of the GNU GPL v2";

static const char *poldek_logprefix = "poldek";

void (*poldek_assert_hook)(const char *expr, const char *file, int line) = NULL;
void (*poldek_malloc_fault_hook)(void) = NULL;


//static FILE        *log_stream = stdout, *log_fstream = NULL;
static tn_hash     *poldek_cnf = NULL;          /* config file values */

static void register_vf_handlers_compat(const tn_hash *htcnf);
static void register_vf_handlers(const tn_array *fetchers);
static int get_conf_sources(tn_array *sources, tn_array *srcs_named,
                            tn_hash *htcnf, tn_array *htcnf_sources);


static inline void ctx_vrfy_is_setup_done(struct poldek_ctx *ctx) 
{
    if (ctx->_iflags & POLDEKCTX_SETUP_DONE)
        return;

    logn(LOGERR | LOGDIE, "poldek_setup() wasn't called...");
}


int poldek_selected_sources(struct poldek_ctx *ctx, unsigned srcflags_excl)
{
    int i, nsources = 0;
    
    n_array_sort(ctx->sources);
    n_array_uniq(ctx->sources);
        
    for (i=0; i < n_array_size(ctx->sources); i++) {
        struct source *src = n_array_nth(ctx->sources, i);

        if ((src->flags & srcflags_excl) == 0)
            nsources++;
    }

    return nsources;
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
int prepare_sources(tn_array *sources)
{
    struct source   *src;
    int             i, rc = 1;
    tn_array        *srcs_path, *srcs_named;

    
    sources_score(sources);
    
    srcs_path = n_array_clone(sources);
    srcs_named = n_array_clone(sources);
    
    for (i=0; i < n_array_size(sources); i++) {
        src = n_array_nth(sources, i);
        if (src->flags & PKGSOURCE_NAMED) /* supplied by -n */
            n_array_push(srcs_named, source_link(src));
        else
            n_array_push(srcs_path, source_link(src));
    }

    if (poldek_cnf && (n_array_size(srcs_named) > 0 ||
                       n_array_size(sources) == 0)) {
        
        tn_hash *htcnf;
        tn_array *htcnf_sources;

        htcnf = poldek_conf_get_section_ht(poldek_cnf, "global");
        htcnf_sources = poldek_conf_get_section_arr(poldek_cnf, "source");
            
        rc = get_conf_sources(srcs_path, srcs_named, htcnf, htcnf_sources);
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
struct source *source_new_htcnf(const tn_hash *htcnf, int no) 
{
    char spec[PATH_MAX], name[20];
    int  n = 0;
    int  v;
    char *vs;
    
    
    vs = poldek_conf_get(htcnf, "name", NULL);
    if (vs == NULL) {
        n_snprintf(name, sizeof(name), "src%.2d", no);
        vs = name;
    }
    
    n += n_snprintf(&spec[n], sizeof(spec) - n, "%s", vs);

    if ((vs = poldek_conf_get(htcnf, "type", NULL)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",type=%s", vs);

    if ((v = poldek_conf_get_int(htcnf, "pri", 0)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",pri=%d", v);
    
    if ((v = poldek_conf_get_bool(htcnf, "noauto", 0)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",noauto");

    if ((v = poldek_conf_get_bool(htcnf, "noautoup", 0)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",noautoup");

    if ((v = poldek_conf_get_bool(htcnf, "signed", 0)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",sign");
    
    else if ((v = poldek_conf_get_bool(htcnf, "sign", 0)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",sign");

    if ((vs = poldek_conf_get(htcnf, "dscr", NULL)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",dscr=%s", vs);

    vs = poldek_conf_get(htcnf, "path", NULL);
    if (vs == NULL)
        vs = poldek_conf_get(htcnf, "url", NULL);
    //printf("spec %d = %s\n", n_hash_size(htcnf), spec);
    n_assert(vs);

    n_snprintf(&spec[n], sizeof(spec) - n, " %s", vs);
    
    vs = poldek_conf_get(htcnf, "prefix", NULL);
    
    return source_new(NULL, spec, vs);
}
    
static 
int get_conf_sources(tn_array *sources, tn_array *srcs_named,
                     tn_hash *htcnf, tn_array *htcnf_sources)
{
    struct source   *src;
    int             i, nerr, getall = 0;
    int             *matches = NULL;
    tn_array        *list;

    if (n_array_size(srcs_named) == 0 && n_array_size(sources) == 0)
        getall = 1;
    
    else if (n_array_size(srcs_named) > 0) {
        matches = alloca(n_array_size(srcs_named) * sizeof(int));
        memset(matches, 0, n_array_size(srcs_named) * sizeof(int));
    }

    if ((list = poldek_conf_get_multi(htcnf, "source"))) {
        for (i=0; i < n_array_size(list); i++) {
            src = source_new(NULL, n_array_nth(list, i), NULL);
            if (!addsource(sources, src, getall, srcs_named, matches))
                source_free(src);
        }
        n_array_free(list);
    }
    
    /* source\d+, prefix\d+ pairs  */
    for (i=0; i < 100; i++) {
        char opt[64], *src_val;
        
        snprintf(opt, sizeof(opt), "source%d", i);
        if ((src_val = poldek_conf_get(htcnf, opt, NULL))) {
            snprintf(opt, sizeof(opt), "prefix%d", i);
            src = source_new(NULL, src_val, poldek_conf_get(htcnf, opt, NULL));
            
            if (!addsource(sources, src, getall, srcs_named, matches))
                source_free(src);
        }
    }

    if (htcnf_sources) {
        for (i=0; i < n_array_size(htcnf_sources); i++) {
            tn_hash *ht = n_array_nth(htcnf_sources, i);
            
            src = source_new_htcnf(ht, n_array_size(sources));
            if (!addsource(sources, src, getall, srcs_named, matches))
                source_free(src);
        }
    }

    nerr = 0;
    for (i=0; i < n_array_size(srcs_named); i++) {
        if (matches[i] == 0) {
            struct source *src = n_array_nth(srcs_named, i);
            logn(LOGERR, _("%s: no such source"), src->name);
            nerr++;
        }
    }

    if (nerr == 0 && getall)
        for (i=0; i < n_array_size(sources); i++) {
            struct source *src = n_array_nth(sources, i);
            src->no = i;
        }


    return nerr == 0;
}


static
void get_conf_opt_list(const tn_hash *htcnf, const char *name,
                       tn_array *tolist)
{
    tn_array *list;
    int i;
    
    if ((list = poldek_conf_get_multi(htcnf, name))) {
        for (i=0; i < n_array_size(list); i++)
            n_array_push(tolist, n_strdup(n_array_nth(list, i)));
        
        n_array_free(list);
    }
    
    n_array_sort(tolist);
    n_array_uniq(tolist);
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
    char name[128], *v;
    tn_array *protocols;

    protocols = n_array_new(2, NULL, (tn_fn_cmp)strcmp);
    
    if ((v = poldek_conf_get(htcnf, "ftp_http_get", NULL))) {
        extract_handler_name(name, sizeof(name), v);
        n_array_clean(protocols);
        n_array_push(protocols, "ftp");
        n_array_push(protocols, "http");
        //vfile_cnflags |= VFILE_USEXT_FTP | VFILE_USEXT_HTTP;
        vfile_register_ext_handler(name, protocols, v);
    }
    
    if ((v = poldek_conf_get(htcnf, "ftp_get", NULL))) {
        //vfile_cnflags |= VFILE_USEXT_FTP;
        extract_handler_name(name, sizeof(name), v);
        n_array_clean(protocols);
        n_array_push(protocols, "ftp");
        vfile_register_ext_handler(name, protocols, v);
    }
    
    if ((v = poldek_conf_get(htcnf, "http_get", NULL))) {
        //vfile_cnflags |= VFILE_USEXT_HTTP;
        extract_handler_name(name, sizeof(name), v);
        n_array_clean(protocols);
        n_array_push(protocols, "http");
        vfile_register_ext_handler(name, protocols, v);
    }
    
    if ((v = poldek_conf_get(htcnf, "https_get", NULL))) {
        //vfile_cnflags |= VFILE_USEXT_HTTPS;
        extract_handler_name(name, sizeof(name), v);
        n_array_clean(protocols);
        n_array_push(protocols, "https");
        vfile_register_ext_handler(name, protocols, v);
    }
        
    if ((v = poldek_conf_get(htcnf, "rsync_get", NULL))) {
        extract_handler_name(name, sizeof(name), v);
        n_array_clean(protocols);
        n_array_push(protocols, "rsync");
        vfile_register_ext_handler(name, protocols, v);
    }
    
    if ((v = poldek_conf_get(htcnf, "cdrom_get", NULL))) {
        extract_handler_name(name, sizeof(name), v);
        n_array_clean(protocols);
        n_array_push(protocols, "cdrom");
        vfile_register_ext_handler(name, protocols, v);
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
        char     *nam, *cmd;
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
        //printf("conf %d (%s) -> (%s)\n", tag, *tl, name);
        tl++;
    }
    
    n_str_tokl_free(tl_save);
    return 1;
}


int poldek_load_config(struct poldek_ctx *ctx, const char *path)
{
    tn_hash   *htcnf = NULL;
    int       rc = 1;
    char      *v;
    tn_array  *list;


    if (path != NULL)
        poldek_cnf = poldek_ldconf(path, 0);
    else 
        poldek_cnf = poldek_ldconf_default();
    
    if (poldek_cnf == NULL)
        return 0;

    htcnf = poldek_conf_get_section_ht(poldek_cnf, "global");

    
    if (poldek_conf_get_bool(htcnf, "use_sudo", 0))
        ctx->ts->flags |= POLDEK_TS_USESUDO;

    if (poldek_conf_get_bool(htcnf, "keep_downloads", 0))
        ctx->ts->flags |= POLDEK_TS_KEEP_DOWNLOADS;
    
    if (poldek_conf_get_bool(htcnf, "confirm_installation", 0))
        ctx->ts->flags |= POLDEK_TS_CONFIRM_INST;
    
    /* backward compat */
    if (poldek_conf_get_bool(htcnf, "confirm_installs", 0))
        ctx->ts->flags |= POLDEK_TS_CONFIRM_INST;

    if (poldek_conf_get_bool(htcnf, "confirm_removal", 1))
        ctx->ts->flags |= POLDEK_TS_CONFIRM_UNINST;

    if (poldek_conf_get_bool(htcnf, "choose_equivalents_manually", 0))
        ctx->ts->flags |= POLDEK_TS_EQPKG_ASKUSER;

    else if ((ctx->ts->flags & POLDEK_TS_CONFIRM_INST) && verbose < 1)
        verbose = 1;

    if (poldek_conf_get_bool(htcnf, "particle_install", 1))
        ctx->ts->flags |= POLDEK_TS_PARTICLE;

    if (ctx->ts->flags & POLDEK_TS_FOLLOW) { /* no --nofollow specified */
        if (!poldek_conf_get_bool(htcnf, "follow", 1))
            ctx->ts->flags &= ~POLDEK_TS_FOLLOW;
    }

    if (ctx->ts->flags & POLDEK_TS_GREEDY) 
        ctx->ts->flags |= POLDEK_TS_FOLLOW;
        
    else if (poldek_conf_get_bool(htcnf, "greedy", 0))
        ctx->ts->flags |= POLDEK_TS_GREEDY | POLDEK_TS_FOLLOW;
    

    if ((ctx->ts->flags & (POLDEK_TS_GREEDY | POLDEK_TS_FOLLOW)) == POLDEK_TS_GREEDY) {
        logn(LOGWARN, _("greedy and follow options are inclusive"));
        ctx->ts->flags |= POLDEK_TS_FOLLOW;
        rc = 0;
    }
        

    if (poldek_conf_get_bool(htcnf, "mercy", 0))
        ctx->ps_flags |= PSVERIFY_MERCY;

    if ((ctx->ps_setup_flags & PSET_DO_UNIQ_PKGNAME) == 0)
        if (poldek_conf_get_bool(htcnf, "unique_package_names", 0))
            ctx->ps_setup_flags |= PSET_DO_UNIQ_PKGNAME;
    
    register_vf_handlers_compat(htcnf);
    register_vf_handlers(poldek_conf_get_section_arr(poldek_cnf, "fetcher"));

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

    if ((v = poldek_conf_get(htcnf, "cachedir", NULL)))
        ctx->ts->cachedir = v;
    
    if ((poldek_conf_get_bool(htcnf, "ftp_sysuser_as_anon_passwd", 0)))
        vfile_configure(VFILE_CONF_SYSUSER_AS_ANONPASSWD, 1);
    
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


static void init_modules(void)
{
    pkgflmodule_init();
    pkgsetmodule_init();
}

static void destroy_modules(void)
{
    pkgsetmodule_destroy();
    pkgflmodule_destroy();
}


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

    n_assert_sethook(poldek_assert_hook);
    n_malloc_set_failhook(poldek_malloc_fault_hook);
    init_modules();
    
}


void poldek_destroy(struct poldek_ctx *ctx) 
{
    destroy_modules();
    ctx = ctx;
    if (poldek_cnf)
        n_hash_free(poldek_cnf);
    sigint_destroy();
}

void poldek_reinit(void)
{
    destroy_modules();
    init_modules();
}


int poldek_configure(struct poldek_ctx *ctx, int param, ...) 
{
    va_list ap;
    int rc;
    unsigned uv;
    void     *vv;
    

    va_start(ap, param);
    
    switch (param) {
        case POLDEK_CONF_PSFLAGS:
            uv = va_arg(ap, unsigned);
            if (uv) {
                ctx->ps_flags |= uv;
            }
            break;

        case POLDEK_CONF_PS_SETUP_FLAGS:
            uv = va_arg(ap, unsigned);
            if (uv) {
                ctx->ps_setup_flags |= uv;
            }
            break;


        case POLDEK_CONF_SOURCE:
            vv = va_arg(ap, void*);
            if (vv) {
                struct source *src = (struct source*)vv;
                if (src->path)
                    src->path = poldek_i_conf_path(src->path, NULL);
                sources_add(ctx->sources, src);
            }
            break;

        case POLDEK_CONF_LOGFILE:
            vv = va_arg(ap, void*);
            if (vv)
                log_init(vv, stdout, poldek_logprefix);
            break;

        case POLDEK_CONF_LOGTTY:
            vv = va_arg(ap, void*);
            if (vv)
                log_init(vv, stdout, poldek_logprefix);
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


int poldek_init(struct poldek_ctx *ctx, unsigned flags)
{

    flags = flags;

    memset(ctx, 0, sizeof(*ctx));
    ctx->sources = n_array_new(4, (tn_fn_free)source_free, (tn_fn_cmp)source_cmp);
    ctx->ps = NULL;
    ctx->ts = poldek_ts_new(NULL);
    
    ctx->dbpkgdir = NULL;
    ctx->ts_instpkgs = 0;

    mem_info_verbose = -1;
    verbose = 0;
    
    log_init(NULL, stdout, poldek_logprefix);
    self_init();

    setlocale(LC_MESSAGES, "");
    setlocale(LC_CTYPE, "");
    bindtextdomain(PACKAGE, NULL);
    textdomain(PACKAGE);

    term_init();
    init_internal();
    pkgdirmodule_init();
    rpm_initlib(NULL);

    vfile_verbose = &verbose;

    //DUPA
    //vfile_msg_fn = log_msg;
    //vfile_msgtty_fn = log_tty;
    //vfile_err_fn = log_err;
    vfile_configure(VFILE_CONF_LOGCB, poldek_vf_vlog_cb);
    vfile_configure(VFILE_CONF_CACHEDIR, setup_cachedir(NULL));

    return 1;
}

int poldek_setup(struct poldek_ctx *ctx)
{
    char *path = NULL;
    
    path = setup_cachedir(ctx->ts->cachedir);
    free(ctx->ts->cachedir);
    ctx->ts->cachedir = path;
    vfile_configure(VFILE_CONF_CACHEDIR, path);

    if (!prepare_sources(ctx->sources))
        return 0;

    if (!mklock(ctx->ts->cachedir))
        return 0;

    return 1;
}


extern int poldek_load_sources__internal(struct poldek_ctx *ctx, int load_dbdepdirs);


int poldek_load_sources(struct poldek_ctx *ctx)
{
    int rc;
    
    if (ctx->_iflags & POLDEKCTX_SOURCES_LOADED)
        return 1;
    
    destroy_modules();
    init_modules();
    rc = poldek_load_sources__internal(ctx, 1);
    ctx->_iflags |= POLDEKCTX_SOURCES_LOADED;
    return rc;
}
