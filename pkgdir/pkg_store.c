/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).

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

#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nbuf.h>

#include <vfile/vfile.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "pkgdir.h"
#include "pkgdir_intern.h"
#include "pkg.h"
#include "pkgu.h"
#include "pkgfl.h"
#include "capreq.h"

#include "pkg_store.h"

static struct pkg_store_tag pkg_store_tag_table[] = {
    { PKG_STORETAG_NAME,  PKG_STORETAG_SIZENIL, "name" },
    { PKG_STORETAG_EVR,   PKG_STORETAG_SIZENIL, "evr"  },
    { PKG_STORETAG_ARCH,  PKG_STORETAG_SIZENIL, "arch" },
    { PKG_STORETAG_OS,    PKG_STORETAG_SIZENIL, "os"   },
    { PKG_STORETAG_FN,    PKG_STORETAG_SIZENIL, "filename" },
    { PKG_STORETAG_SRCFN, PKG_STORETAG_SIZENIL, "source package filename" },
    /* groupid, btime, etc */
    { PKG_STORETAG_BINF,  PKG_STORETAG_SIZE8,  "pkg's int32 fields" },
    { PKG_STORETAG_CAPS,  PKG_STORETAG_SIZE16, "caps"  },
    { PKG_STORETAG_REQS,  PKG_STORETAG_SIZE16, "reqs"  },
    { PKG_STORETAG_SUGS,  PKG_STORETAG_SIZE16, "sugs"  },
    { PKG_STORETAG_CNFLS, PKG_STORETAG_SIZE16, "cnfls" },
    { PKG_STORETAG_FL,    PKG_STORETAG_SIZE32, "file list" },
    { PKG_STORETAG_DEPFL, PKG_STORETAG_SIZE32, "depdirs file list" },
    { PKG_STORETAG_CONT,  PKG_STORETAG_SIZE16, "deps continuation" },
//    { '6', PKG_STORETAG_SIZE16, "fake16" }, // for testing
//    { '2', PKG_STORETAG_SIZE32, "fake32" },
    { 0, 0, 0 },
};

static int pkg_store_tag_table_size = sizeof(pkg_store_tag_table) / sizeof(pkg_store_tag_table[0]);

static int tag_lookup_tab[256] = {0};

static void init_tag_lookup_tab(void)
{
    register int i = 0;

    tag_lookup_tab[0] = 1;
    while (pkg_store_tag_table[i].tag > 0) {
        //n_assert(pkg_store_tag_table[i].tag < 256);
        tag_lookup_tab[pkg_store_tag_table[i].tag] = i;
        i++;
    }
}

static
const struct pkg_store_tag *pkg_store_lookup_tag(int tag)
{
    register int i;

    if (tag_lookup_tab[0] == 0)
        init_tag_lookup_tab();

    i = tag_lookup_tab[tag];
    if (i == 0)
        return NULL;
    n_assert(i < pkg_store_tag_table_size);
    return &pkg_store_tag_table[i];
}

int pkg_store_skiptag(int tag, int tag_binsize, tn_stream *st)
{
    DBGF("skiptag %c %c\n", tag, tag_binsize ? tag_binsize:'-');
    tag = tag;

    switch (tag_binsize) {
        case PKG_STORETAG_SIZENIL:
            return 1;

        case PKG_STORETAG_SIZE8:
            n_buf_restore_skip(st, TN_BUF_STORE_8B);
            break;

        case PKG_STORETAG_SIZE16:
            n_buf_restore_skip(st, TN_BUF_STORE_16B);
            break;

        case PKG_STORETAG_SIZE32:
            n_buf_restore_skip(st, TN_BUF_STORE_32B);
            break;

        default:
            return 0;
            break;
    }
    return 1;
    return n_stream_seek(st, 1, SEEK_CUR) == 0; /* skip ending '\n' */
}


static int pkg_store_bintag(int tag, tn_buf *nbuf)
{
    const struct pkg_store_tag *tg;
    char s[4] = {0};


    s[0] = tag;
    s[1] = ':';

    tg = pkg_store_lookup_tag(tag);
    n_assert(tg);
    n_assert(tg->binsize);

    s[2] = tg->binsize;
    s[3] = '\n';
    return n_buf_write(nbuf, s, 4) == 4;
}

