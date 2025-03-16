#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/stat.h>
#include <sys/types.h>

#include <trurl/nmalloc.h>
#include <trurl/nstr.h>
#include <trurl/n_snprintf.h>

#include "conf.h"
#include "misc.h"
#include "log.h"
#include "i18n.h"
#include "pkg.h"
#include "pkgmisc.h"
#include "poldek_intern.h"
#include "poldek_term.h"
#include "pkgset.h"
#include "poldek_ts.h"

static int summary_VERBOSE_LEVEL = 1;

/* install summary saved to ts to propagate it to high level api  */
void poldek__ts_update_summary(struct poldek_ts *ts,
                               const char *prefix, const tn_array *pkgs,
                               unsigned pmsflags, const struct pkgmark_set *pms)
{
    tn_array *supkgs;
    int i;

    n_assert(pkgs);
    if (n_array_size(pkgs) == 0)
        return;

    if (pms == NULL)
        n_assert(pmsflags == 0);

    if ((supkgs = n_hash_get(ts->ts_summary, prefix)) == NULL)
        supkgs = pkgs_array_new(n_array_size(pkgs));

    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        if (pmsflags && pms && !pkgmark_isset(pms, pkg, pmsflags))
            continue;

        n_array_push(supkgs, pkg_link(pkg));
    }

    if (n_array_size(supkgs) == 0) {
        n_assert(!n_hash_exists(ts->ts_summary, prefix));
        n_array_free(supkgs);

    } else {
        n_hash_insert(ts->ts_summary, prefix, supkgs);
        n_array_sort(supkgs);
    }

}

tn_array *poldek_ts_get_summary(const struct poldek_ts *ts, const char *mark)
{
    tn_array *pkgs;
    n_assert(mark != NULL);
    pkgs = n_hash_get(ts->ts_summary, mark);

    if (pkgs != NULL)
        return n_ref(pkgs);
    return pkgs;
}

struct color_str {
    char *str;                  /* string with color codes */
    int len;                    /* length without color codes */
    char _buf[];
};

/* for scripts */
static
void do_display_parseable_summary(const char *prefix, const tn_array *strpkgs)
{
    for (int i=0; i < n_array_size(strpkgs); i++) {
        const struct color_str *s = n_array_nth(strpkgs, i);
        msgn(summary_VERBOSE_LEVEL, "%%%s %s", prefix, s->str);
    }
}

/* pre 0.4.0 summary */
static
void do_display_summary(const char *prefix, int prefix_color, const tn_array *strpkgs)
{
    int npkgs = n_array_size(strpkgs);
    int prefix_printed = 0;
    tn_buf *nbuf = n_buf_new(512);
    int prefix_len = strlen(prefix);
    char cprefix[64];
    const char *colon = "  ";

    int term_width = poldek_term_get_width() - 5;
    int ncol = prefix_len + 1;

    if (prefix_color) {
        poldek_term_snprintf_c(PRCOLOR_GREEN, cprefix, sizeof(cprefix), "%s", prefix);
        prefix = cprefix;
    }

    for (int i=0; i < n_array_size(strpkgs); i++) {
        const struct color_str *s = n_array_nth(strpkgs, i);

        if (prefix_printed == 0) {
            n_buf_printf(nbuf, "%s ", prefix);
            prefix_printed = 1;
        }

        if (ncol + s->len >= term_width) {
            msgn(summary_VERBOSE_LEVEL, "%s", (char*)n_buf_ptr(nbuf));

            n_buf_clean(nbuf);
            ncol = prefix_len + 1;
            n_buf_printf(nbuf, "%s ", prefix);
        }

        if (--npkgs == 0)
            colon = "";

        n_buf_printf(nbuf, "%s%s", s->str, colon);
        ncol += s->len + strlen(colon);
    }

    if (prefix_printed)
        n_buf_printf(nbuf, "\n");

    if (n_buf_size(nbuf) > 0)
        msg(summary_VERBOSE_LEVEL, "%s", (char*)n_buf_ptr(nbuf));

    n_buf_free(nbuf);
}

/* pre 0.4.0 summary */
static
void display_summary(const char *prefix, int prefix_color, const tn_array *pkgs, int parseable)
{
    n_assert(pkgs);
    n_assert(n_array_size(pkgs) > 0);

    tn_array *strpkgs = n_array_new(n_array_size(pkgs), free, NULL);
    for (int i=0; i < n_array_size(pkgs); i++) {
        struct color_str *s = n_malloc(sizeof(*s));
        s->str = (char*)pkg_id(n_array_nth(pkgs, i));
        s->len = strlen(s->str);
        n_array_push(strpkgs, s);
    }

    if (parseable) {
        do_display_parseable_summary(prefix, strpkgs);
    } else {
        do_display_summary(prefix, prefix_color, strpkgs);
    }

    n_array_free(strpkgs);
}

