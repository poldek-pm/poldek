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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <trurl/trurl.h>
#include <trurl/nstream.h>
#include <trurl/nhash.h>


#include "i18n.h"
#include "log.h"
#include "pkgu.h"
#include "misc.h"
#include "rpm/rpmhdr.h"

struct pkguinf_i18n {
    char              *summary;
    char              *description;
    char              _buf[0];
};

static Header make_pkguinf_hdr(struct pkguinf *pkgu, int *langs_cnt);

static
struct pkguinf_i18n *pkguinf_i18n_new(const char *summary,
                                      const char *description)
{
    int s_len, d_len;
    struct pkguinf_i18n *inf;
    
    s_len = strlen(summary) + 1;
    d_len = strlen(description) + 1;
    inf = n_malloc(sizeof(*inf) + s_len + d_len);

    memcpy(inf->_buf, summary, s_len);
    memcpy(&inf->_buf[s_len], description, d_len);
    inf->summary = inf->_buf;
    inf->description = &inf->_buf[s_len];
    return inf;
}

static
struct pkguinf *pkguinf_malloc(void)
{
    struct pkguinf *pkgu;
    
    pkgu = n_malloc(sizeof(*pkgu));
    memset(pkgu, 0, sizeof(*pkgu));
    
    pkgu->license = NULL;
    pkgu->url = NULL;
    pkgu->summary = NULL;
    pkgu->description = NULL;
    pkgu->vendor = NULL;
    pkgu->buildhost = NULL;
    
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
    
    if (pkgu->license)
        free(pkgu->license);
    
    if (pkgu->url)
        free(pkgu->url);
        
    if (pkgu->summary)
        pkgu->summary = NULL;
        
    if (pkgu->description)
        pkgu->description = NULL;
    
    if (pkgu->vendor)
        free(pkgu->vendor);
    
    if (pkgu->buildhost)
        free(pkgu->buildhost);

    if (pkgu->_langs)
        n_array_free(pkgu->_langs);

    if (pkgu->_langs_rpmhdr)
        n_array_free(pkgu->_langs_rpmhdr);
    
    if (pkgu->_ht)
        n_hash_free(pkgu->_ht);

    pkgu->_langs_rpmhdr = NULL;
    pkgu->_langs = NULL;
    pkgu->_ht = NULL;
    free(pkgu);
}

struct pkguinf *pkguinf_link(struct pkguinf *pkgu)
{
    pkgu->_refcnt++;
    return pkgu;
}


int pkguinf_store_rpmhdr(struct pkguinf *pkgu, tn_buf *nbuf) 
{
    Header   hdr = NULL;
    void     *rawhdr;
    int      rawhdr_size;
    
    int rc;


    hdr = make_pkguinf_hdr(pkgu, NULL);
    rawhdr_size = headerSizeof(hdr, HEADER_MAGIC_NO);
    rawhdr = headerUnload(hdr);

#if 0    
    printf("> %ld\t%d\n", ftell(stream), headerSizeof(pkgu->_hdr, HEADER_MAGIC_NO));
    headerDump(pkgu->_hdr, stdout, HEADER_DUMP_INLINE, rpmTagTable);
#endif     

	n_buf_write_int16(nbuf, n_hash_size(pkgu->_ht));
    n_buf_write_int16(nbuf, rawhdr_size);

    
    rc = (n_buf_write(nbuf, rawhdr, rawhdr_size) == rawhdr_size);
    
    free(rawhdr);
    headerFree(hdr);
    
    return rc;
}


struct pkguinf *pkguinf_restore_rpmhdr_st(tn_stream *st, off_t offset)
{
    uint16_t nsize, nlangs;
    struct pkguinf *pkgu = NULL;
    void *rawhdr;
    Header hdr;

    if (offset > 0)
        if (n_stream_seek(st, offset, SEEK_SET) != 0) {
            logn(LOGERR, "pkguinf_restore: fseek %ld: %m", offset);
            return NULL;
        }

    
    if (!n_stream_read_uint16(st, &nlangs)) {
        logn(LOGERR, "pkguinf_restore: read error nlangs (%m) at %ld %p",
             n_stream_tell(st), st);
        return NULL;
    }
    
    if (!n_stream_read_uint16(st, &nsize)) {
        logn(LOGERR, "pkguinf_restore: read error nsize (%m) at %ld",
             n_stream_tell(st));
        return NULL;
    }
    
    rawhdr = alloca(nsize);
    
    if (n_stream_read(st, rawhdr, nsize) != nsize) {
        logn(LOGERR, "pkguinf_restore: read %d error at %ld", nsize,
             n_stream_tell(st));
        return NULL;
    }
    
    if ((hdr = headerLoad(rawhdr)) != NULL) {
        pkgu = pkguinf_ldhdr(hdr);
        headerFree(hdr); //rpm's memleak
    }

    return pkgu;
}


