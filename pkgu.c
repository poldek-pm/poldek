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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <trurl/trurl.h>
#include <trurl/nstream.h>
#include <trurl/nhash.h>

#include <iconv.h>
#include <langinfo.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "pkgu.h"
#include "misc.h"
#ifdef HAVE_RPMORG
# include "pm/rpmorg/pm_rpm.h"
#else
# include "pm/rpm/pm_rpm.h"
#endif

#define NA_OWNED             (1 << 0)
#define RECODE_SUMMMARY      (1 << 1) /* needs to be recoded */
#define RECODE_DESCRIPTION   (1 << 2)
#define SUMMARY_RECODED      (1 << 3) /* already recoded     */
#define DESCRITPION_RECODED  (1 << 4)

struct pkguinf {
    char              *license;
    char              *url;
    char              *_summary;
    char              *_description;
    const char        *_encoding; /* for iconv, NFY */
    char              *vendor;
    char              *buildhost;
    char              *distro;
    char              *legacy_sourcerpm;
    char              *changelog;

    tn_hash           *_ht;
    tn_array          *_langs;
    tn_array          *_langs_rpmhdr; /* v018x legacy: for preserving
                                         the langs order */
    tn_alloc          *_na;
    int16_t           _refcnt;
    uint16_t          _flags;
};

struct pkguinf_i18n {
    char              *summary;
    char              *description;
    char              _buf[0];
};


static
struct pkguinf_i18n *pkguinf_i18n_new(tn_alloc *na, const char *summary,
                                      const char *description)
{
    int s_len, d_len;
    struct pkguinf_i18n *inf;

    if (summary == NULL)
        summary = "";

    if (description == NULL)
        description = "";

    s_len = strlen(summary) + 1;
    d_len = strlen(description) + 1;
    inf = na->na_malloc(na, sizeof(*inf) + s_len + d_len);

    memcpy(inf->_buf, summary, s_len);
    memcpy(&inf->_buf[s_len], description, d_len);
    inf->summary = inf->_buf;
    inf->description = &inf->_buf[s_len];
    return inf;
}

struct pkguinf *pkguinf_new(tn_alloc *na)
{
    struct pkguinf *pkgu;
    tn_alloc *_na = NULL;

    if (na == NULL)
        na = _na = n_alloc_new(8, TN_ALLOC_OBSTACK);


    pkgu = na->na_malloc(na, sizeof(*pkgu));
    memset(pkgu, 0, sizeof(*pkgu));
    pkgu->_na = na;
    if (_na)
        pkgu->_flags |= NA_OWNED;

    pkgu->license = NULL;
    pkgu->url = NULL;
    pkgu->_summary = NULL;
    pkgu->_description = NULL;
    pkgu->vendor = NULL;
    pkgu->buildhost = NULL;
    pkgu->legacy_sourcerpm = NULL;

    pkgu->_ht = NULL;
    pkgu->_langs = NULL;
    pkgu->_refcnt = 0;

    return pkgu;
}

void pkguinf_free(struct pkguinf *pkgu)
{
    if (pkgu->_refcnt > 0) {
        pkgu->_refcnt--;
        return;
    }

    if (pkgu->_summary) {
        if (pkgu->_flags & SUMMARY_RECODED)
            free(pkgu->_summary);
        pkgu->_summary = NULL;
    }

    if (pkgu->_description) {
        if (pkgu->_flags & DESCRITPION_RECODED)
            free(pkgu->_description);
        pkgu->_description = NULL;
    }

    if (pkgu->_langs)
        n_array_free(pkgu->_langs);

    if (pkgu->_langs_rpmhdr)
        n_array_free(pkgu->_langs_rpmhdr);

    if (pkgu->_ht)
        n_hash_free(pkgu->_ht);

    pkgu->_langs_rpmhdr = NULL;
    pkgu->_langs = NULL;
    pkgu->_ht = NULL;

    if (pkgu->_flags & NA_OWNED)
        n_alloc_free(pkgu->_na);
}