static
int print_pair(char *line, size_t size,
               struct pkg *pkg, struct pkg *old_pkg)
{
    int eq_ver = n_str_eq(pkg->ver, old_pkg->ver);
    int eq_rel = n_str_eq(pkg->rel, old_pkg->rel);
    int eq_arch = n_str_eq(pkg_arch(pkg), pkg_arch(old_pkg));

    char arch[256];
    int n = 0;

    int new_color = PRCOLOR_GREEN;
    int old_color = PRCOLOR_RED;

    if (eq_arch) {
        snprintf(arch, sizeof(arch), "%s", pkg_arch(pkg));
    } else {
        char old[64], new[64];
        poldek_term_snprintf_c(old_color, old, sizeof(old), "%s", pkg_arch(old_pkg));
        poldek_term_snprintf_c(new_color, new, sizeof(new), "%s", pkg_arch(pkg));
        snprintf(arch, sizeof(arch), "(%s => %s)", old, new);
    }

    if (eq_ver && eq_rel) {     /* reinstallation */
        n = n_snprintf(line, size, "%s", pkg_id(pkg));

    } else if (!eq_ver && eq_rel) {
        char old[64], new[64];
        poldek_term_snprintf_c(old_color, old, sizeof(old), "%s", old_pkg->ver);
        poldek_term_snprintf_c(new_color, new, sizeof(new), "%s", pkg->ver);

        n = n_snprintf(line, size, "%s-(%s => %s)-%s.%s",
                   pkg->name, old, new, pkg->rel, arch);

    } else if (eq_ver && !eq_rel) {
        char old[64], new[64];
        poldek_term_snprintf_c(old_color, old, sizeof(old), "%s", old_pkg->rel);
        poldek_term_snprintf_c(new_color, new, sizeof(new), "%s", pkg->rel);

        n = n_snprintf(line, size, "%s-%s-(%s => %s).%s",
                   pkg->name, pkg->ver, old, new, arch);

    } else if (!eq_ver && !eq_rel) {
        char old[64], new[64];
        poldek_term_snprintf_c(old_color, old, sizeof(old), "%s-%s", old_pkg->ver, old_pkg->rel);
        poldek_term_snprintf_c(new_color, new, sizeof(new), "%s-%s", pkg->ver, pkg->rel);

        n = n_snprintf(line, size, "%s-(%s => %s).%s",
                   pkg->name, old, new, arch);
    }

    return n;
}

static int pkg_cmp_name_arch(const struct pkg *p1, const struct pkg *p2)
{
    int rc = pkg_cmp_name(p1, p2);
    if (rc == 0)
        rc = strcmp(pkg_arch(p1), pkg_arch(p2)); /* need lexical order */

    return rc;
}

static
void colored_install_summary(const tn_array *ipkgs, const tn_array *idepkgs, const tn_array *rmpkgs)
{
    tn_array *upgs = n_array_new(n_array_size(ipkgs), free, NULL);
    tn_array *news = n_array_clone(ipkgs);
    tn_array *pkgs = n_array_dup(ipkgs, (tn_fn_dup)pkg_link);

    if (idepkgs)
        pkgs = n_array_concat_ex(pkgs, idepkgs, (tn_fn_dup)pkg_link);

    tn_array *rems = NULL;
    if (rmpkgs) {
        rems = n_array_dup(rmpkgs, (tn_fn_dup)pkg_link);
        n_array_sort_ex(rems, (tn_fn_cmp)pkg_cmp_name_arch);
    }

    n_array_sort_ex(pkgs, (tn_fn_cmp)pkg_cmp_name_arch);

    for (int i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        struct pkg *old_pkg = NULL;

        if (rems) {
            old_pkg = n_array_bsearch_ex(rems, pkg, (tn_fn_cmp)pkg_cmp_name_arch);
            if (old_pkg == NULL) {
                old_pkg = n_array_bsearch_ex(rems, pkg, (tn_fn_cmp)pkg_cmp_name);
            }
        }

        if (old_pkg) {
            char line[PATH_MAX];
            print_pair(line, sizeof(line), pkg, old_pkg);

            struct color_str *s = n_malloc(sizeof(*s) + strlen(line) + 1);
            n_strncpy(s->_buf, line, strlen(line) + 1);
            s->str = &s->_buf[0];
            s->len = strlen(pkg_id(pkg)) + strlen(old_pkg->ver) + strlen(old_pkg->rel) + strlen(".( => )");

            n_array_push(upgs, s);
            n_array_remove(rems, old_pkg);
        } else {
            n_array_push(news, pkg_link(pkg));
        }
    }

    if (n_array_size(upgs) > 0) {
        do_display_summary("U", PRCOLOR_GREEN, upgs);
    }
    n_array_free(upgs);

    if (n_array_size(news) > 0) {
        display_summary("A", PRCOLOR_GREEN, news, 0);
    }
    n_array_free(news);

    if (rems && n_array_size(rems) > 0) {
        display_summary("R", PRCOLOR_RED, rems, 0);
    }

    if (rems)
        n_array_free(rems);

    n_array_free(pkgs);
}