int pkguinf_skip_rpmhdr(tn_stream *st) 
{
    uint16_t nsize, nlangs;

    n_stream_seek(st, sizeof(nlangs), SEEK_CUR);
    
    if (!n_stream_read_uint16(st, &nsize))
        nsize = 0;
	else
        n_stream_seek(st, nsize, SEEK_CUR);
    
    return nsize;
}


static char *cp_tag(Header h, int rpmtag)
{
    struct rpmhdr_ent  hdrent;
    char *t = NULL;
        
    if (rpmhdr_ent_get(&hdrent, h, rpmtag))
        t = n_strdup(rpmhdr_ent_as_str(&hdrent));
    rpmhdr_ent_free(&hdrent);
    return t;
}

struct pkguinf *pkguinf_ldhdr(Header h) 
{
    char               **langs, **summs, **descrs;
    int                nsumms, ndescrs;
    int                i, n, nlangs = 0;
    struct pkguinf     *pkgu;
    
    
    pkgu = pkguinf_malloc();
    pkgu->_ht = n_hash_new(3, free);
    
    if ((langs = rpmhdr_langs(h))) {
        tn_array *avlangs, *sl_langs;
        char *sl_lang;
        
        headerGetRawEntry(h, RPMTAG_SUMMARY, 0, (void*)&summs, &nsumms);
        headerGetRawEntry(h, RPMTAG_DESCRIPTION, 0, (void*)&descrs, &ndescrs);
        
        n = nsumms;
        if (n > ndescrs)
            n = ndescrs;

        avlangs = n_array_new(4, NULL, (tn_fn_cmp)strcmp);
        pkgu->_langs_rpmhdr = n_array_new(4, free, NULL);
        
        for (i=0; i < n; i++) {
            struct pkguinf_i18n *inf;
            
            if (langs[i] == NULL)
                break;
            
            n_array_push(avlangs, langs[i]);
            n_array_push(pkgu->_langs_rpmhdr, n_strdup(langs[i]));
            
            inf = pkguinf_i18n_new(summs[i], descrs[i]);
            n_hash_insert(pkgu->_ht, langs[i], inf);
        }
        nlangs = n;

        sl_langs = lc_lang_select(avlangs, lc_messages_lang());
        if (sl_langs == NULL)
            sl_lang = "C";
        else
            sl_lang = n_array_nth(sl_langs, n_array_size(sl_langs) - 1); 

        if (sl_lang) {
            struct pkguinf_i18n *inf;
            
            inf = n_hash_get(pkgu->_ht, sl_lang);
            pkgu->summary = inf->summary;
            pkgu->description = inf->description;
        }

        n_array_free(avlangs);
        if (sl_langs)
            n_array_free(sl_langs);
        
        free(langs);
        free(summs);
        free(descrs);
    }

    pkgu->vendor = cp_tag(h, RPMTAG_VENDOR);
    pkgu->license = cp_tag(h, RPMTAG_COPYRIGHT);
    pkgu->url = cp_tag(h, RPMTAG_URL);
    pkgu->distro = cp_tag(h, RPMTAG_DISTRIBUTION);
    pkgu->buildhost = cp_tag(h, RPMTAG_BUILDHOST);

    return pkgu;
}


static Header make_pkguinf_hdr(struct pkguinf *pkgu, int *langs_cnt) 
{
    int                i, nlangs = 0;
    Header             hdr = NULL;
    unsigned           hdr_size;
    tn_array           *langs;
    

    hdr = headerNew();
    if ((langs = pkgu->_langs_rpmhdr) == NULL)
        langs = pkguinf_langs(pkgu);
    
    for (i=0; i < n_array_size(langs); i++) {
        const char *lang = n_array_nth(langs, i);
        struct pkguinf_i18n *inf = n_hash_get(pkgu->_ht, lang);
        
        headerAddI18NString(hdr, RPMTAG_SUMMARY, inf->summary, lang);
        headerAddI18NString(hdr, RPMTAG_DESCRIPTION, inf->description, lang);
    }

    if (pkgu->vendor)
        headerAddEntry(hdr, RPMTAG_VENDOR, RPM_STRING_TYPE, pkgu->vendor, 1);
    
    if (pkgu->license)
        headerAddEntry(hdr, RPMTAG_COPYRIGHT, RPM_STRING_TYPE, pkgu->license, 1);
    
    if (pkgu->url)
        headerAddEntry(hdr, RPMTAG_URL, RPM_STRING_TYPE, pkgu->url, 1);
    
    if (pkgu->distro)
        headerAddEntry(hdr, RPMTAG_DISTRIBUTION, RPM_STRING_TYPE, pkgu->distro, 1);
    
    if (pkgu->buildhost)
        headerAddEntry(hdr, RPMTAG_BUILDHOST, RPM_STRING_TYPE, pkgu->buildhost, 1);

    hdr_size = headerSizeof(hdr, HEADER_MAGIC_NO);
    
    if (hdr_size > UINT16_MAX) {
        logn(LOGERR, "internal: header size too large: %d", hdr_size);
        headerFree(hdr);
        hdr = NULL;
    }

    if (langs_cnt)
        *langs_cnt = nlangs;

    return hdr;
}