/*
  Set recodable pkgu member, set _flags accordinggly to trigger
  recoding in pkguinf_get()
 */
static void pkgu_set_recodable(struct pkguinf *pkgu, int tag, char *val,
                               const char *lang)
{
    char **member = NULL;
    unsigned doneflag = 0, needflag = 0;
    char *usrencoding = NULL;

    switch (tag) {
        case PKGUINF_SUMMARY:
            member = &pkgu->_summary;
            doneflag = SUMMARY_RECODED;
            needflag = RECODE_SUMMMARY;
            break;

        case PKGUINF_DESCRIPTION:
            member = &pkgu->_description;
            doneflag = DESCRITPION_RECODED;
            needflag = RECODE_SUMMMARY;
            break;

        default:
            n_assert(0);
            break;
    }

    if (*member && (pkgu->_flags & doneflag)) {
        free((char*)*member);
        *member = NULL;
    }

    *member = val;
    pkgu->_flags &= ~needflag;

    if (strstr(lang, "UTF-8") == NULL) {
        *member = val;
        return;
    }

    usrencoding = nl_langinfo(CODESET);
    DBGF("CODE %s\n", usrencoding);

    if (usrencoding && n_str_ne(usrencoding, "UTF-8"))
        pkgu->_flags |= needflag;
}


static char *recode(const char *val, const char *valencoding)
{
    char *p, *val_utf8, *usrencoding, *tmpval;
    size_t vlen, u_vlen, tmpvlen;
    iconv_t cd;

    usrencoding = nl_langinfo(CODESET);
    if (usrencoding == NULL)
        return (char*)val;

    valencoding = "UTF-8";   /* XXX, support for others needed? */

    vlen = u_vlen = tmpvlen = strlen(val);
    p = val_utf8 = n_malloc(u_vlen + 1);

    /* FIXME: iconv leave val content untouched */
    tmpval = (char*) val;

    cd = iconv_open(usrencoding, valencoding);
    if (iconv(cd, &tmpval, &tmpvlen, &p, &u_vlen) == (size_t)-1) {
        iconv_close(cd);
        free(val_utf8);
        return (char*)val;
    }
    iconv_close(cd);
    n_assert(p <= val_utf8 + vlen);
    *p = '\0';
    return val_utf8;
}

struct pkguinf *pkguinf_link(struct pkguinf *pkgu)
{
    pkgu->_refcnt++;
    return pkgu;
}

static inline
void set_member(struct pkguinf *pkgu, char **m, const char *v, int len)
{
    char *mm;

    mm = pkgu->_na->na_malloc(pkgu->_na, len + 1);
    memcpy(mm, v, len + 1);
    *m = mm;
}

static char *na_strdup(tn_alloc *na, const char *v, int len)
{
    char *mm;

    mm = na->na_malloc(na, len + 1);
    memcpy(mm, v, len + 1);
    return mm;
}


static char *cp_tag(tn_alloc *na, Header h, int rpmtag)
{
    struct rpmhdr_ent  hdrent;
    char *t = NULL;

    if (pm_rpmhdr_ent_get(&hdrent, h, rpmtag)) {
        char *s = pm_rpmhdr_ent_as_str(&hdrent);
        int len = strlen(s) + 1;
        t = na->na_malloc(na, len + 1);
        memcpy(t, s, len);
    }

    pm_rpmhdr_ent_free(&hdrent);
    return t;
}


struct changelog_ent {
    time_t   ts;
    char     *info;             /* rev,  author */
    char     message[0];
};

int changelog_ent_cmp(struct changelog_ent *e1, struct changelog_ent *e2)
{
    return e1->ts - e2->ts;
}