static
void colored_uninstall_summary(const tn_array *ipkgs, const tn_array *idepkgs)
{
    display_summary("R", PRCOLOR_RED, ipkgs, 0);

    if (idepkgs && n_array_size(idepkgs) > 0) {
        display_summary("D", PRCOLOR_RED, idepkgs, 0);
    }
}

static
void colored_summary(struct poldek_ts *ts, const tn_array *ipkgs,
                     const tn_array *idepkgs, const tn_array *rmpkgs)
{
    if (poldek_VERBOSE < summary_VERBOSE_LEVEL)
        return;

    if (ts->type != POLDEK_TS_UNINSTALL) {
        colored_install_summary(ipkgs, idepkgs, rmpkgs);
    } else {
        colored_uninstall_summary(ipkgs, idepkgs);
    }
}

void poldek__ts_display_summary(struct poldek_ts *ts)
{
    int ninst = 0, ndep = 0, nrm = 0, npkgs = 0, parseable = 0;
    long int sinsts = 0, sdeps = 0, srems = 0, sdiff = 0;
    tn_array *ipkgs, *idepkgs, *rmpkgs, *pkgs;
    char ms[1024], *to, *prefix;
    int i, n;

    ipkgs = n_hash_get(ts->ts_summary, "I");
    idepkgs = n_hash_get(ts->ts_summary, "D");
    rmpkgs = n_hash_get(ts->ts_summary, "R");

    ninst = ipkgs ? n_array_size(ipkgs) : 0;
    ndep  = idepkgs ? n_array_size(idepkgs) : 0;
    nrm   = rmpkgs ? n_array_size(rmpkgs) : 0;

    if (ipkgs) {
	for (i=0; i < ninst; i++) {
	    struct pkg *pkg = n_array_nth(ipkgs, i);
	    sinsts += pkg->size;
	}
    }

    if (idepkgs) {
	for (i=0; i < ndep; i++) {
	    struct pkg *pkg = n_array_nth(idepkgs, i);
	    sdeps += pkg->size;
	}
    }

    if (rmpkgs) {
	for (i=0; i < nrm; i++) {
	    struct pkg *pkg = n_array_nth(rmpkgs, i);
	    srems += pkg->size;
	}
    }

    if (ts->type != POLDEK_TS_UNINSTALL) {
        to = _("to install");
        prefix = "I";
        pkgs = ipkgs;
        npkgs = ninst + ndep;
        sdiff = sinsts + sdeps - srems;
    } else {
        to = _("to remove");
        prefix = "R";
        pkgs = rmpkgs;
        npkgs = nrm + ndep;
        sdiff = - srems - sdeps;
        nrm = 0;
    }
    n_assert(pkgs);
    n_assert(npkgs);

#ifndef ENABLE_NLS
    n = n_snprintf(ms, sizeof(ms),
                   "There are %d package%s %s", npkgs, npkgs > 1 ? "s":"", to);
    if (ndep)
        n += n_snprintf(&ms[n], sizeof(ms) - n,
                        " (%d marked by dependencies)", ndep);

#else
    n = n_snprintf(ms, sizeof(ms),
                   ngettext("There are %d package %s",
                            "There are %d packages %s", npkgs), npkgs, to);

    if (ndep)
        n += n_snprintf(&ms[n], sizeof(ms),
                        ngettext(" (%d marked by dependencies)",
                                 " (%d marked by dependencies)", ndep), ndep);
#endif
    if (nrm)
        n += n_snprintf(&ms[n], sizeof(ms) - n, _(", %d to remove"), nrm);

    n_snprintf(&ms[n], sizeof(ms) - n,  ":");
    msgn(1, "%s", ms);

    parseable = ts->getop(ts, POLDEK_OP_PARSABLETS);

    tn_hash *global = poldek_conf_get_section(ts->ctx->htconf, "global");
    const char *style = poldek_conf_get(global, "summary style", NULL);
    int coloured = (style == NULL || n_str_eq(style, "color"));

    if (!parseable && coloured) {
        colored_summary(ts, pkgs, idepkgs, rmpkgs);
    } else {
        if (npkgs)
            display_summary(prefix, 0, pkgs, parseable);

        if (idepkgs && ndep)
            display_summary("D", 0, idepkgs, parseable);

        if (ts->type != POLDEK_TS_UNINSTALL) {
            if (rmpkgs)
                display_summary("R", 0, rmpkgs, parseable);
        }
    }

    if (sdiff != 0) {
        char size[64];
        snprintf_size(size, sizeof(size), labs(sdiff), 1, 1);

        if (sdiff > 0)
           msgn(1, _("This operation will use %s of disk space."), size);
        else
           msgn(1, _("This operation will free %s of disk space."), size);
    }
}