static void store_capreq_array(const struct pkg *pkg, tn_array *capreqs, int8_t tag, tn_buf *nbuf)
{
    tn_array *arr = n_array_new(n_array_size(capreqs), NULL, NULL);

    for (int i=0; i < n_array_size(capreqs); i++) {
        struct capreq *cr = n_array_nth(capreqs, i);
        if (pkg_eq_capreq(pkg, cr))
            continue;

        if (capreq_is_bastard(cr))
            continue;

        n_array_push(arr, cr);
    }

    if (n_array_size(arr) == 0)
        goto l_end;

    int nstored = 0;

    do {
        int off = n_buf_tell(nbuf);
        pkg_store_bintag(tag, nbuf);
        nstored = capreq_arr_store(arr, nbuf);

        n_assert(nstored <= n_array_size(arr));

        if (nstored == 0) {         /* rollback */
            n_buf_seek(nbuf, off, SEEK_SET);
            break;
        }

        if (nstored == n_array_size(arr)) { /* all stored */
            break;
        }

        msgn(1, "%s: capabilities will be splitted due to their size over 64KB", pkg_id(pkg));

        tag = PKG_STORETAG_CONT;
        while (nstored > 0) {   /* store the rest as CONT */
            n_array_shift(arr);
            nstored--;
        }
        DBGF("cont %s %d\n", pkg_id(pkg), n_array_size(arr));
    } while (n_array_size(arr) > 0);

 l_end:
    n_array_free(arr);
}

/* local tags of PKG_STORETAG_BINF tag */
#define PKGFIELD_TAG_SIZE    'S'
#define PKGFIELD_TAG_FSIZE   's'
#define PKGFIELD_TAG_BTIME   'b'
#define PKGFIELD_TAG_ITIME   'i'
#define PKGFIELD_TAG_GID     'g'
#define PKGFIELD_TAG_RECNO   'r'
#define PKGFIELD_TAG_FMTIME  't'
#define PKGFIELD_TAG_COLOR   'C'

static
void pkg_store_fields(tn_buf *nbuf, const struct pkg *pkg, unsigned flags)
{
    uint8_t n = 0, size8t;
    int size = 0;

    if (pkg->size)
        n++;

    if (pkg->fsize)
        n++;

    if (pkg->btime)
        n++;

    if (pkg->itime)
        n++;

    if (pkg->groupid)
        n++;

    if ((flags & PKGSTORE_RECNO) && pkg->recno)
        n++;

    if (pkg->fmtime)
        n++;

    if (pkg->color)
        n++;

    size = (sizeof(int32_t) + 1) * n;
    n_assert(size < UINT8_MAX);
    size8t = size;

    n_buf_write_uint8(nbuf, size8t);
    n_buf_write_uint8(nbuf, n);

    if (pkg->size) {
        n_buf_add_int8(nbuf, PKGFIELD_TAG_SIZE);
        n_buf_add_int32(nbuf, pkg->size);
    }

    if (pkg->fsize) {
        n_buf_add_int8(nbuf, PKGFIELD_TAG_FSIZE);
        n_buf_add_int32(nbuf, pkg->fsize);
    }

    if (pkg->btime) {
        n_buf_add_int8(nbuf, PKGFIELD_TAG_BTIME);
        n_buf_add_int32(nbuf, pkg->btime);
    }

    if (pkg->itime) {
        n_buf_add_int8(nbuf, PKGFIELD_TAG_ITIME);
        n_buf_add_int32(nbuf, pkg->itime);
    }

    if (pkg->groupid) {
        n_buf_add_int8(nbuf, PKGFIELD_TAG_GID);
        //printf("STORE gid %d %s\n", pkg->groupid, pkg_snprintf_s(pkg));
        n_buf_add_int32(nbuf, pkg->groupid);
    }

    if ((flags & PKGSTORE_RECNO) && pkg->recno) {
        n_buf_add_int8(nbuf, PKGFIELD_TAG_RECNO);
        n_buf_add_int32(nbuf, pkg->recno);
    }

    if (pkg->fmtime) {
        n_buf_add_int8(nbuf, PKGFIELD_TAG_FMTIME);
        n_buf_add_int32(nbuf, pkg->fmtime);
    }

    if (pkg->color) {
        n_buf_add_int8(nbuf, PKGFIELD_TAG_COLOR);
        n_buf_add_int32(nbuf, pkg->color);
    }

    n_buf_printf(nbuf, "\n");
}


int pkg_restore_fields(tn_stream *st, struct pkg *pkg)
{
    uint8_t n = 0, nsize, tag = 0;
    uint32_t tmp;

    n_stream_read_uint8(st, &nsize);
    n_stream_read_uint8(st, &n);

    while (n) {
        n_stream_read_uint8(st, &tag);
        switch (tag) {
            case PKGFIELD_TAG_SIZE:
                n_stream_read_uint32(st, &pkg->size);
                break;

            case PKGFIELD_TAG_FSIZE:
                n_stream_read_uint32(st, &pkg->fsize);
                break;

            case PKGFIELD_TAG_BTIME:
                n_stream_read_uint32(st, &pkg->btime);
                break;

            case PKGFIELD_TAG_ITIME:
                n_stream_read_uint32(st, (uint32_t*)&pkg->itime);
                break;

            case PKGFIELD_TAG_GID:
                n_stream_read_uint32(st, (uint32_t*)&pkg->groupid);
                break;

            case PKGFIELD_TAG_RECNO:
                n_stream_read_uint32(st, &pkg->recno);
                break;

            case PKGFIELD_TAG_FMTIME:
                n_stream_read_uint32(st, &pkg->fmtime);
                break;

            case PKGFIELD_TAG_COLOR:
                n_stream_read_uint32(st, &pkg->color);
                break;

            default:            /* skip unknown tag */
                n_stream_read_uint32(st, &tmp);
                break;
        }
        n--;
    }

    return n_stream_read_uint8(st, &n); /* '\n' */
}