static time_t parse_datetime(const char *str)
{
    struct tm tm;
    time_t ts = 0;
    int n;
    char c;

    n = sscanf(str, "%d%c%d%c%d %d:%d:%d", &tm.tm_year, &c, &tm.tm_mon, &c,
               &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    if (n != 8)
        return 0;

    tm.tm_mon -= 1;
    tm.tm_year -= 1900;

    if ((ts = mktime(&tm)) == -1)
        ts = 0;

    return ts;
}

/* remove header; add '*'s; replace "Revision REV" with "rREV" */
static
char *prepare_pld_changelog(tn_alloc *na, const char *changelog)
{
    struct changelog_ent  *ent = NULL;
    char                  *entmark = "Revision", *prepared_log;
    int                   i, started, max_MESSAGE = 1024, len;
    tn_array              *entries, *lines;
    tn_buf                *logmsg;

    lines = n_str_etokl_ext(changelog, "\n", "", "", '\\');


    len = strlen(entmark);
    entries = n_array_new(8, NULL, (tn_fn_cmp)changelog_ent_cmp);
    logmsg = n_buf_new(1024);
    started = 0;

    for (i = 0; i < n_array_size(lines); i++) {
        char *line = n_array_nth(lines, i);

        if (strncmp(line, entmark, len) == 0)
             started = 1;

        if (!started)
            continue;

        if (strncmp(line, entmark, len) == 0) {
            char *tstr, *rev, info[80];
            time_t ts;
            int n;

            if (ent != NULL) {
                n_snprintf(ent->message, max_MESSAGE, "%s", n_buf_ptr(logmsg));
                n_buf_clean(logmsg);
                n_array_push(entries, ent);
                ent = NULL;
            }

            //Revision REV YYYY-MM-DD HH:MM:SS rest
            rev = line + len + 1;       /* skip Revision */
            while (*rev && isspace(*rev))
                rev++;

            tstr = strchr(rev, ' ');
            if (tstr) {
                *tstr = '\0';
                tstr++;
                while (*tstr && isspace(*tstr))
                    tstr++;
            }

            if (rev && tstr) {
                ts = parse_datetime(tstr);
                if (ts == 0)
                    continue;
            }

            n = n_snprintf(info, sizeof(info), "* r%s %s", rev, tstr);

            ent = n_malloc(sizeof(*ent) + max_MESSAGE);
            memset(ent, 0, sizeof(*ent));
            ent->info = n_strdupl(info, n);
            ent->message[0] = '\0';
            continue;
        }

        if (ent)
            n_buf_printf(logmsg, "%s\n", line);
    }

    if (ent != NULL) {
        n_snprintf(ent->message, max_MESSAGE, "%s", n_buf_ptr(logmsg));
        n_array_push(entries, ent);
    }

    n_array_free(lines);
    n_buf_clean(logmsg);

    /* shift && free entries cause ents are simply malloc()ed here */
    while (n_array_size(entries)) {
        ent = n_array_shift(entries);
        n_buf_printf(logmsg, "%s\n", ent->info);
        n_buf_printf(logmsg, "%s\n", ent->message);
    }
    n_array_free(entries);

    prepared_log = na_strdup(na, n_buf_ptr(logmsg), n_buf_size(logmsg));
    n_buf_free(logmsg);

    return prepared_log;
}

static tn_array *parse_changelog(tn_alloc *na, tn_array *lines)
{
    struct changelog_ent  *ent = NULL;
    int                   i, max_MESSAGE = 1024;
    tn_array              *entries;
    tn_buf                *logmsg;

    entries = n_array_new(8, NULL, (tn_fn_cmp)changelog_ent_cmp);
    logmsg = n_buf_new(1024);

    for (i = 0; i < n_array_size(lines); i++) {
        char *line = n_array_nth(lines, i);

        if (*line == '*') {
            char *ts;

            if (ent != NULL) {
                n_snprintf(ent->message, max_MESSAGE, "%s", n_buf_ptr(logmsg));
                n_buf_clean(logmsg);
                n_array_push(entries, ent);
                ent = NULL;
            }

            ent = na->na_malloc(na, sizeof(*ent) + max_MESSAGE);
            memset(ent, 0, sizeof(*ent));
            ent->info = na_strdup(na, line, strlen(line));
            ent->message[0] = '\0';

            //* [rREV] YYYY-MM-DD HH:MM:SS rest
            ts = strchr(line, ' ');
            while (*ts && isspace(*ts))
                ts++;

            if (ts && *ts == 'r')
                ts = strchr(ts, ' ');

            if (ts) {
                while (*ts && isspace(*ts))
                    ts++;

                if (ts)
                    ent->ts = parse_datetime(ts);
            }
            continue;
        }
        if (ent)
            n_buf_printf(logmsg, "%s\n", line);
    }

    if (ent != NULL) {
        n_snprintf(ent->message, max_MESSAGE, "%s", n_buf_ptr(logmsg));
        n_array_push(entries, ent);
    }
    n_buf_free(logmsg);

    return entries;
}

static tn_array *get_parsed_changelog(struct pkguinf *inf, time_t since)
{
    tn_array *lines, *entries;
    const char *changelog;

    if ((changelog = pkguinf_get(inf, PKGUINF_CHANGELOG)) == NULL)
        return NULL;

    lines = n_str_etokl_ext(changelog, "\n", "", "", '\\');
    entries = parse_changelog(inf->_na, lines);
    n_array_free(lines);

    if (since) {
        tn_array *tmp = n_array_clone(entries);
        int i;

        for (i=0; i<n_array_size(entries); i++) {
            struct changelog_ent *ent = n_array_nth(entries, i);
            if (ent->ts > since)
                n_array_push(tmp, ent);
        }
        n_array_free(entries);
        entries = tmp;
    }

    return entries;
}

int pkguinf_changelog_with_security_fixes(struct pkguinf *inf, time_t since)
{
    tn_array *entries;
    int yes = 0, i;

    n_assert(since);
    if ((entries = get_parsed_changelog(inf, since)) == NULL)
        return 0;

    for (i=0; i < n_array_size(entries); i++) {
        struct changelog_ent *ent = n_array_nth(entries, i);
        const char *m = ent->message;

        if (strstr(m, "CVE-20") || strstr(m, "CVE-19") || strcasestr(m, "security")) {
            yes = 1;
            break;
        }
    }
    n_array_free(entries);
    return yes;
}

const char *pkguinf_get_changelog(struct pkguinf *inf, time_t since)
{
    tn_array *entries;
    tn_buf   *nbuf;
    char     *changelog = NULL;
    int      i;

    if (!since)
        return pkguinf_get(inf, PKGUINF_CHANGELOG);

    if ((entries = get_parsed_changelog(inf, since)) == NULL)
        return pkguinf_get(inf, PKGUINF_CHANGELOG);

    nbuf = n_buf_new(1024 * 4);
    for (i=0; i < n_array_size(entries); i++) {
        struct changelog_ent *ent = n_array_nth(entries, i);
        n_buf_printf(nbuf, "%s\n", ent->info);
        n_buf_printf(nbuf, "%s\n", ent->message);
    }
    n_array_free(entries);

    changelog = na_strdup(inf->_na, n_buf_ptr(nbuf), n_buf_size(nbuf));
    n_buf_free(nbuf);
    return changelog;
}

static char *do_load_changelog_from_rpmhdr(tn_alloc *na, void *hdr)
{
    struct rpmhdr_ent    e_name, e_time, e_text;
    char                 **names = NULL, **texts = NULL, *changelog = NULL;
    uint32_t             *times = NULL;
    tn_buf               *nbuf = NULL;
    int                  i;

    if (!pm_rpmhdr_ent_get(&e_name, hdr, RPMTAG_CHANGELOGNAME))
        return NULL;

    if (!pm_rpmhdr_ent_get(&e_time, hdr, RPMTAG_CHANGELOGTIME)) {
        pm_rpmhdr_ent_free(&e_name);
        return NULL;
    }

    if (!pm_rpmhdr_ent_get(&e_text, hdr, RPMTAG_CHANGELOGTEXT)) {
        pm_rpmhdr_ent_free(&e_name);
        pm_rpmhdr_ent_free(&e_time);
        return NULL;
    }

    names = pm_rpmhdr_ent_as_strarr(&e_name);
    times = pm_rpmhdr_ent_as_intarr(&e_time);
    texts = pm_rpmhdr_ent_as_strarr(&e_text);

    if (e_name.cnt == 1 && strstr(names[0], "PLD")) {
        changelog = prepare_pld_changelog(na, texts[0]);
        goto l_end;
    }

    nbuf = n_buf_new(1024);
    for (i=0; i < e_name.cnt; i++) {
        char ts[32];
        time_t t = times[i];

        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", gmtime((time_t*)&t));

        n_buf_printf(nbuf, "* %s %s\n", ts, names[i]);
        n_buf_printf(nbuf, "%s\n\n", texts[i]);
    }

    changelog = na_strdup(na, n_buf_ptr(nbuf), n_buf_size(nbuf));
    n_buf_free(nbuf);

 l_end:
    pm_rpmhdr_ent_free(&e_name);
    pm_rpmhdr_ent_free(&e_time);
    pm_rpmhdr_ent_free(&e_text);

    return changelog;
}

static
char *load_changelog_from_rpmhdr(tn_alloc *na, void *hdr)
{
    const char *name, *version, *release;
    char nvr[512], *changelog = NULL, *sourcerpm = NULL;
    struct rpmhdr_ent srcrpm_ent;
    uint32_t epoch, n;

    pm_rpmhdr_nevr(hdr, &name, &epoch, &version, &release, NULL, NULL);
    if (name == NULL || version == NULL || release == NULL)
        return NULL;

    n = n_snprintf(nvr, sizeof(nvr), "%s-%s-%s.", name, version, release);

    if (pm_rpmhdr_ent_get(&srcrpm_ent, hdr, RPMTAG_SOURCERPM))
        sourcerpm = pm_rpmhdr_ent_as_str(&srcrpm_ent);

    /* "main" package */
    if (sourcerpm == NULL || strncmp(sourcerpm, nvr, n) == 0) {
        changelog = do_load_changelog_from_rpmhdr(na, hdr);

    } else { /* subpackage */
        char ts[32], *mainame, *p;
        time_t now = time(NULL);

        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", gmtime(&now));
        n_strdupap(sourcerpm, &mainame);

        if ((p = strrchr(mainame, '.'))) { /* cut off .src.rpm */
            *p = '\0';
            if ((p = strrchr(mainame, '.')))
                *p = '\0';
        }

        changelog = na->na_malloc(na, 512);
        n_snprintf(changelog, 512,
                   "* %s poldek@pld-linux.org\n- see %s's log\n", ts, mainame);
    }

    if (sourcerpm)
        pm_rpmhdr_ent_free(&srcrpm_ent);

    return changelog;
}

struct pkguinf *pkguinf_ldrpmhdr(tn_alloc *na, void *hdr, tn_array *loadlangs)
{
    tn_array           *langs = NULL;
    char               **summs, **descrs;
    int                nsumms, ndescrs;
    int                i, n;
    struct pkguinf     *pkgu;
    Header             h = hdr;

    pkgu = pkguinf_new(na);
    pkgu->_ht = n_hash_new(3, NULL);

    if ((langs = pm_rpmhdr_langs(h))) {
        tn_array *sl_langs = NULL;
        char *lc_lang = NULL, *sl_lang = NULL;

        pm_rpmhdr_get_raw_entry(h, RPMTAG_SUMMARY, (void*)&summs, &nsumms);
        pm_rpmhdr_get_raw_entry(h, RPMTAG_DESCRIPTION, (void*)&descrs, &ndescrs);

        n = nsumms;
        if (n > ndescrs)
            n = ndescrs;

        pkgu->_langs_rpmhdr = n_array_new(4, free, NULL);
        for (i=0; i < n; i++) {
            struct pkguinf_i18n *inf;
            const char *lang;

            if (n_array_size(langs) < i)
                break;

            lang = n_array_nth(langs, i);
            n_array_push(pkgu->_langs_rpmhdr, n_strdup(lang));

            inf = pkguinf_i18n_new(pkgu->_na, summs[i], descrs[i]);
            n_hash_insert(pkgu->_ht, lang, inf);
        }

        /* langs which weren't added to the pkgu->_ht have to be removed,
           otherwise lc_lang_select() may return value which doesn't exist
           in the hash table */
        if (n < n_array_size (langs)) {
    	    for (i = n_array_size(langs); i > n; i--)
                langs = n_array_remove_nth(langs, i - 1);
        }

        if (loadlangs) {
    	    for (i = 0; i < n_array_size(loadlangs); i++) {
    		const char *loadlang = n_array_nth(loadlangs, i);

		if (loadlang == NULL)
		    continue;

		if (lc_lang == NULL)
		    lc_lang = n_strdup(loadlang);
		else {
		    lc_lang = n_str_concat(lc_lang, ":", loadlang, NULL);
		}
    	    }
        } else
    	    lc_lang = n_strdup(lc_messages_lang());

        sl_langs = lc_lang_select(langs, lc_lang);
        if (sl_langs == NULL)
            sl_lang = "C";
        else
            sl_lang = n_array_nth(sl_langs, 0);

        if (sl_lang) {
            struct pkguinf_i18n *inf;

            inf = n_hash_get(pkgu->_ht, sl_lang);
            n_assert(inf);
            pkgu_set_recodable(pkgu, PKGUINF_SUMMARY, inf->summary, sl_lang);
            pkgu_set_recodable(pkgu, PKGUINF_DESCRIPTION, inf->description, sl_lang);
        }

        n_array_free(langs);
        n_array_cfree(&sl_langs);

        free(lc_lang);
        free(summs);
        free(descrs);
    }

    pkgu->vendor = cp_tag(pkgu->_na, h, RPMTAG_VENDOR);
    pkgu->license = cp_tag(pkgu->_na, h, PM_RPMTAG_LICENSE);
    pkgu->url = cp_tag(pkgu->_na, h, RPMTAG_URL);
    pkgu->distro = cp_tag(pkgu->_na, h, RPMTAG_DISTRIBUTION);
    pkgu->buildhost = cp_tag(pkgu->_na, h, RPMTAG_BUILDHOST);
    pkgu->legacy_sourcerpm = NULL;
    pkgu->changelog = load_changelog_from_rpmhdr(pkgu->_na, h);

    return pkgu;
}

tn_array *pkguinf_langs(struct pkguinf *pkgu)
{
    if (pkgu->_langs == NULL)
        pkgu->_langs = n_hash_keys(pkgu->_ht);

    if (pkgu->_langs)
        n_array_sort(pkgu->_langs);
    return pkgu->_langs;
}

#define PKGUINF_TAG_ENDCMN   'E'

tn_buf *pkguinf_store(const struct pkguinf *pkgu, tn_buf *nbuf,
                      const char *lang)
{
    struct pkguinf_i18n *inf;
    struct member {
        char tag;
        char *value;
    } members[] = {
        { PKGUINF_LICENSE, pkgu->license },
        { PKGUINF_URL, pkgu->url },
        { PKGUINF_VENDOR, pkgu->vendor },
        { PKGUINF_BUILDHOST, pkgu->buildhost },
        { PKGUINF_DISTRO, pkgu->distro },
        { PKGUINF_CHANGELOG, pkgu->changelog },
        { 0, NULL }
    };

    n_assert(lang);
    if (n_str_eq(lang, "C")) {
        /* skip entries older than year */
        time_t since = time(NULL) - 3600 * 24 * 356;
        int i = 0;

        while (members[i].tag) {
            struct member m = members[i++];
            const char *value = m.value;

            if (m.tag == PKGUINF_CHANGELOG) {
                if (pkgu->changelog && strlen(pkgu->changelog) > 512)
                    value = pkguinf_get_changelog((struct pkguinf*)pkgu, since);
            }

            if (value == NULL)
                continue;

            n_buf_putc(nbuf, m.tag);
            n_buf_putc(nbuf, '\0');
            n_buf_puts(nbuf, value);
            n_buf_putc(nbuf, '\0');
        }

        n_buf_putc(nbuf, PKGUINF_TAG_ENDCMN);
        n_buf_putc(nbuf, '\0');
    }

    if ((inf = n_hash_get(pkgu->_ht, lang))) {
        n_buf_putc(nbuf, PKGUINF_SUMMARY);
        n_buf_putc(nbuf, '\0');
        n_buf_puts(nbuf, inf->summary);
        n_buf_putc(nbuf, '\0');

        n_buf_putc(nbuf, PKGUINF_DESCRIPTION);
        n_buf_putc(nbuf, '\0');
        n_buf_puts(nbuf, inf->description);
        n_buf_putc(nbuf, '\0');
    }

    return nbuf;
}



struct pkguinf *pkguinf_restore(tn_alloc *na, tn_buf_it *it, const char *lang)
{
    struct pkguinf *pkgu;
    char *key = NULL, *val;
    size_t len = 0;

    pkgu = pkguinf_new(na);

    if (lang && strcmp(lang, "C") == 0) {
        while ((key = n_buf_it_getz(it, &len))) {
            if (len > 1)
                return NULL;

            if (*key == PKGUINF_TAG_ENDCMN)
                break;

            val = n_buf_it_getz(it, &len);
            switch (*key) {
                case PKGUINF_LICENSE:
                    set_member(pkgu, &pkgu->license, val, len);
                    break;

                case PKGUINF_URL:
                    set_member(pkgu, &pkgu->url, val, len);
                    break;

                case PKGUINF_VENDOR:
                    set_member(pkgu, &pkgu->vendor, val, len);
                    break;

                case PKGUINF_BUILDHOST:
                    set_member(pkgu, &pkgu->buildhost, val, len);
                    break;

                case PKGUINF_DISTRO:
                    set_member(pkgu, &pkgu->distro, val, len);
                    break;

                case PKGUINF_LEGACY_SOURCERPM:
                    set_member(pkgu, &pkgu->legacy_sourcerpm, val, len);
                    break;

                case PKGUINF_CHANGELOG:
                    set_member(pkgu, &pkgu->changelog, val, len);
                    break;

                default:
                    /* skip unknown tag */
                    ;
            }
        }
    }

    n_assert(lang);

    pkguinf_restore_i18n(pkgu, it, lang);
    return pkgu;
}


int pkguinf_restore_i18n(struct pkguinf *pkgu, tn_buf_it *it, const char *lang)
{
    struct pkguinf_i18n *inf;
    char *summary, *description, *key;
    size_t slen = 0, dlen = 0, len = 0;


    if (pkgu->_ht == NULL)
        pkgu->_ht = n_hash_new(3, NULL);

    else if (n_hash_exists(pkgu->_ht, lang))
        return 1;

    key = n_buf_it_getz(it, &len);
    if (*key != PKGUINF_SUMMARY)
        return 0;

    summary = n_buf_it_getz(it, &slen);

    key = n_buf_it_getz(it, &len);
    if (*key != PKGUINF_DESCRIPTION)
        return 0;
    description = n_buf_it_getz(it, &dlen);

    inf = pkguinf_i18n_new(pkgu->_na, summary, description);
    n_hash_insert(pkgu->_ht, lang, inf);

    pkgu_set_recodable(pkgu, PKGUINF_SUMMARY, inf->summary, lang);
    pkgu_set_recodable(pkgu, PKGUINF_DESCRIPTION, inf->description, lang);
    return 1;
}

const char *pkguinf_get(const struct pkguinf *pkgu, int tag)
{
    char **val = NULL;     /* for summary, description recoding */
    unsigned doneflag = 0, needflag = 0;

    switch (tag) {
        case PKGUINF_LICENSE:
            return pkgu->license;

        case PKGUINF_URL:
            return pkgu->url;

        case PKGUINF_VENDOR:
            return pkgu->vendor;

        case PKGUINF_BUILDHOST:
            return pkgu->buildhost;

        case PKGUINF_DISTRO:
            return pkgu->distro;

        case PKGUINF_LEGACY_SOURCERPM:
    	    return pkgu->legacy_sourcerpm;

        case PKGUINF_CHANGELOG:
    	    return pkgu->changelog;

        case PKGUINF_SUMMARY:
            val = (char**)&pkgu->_summary;
            doneflag = SUMMARY_RECODED;
            needflag = RECODE_SUMMMARY;
            break;

        case PKGUINF_DESCRIPTION:
            val = (char**)&pkgu->_description;
            doneflag = DESCRITPION_RECODED;
            needflag = RECODE_DESCRIPTION;
            break;

        default:
            if (poldek_VERBOSE > 2)
                logn(LOGERR, "%d: unknown tag", tag);
            break;
    }

    if (val) { /* something to recode? */
        struct pkguinf *uinf = (struct pkguinf *)pkgu;/* disable const, ugly */
        char *recoded = NULL;


        /* already recoded or no recoding needed */
        if ((pkgu->_flags & needflag) == 0)
            return *val;

        recoded = recode(*val, NULL);
        if (recoded && recoded != *val) {
            uinf->_flags |= doneflag; /* free() needed */
            *val = recoded;
        }

        uinf->_flags &= ~needflag; /* do not try anymore */
        return *val;
    }

    return NULL;
}

int pkguinf_set(struct pkguinf *pkgu, int tag, const char *val,
                const char *lang)
{
    int len;

    len = strlen(val);

    switch (tag) {
        case PKGUINF_LICENSE:
            set_member(pkgu, &pkgu->license, val, len);
            break;

        case PKGUINF_URL:
            set_member(pkgu, &pkgu->url, val, len);
            break;

        case PKGUINF_VENDOR:
            set_member(pkgu, &pkgu->vendor, val, len);
            break;

        case PKGUINF_BUILDHOST:
            set_member(pkgu, &pkgu->buildhost, val, len);
            break;

        case PKGUINF_DISTRO:
            set_member(pkgu, &pkgu->distro, val, len);
            break;

        case PKGUINF_LEGACY_SOURCERPM:
            n_die("SOURCERPM");
            break;

        case PKGUINF_SUMMARY:
        case PKGUINF_DESCRIPTION:
        {
            struct pkguinf_i18n *inf;

            if (pkgu->_ht == NULL)
                pkgu->_ht = n_hash_new(3, NULL);

            if (lang == NULL)
                lang = "C";

            if ((inf = n_hash_get(pkgu->_ht, lang)) == NULL) {
                inf = pkguinf_i18n_new(pkgu->_na, NULL, NULL);
                n_hash_insert(pkgu->_ht, lang, inf);
            }

            if (tag == PKGUINF_SUMMARY) {
                inf->summary = na_strdup(pkgu->_na, val, len);
                pkgu_set_recodable(pkgu, PKGUINF_SUMMARY, inf->summary, lang);

            } else {
                inf->description = na_strdup(pkgu->_na, val, len);
                pkgu_set_recodable(pkgu, PKGUINF_DESCRIPTION, inf->description,
                                   lang);
            }
        }

        default:
            if (poldek_VERBOSE > 2)
                logn(LOGERR, "%d: unknown tag", tag);
            return 0;
            break;
    }

    return 1;
}