tn_array *pkguinf_langs(struct pkguinf *pkgu)
{
    if (pkgu->_langs == NULL) 
        pkgu->_langs = n_hash_keys(pkgu->_ht);
    if (pkgu->_langs)
        n_array_sort(pkgu->_langs);
    return pkgu->_langs;
}


#define PKGUINF_LICENSE      'l'
#define PKGUINF_URL          'u'
#define PKGUINF_SUMMARY      's'
#define PKGUINF_DESCRIPTION  'd'
#define PKGUINF_VENDOR       'v'
#define PKGUINF_BUILDHOST    'b'
#define PKGUINF_DISTRO       'D'

#define PKGUINF_TAG_LANG     'L'
#define PKGUINF_TAG_ENDCMN   'E'

tn_buf *pkguinf_store(const struct pkguinf *pkgu, tn_buf *nbuf,
                      const char *lang)
{
    struct pkguinf_i18n *inf;

    if (lang && strcmp(lang, "C") == 0) {
        if (pkgu->license) {
            n_buf_putc(nbuf, PKGUINF_LICENSE);
            n_buf_putc(nbuf, '\0');
            n_buf_puts(nbuf, pkgu->license);
            n_buf_putc(nbuf, '\0');
        }
    
        if (pkgu->url) {
            n_buf_putc(nbuf, PKGUINF_URL);
            n_buf_putc(nbuf, '\0');
            n_buf_puts(nbuf, pkgu->url);
            n_buf_putc(nbuf, '\0');
        }
    
        if (pkgu->vendor) {
            n_buf_putc(nbuf, PKGUINF_VENDOR);
            n_buf_putc(nbuf, '\0');
            n_buf_puts(nbuf, pkgu->vendor);
            n_buf_putc(nbuf, '\0');
        }
    
        if (pkgu->buildhost) {
            n_buf_putc(nbuf, PKGUINF_BUILDHOST);
            n_buf_putc(nbuf, '\0');
            n_buf_puts(nbuf, pkgu->buildhost);
            n_buf_putc(nbuf, '\0');
        }
    
        if (pkgu->distro) {
            n_buf_putc(nbuf, PKGUINF_DISTRO);
            n_buf_putc(nbuf, '\0');
            n_buf_puts(nbuf, pkgu->distro);
            n_buf_putc(nbuf, '\0');
        }
        
        n_buf_putc(nbuf, PKGUINF_TAG_ENDCMN);
        n_buf_putc(nbuf, '\0');
    }
    
    
    n_assert(lang);
    if (lang != NULL) {
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
        
    } else {
        int i;
        tn_array *langs = n_hash_keys(pkgu->_ht);
        
        n_assert(0);
        
        for (i=0; i < n_array_size(langs); i++) {
            char *lang = n_array_nth(langs, i);
            n_buf_putc(nbuf, PKGUINF_TAG_LANG);
            n_buf_putc(nbuf, '\0');
            n_buf_puts(nbuf, lang);
            n_buf_putc(nbuf, '\0');
            
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
        }
    }
    
    return nbuf;
}


struct pkguinf *pkguinf_restore(tn_buf_it *it, const char *lang)
{
    struct pkguinf *pkgu;
    char *key = NULL, *val;
    size_t len = 0;

    pkgu = pkguinf_malloc();
    
    if (lang && strcmp(lang, "C") == 0) {
        while ((key = n_buf_it_getz(it, &len))) {
            if (len > 1)
                return NULL;
            
            if (*key == PKGUINF_TAG_ENDCMN)
                break;
            
            val = n_buf_it_getz(it, &len);
            switch (*key) {
                case PKGUINF_LICENSE:
                    pkgu->license = n_strdupl(val, len);
                    break;

                case PKGUINF_URL:
                    pkgu->url = n_strdupl(val, len);
                    break;

                case PKGUINF_VENDOR:
                    pkgu->vendor = n_strdupl(val, len);
                    break;

                case PKGUINF_BUILDHOST:
                    pkgu->buildhost = n_strdupl(val, len);
                    break;

                case PKGUINF_DISTRO:
                    pkgu->distro = n_strdupl(val, len);
                    break;

                default:
                    n_assert(0);
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
    size_t len = 0;
    

    if (pkgu->_ht == NULL)
        pkgu->_ht = n_hash_new(3, free);
    
    else if (n_hash_exists(pkgu->_ht, lang))
        return 1;

    key = n_buf_it_getz(it, &len);
    if (*key != PKGUINF_SUMMARY)
        return 0;
    
    summary = n_buf_it_getz(it, &len);

    key = n_buf_it_getz(it, &len);
    if (*key != PKGUINF_DESCRIPTION)
        return 0;
    description = n_buf_it_getz(it, &len);

    inf = pkguinf_i18n_new(summary, description);
    n_hash_insert(pkgu->_ht, lang, inf);
    
    pkgu->summary = inf->summary;
    pkgu->description = inf->description;
    
    return 1;
}