static
void pkg_store_fl(const struct pkg *pkg, tn_buf *nbuf,
                  tn_array *exclpath, tn_array *depdirs,
                  unsigned flags)
{
    struct pkgflist *flist;


    if ((flags & PKGSTORE_NOANYFL) == PKGSTORE_NOANYFL)
        return;

    DBGF("%s\n", pkg_snprintf_s(pkg));

    flist = pkg_get_flist(pkg);
    DBGF("flist = %p\n", flist);
    if (flist == NULL)
        return;

    if (depdirs == NULL) {
        if ((flags & PKGSTORE_NOFL) == 0) {
            pkg_store_bintag(PKG_STORETAG_FL, nbuf);
            pkgfl_store(flist->fl, nbuf, exclpath, depdirs, PKGFL_ALL);
        }

    } else {
        if ((flags & PKGSTORE_NODEPFL) == 0) {
            pkg_store_bintag(PKG_STORETAG_DEPFL, nbuf);
            pkgfl_store(flist->fl, nbuf, exclpath, depdirs, PKGFL_DEPDIRS);
        }


        if ((flags & PKGSTORE_NOFL) == 0) {
            pkg_store_bintag(PKG_STORETAG_FL, nbuf);
            pkgfl_store(flist->fl, nbuf, exclpath, depdirs, PKGFL_NOTDEPDIRS);
        }
    }
    DBGF("END %s\n", pkg_snprintf_s(pkg));
    pkgflist_free(flist);
}


int pkg_store(const struct pkg *pkg, tn_buf *nbuf, tn_array *depdirs,
              tn_array *exclpath, unsigned flags)
{
    int ncaps;

    msgn(3, "storing %s", pkg_id(pkg));

    if ((flags & PKGSTORE_NOEVR) == 0) {
        n_buf_printf(nbuf, "N: %s\n", pkg->name);
        if (pkg->epoch)
            n_buf_printf(nbuf, "V: %d:%s-%s\n", pkg->epoch, pkg->ver, pkg->rel);
        else
            n_buf_printf(nbuf, "V: %s-%s\n", pkg->ver, pkg->rel);
    }

    if ((flags & PKGSTORE_NOARCH) == 0 && pkg->_arch)
        n_buf_printf(nbuf, "A: %s\n", pkg_arch(pkg));

    if ((flags & PKGSTORE_NOOS) == 0 && pkg->_os)
        n_buf_printf(nbuf, "O: %s\n", pkg_os(pkg));

    if (pkg->fn)
        n_buf_printf(nbuf, "%c: %s\n", PKG_STORETAG_FN, pkg->fn);

    if (pkg->srcfn)
        n_buf_printf(nbuf, "%c: %s\n", PKG_STORETAG_SRCFN, pkg->srcfn);
    else                        /* must store something, PKG_HAS_SRCFN */
        n_buf_printf(nbuf, "%c: -\n", PKG_STORETAG_SRCFN);

    pkg_store_bintag(PKG_STORETAG_BINF, nbuf);
    pkg_store_fields(nbuf, pkg, flags);

    if (pkg->caps && n_array_size(pkg->caps)) {
        store_capreq_array(pkg, pkg->caps, PKG_STORETAG_CAPS, nbuf);
    }

    if (pkg->reqs && (ncaps = capreq_arr_store_n(pkg->reqs))) {
        store_capreq_array(pkg, pkg->reqs, PKG_STORETAG_REQS, nbuf);
    }

    if (pkg->sugs && (ncaps = capreq_arr_store_n(pkg->sugs))) {
        store_capreq_array(pkg, pkg->sugs, PKG_STORETAG_SUGS, nbuf);
    }

    if (pkg->cnfls && (ncaps = capreq_arr_store_n(pkg->cnfls))) {
        store_capreq_array(pkg, pkg->cnfls, PKG_STORETAG_CNFLS, nbuf);
    }

    //mem_info(-10, "before fl");
    pkg_store_fl(pkg, nbuf, depdirs, exclpath, flags);
    //mem_info(-10, "after fl");

    /* removed support for uinfo in main index,
       assert nobody try this */
    n_assert(flags & PKGSTORE_NODESC);

    n_buf_printf(nbuf, "\n");

    return n_buf_size(nbuf);
}


int pkg_store_st(const struct pkg *pkg, tn_stream *st, tn_array *depdirs,
                 unsigned flags)
{
    tn_buf *nbuf;
    int n = 0;

    nbuf = n_buf_new(1024 * 8);
    if (pkg_store(pkg, nbuf, NULL, depdirs, flags) > 0)
        n = n_stream_write(st, n_buf_ptr(nbuf), n_buf_size(nbuf));

    n_buf_free(nbuf);
    return n;
}
