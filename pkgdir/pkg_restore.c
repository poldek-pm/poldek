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

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nbuf.h>
#include <trurl/nstream.h>
#include <trurl/nmalloc.h>

#include <vfile/vfile.h>

#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "pkgdir.h"
#include "pkgdir_intern.h"
#include "pkg.h"
#include "pkgu.h"
#include "pkgfl.h"
#include "capreq.h"
#include "pkgmisc.h"

#define PKGT_HAS_NAME     (1 << 0)
#define PKGT_HAS_EVR      (1 << 1)
#define PKGT_HAS_CAP      (1 << 3)
#define PKGT_HAS_REQ      (1 << 4)
#define PKGT_HAS_CNFL     (1 << 5)
#define PKGT_HAS_FILES    (1 << 6)
#define PKGT_HAS_ARCH     (1 << 7)
#define PKGT_HAS_OS       (1 << 8)
#define PKGT_HAS_SIZE     (1 << 9)
#define PKGT_HAS_FSIZE    (1 << 10)
#define PKGT_HAS_BTIME    (1 << 11)
#define PKGT_HAS_GROUPID  (1 << 12)
#define PKGT_HAS_FN       (1 << 13)

struct pkgtags_s {
    unsigned   flags;
    char       name[64];
    char       evr[64];
    char       arch[64];
    char       os[64];
    char       fn[PATH_MAX];
    uint32_t   size;
    uint32_t   fsize;
    uint32_t   btime;
    uint32_t   groupid;
    tn_array   *caps;
    tn_array   *reqs;
    tn_array   *cnfls;
    tn_tuple   *pkgfl;
    off_t      nodep_files_offs; /* non dep files tag off_t */
    
    struct pkguinf *pkguinf;
    off_t      pkguinf_offs;
};

extern int pkg_restore_fields(tn_stream *st, struct pkg *pkg);

static
int add2pkgtags(struct pkgtags_s *pkgt, char tag, char *value,
                const char *pathname, off_t offs);

static
struct pkg *pkg_ldtags(tn_alloc *na, struct pkg *pkg,
                       struct pkgtags_s *pkgt, struct pkg_offs *pkgo);


inline static char *eatws(char *str) 
{
    while (isspace(*str))
        str++;
    return str;
}

inline static char *to_printable(char *s, int n) 
{
    int i;
    
    for (i=0; i < n ; i++)
        if (!isprint(s[i]))
            s[i] = '.';
    
    return s;
}



inline static char *next_tokn(char **str, char delim, int *toklen) 
{
    char *p, *token;

    
    if ((p = strchr(*str, delim)) == NULL) 
        token = NULL;
    else {
        *p = '\0';
        
        if (toklen)
            *toklen = p - *str;
        p++;
        while(isspace(*p))
            p++;
        token = *str;
        *str = p;
    }
    
    return token;
}

struct pkg *pkg_restore_st(tn_stream *st, tn_alloc *na, struct pkg *pkg, 
                           tn_array *depdirs, unsigned ldflags,
                           struct pkg_offs *pkgo, const char *fn)
{
    struct pkgtags_s     pkgt;
    struct pkg           tmpkg;
    off_t                offs;
    unsigned long        ul_offs;
    char                 linebuf[4096];
    int                  nerr = 0, nread, pkg_loaded = 0;
    int                  tag, tag_binsize = PKG_STORETAG_SIZENIL;
    const  char          *errmg_double_tag = "%s:%lu: double '%c' tag";
    const  char          *errmg_ldtag = "%s:%lu: load '%c' tag error";

#if 0
    printf("FULL %d\n", (ldflags & PKGDIR_LD_FULLFLIST));
    if (depdirs) {
        int i;
        printf("depdirs %p %d\n", depdirs, n_array_size(depdirs));
        for (i=0; i<n_array_size(depdirs); i++) {
            printf("DEP %s\n", n_array_nth(depdirs, i));
        }
    }
    
#endif    

    memset(&pkgt, 0, sizeof(pkgt));
    memset(&tmpkg, 0, sizeof(tmpkg));
    
    while ((nread = n_stream_gets(st, linebuf, sizeof(linebuf))) > 0) {
        char *p, *val, *line;
        
        offs = n_stream_tell(st);
        ul_offs = offs;         /* to satisfy printf() */
        line = linebuf;
        
        //printf("line[%ld] = (%s)\n", offs, line);
        if (*line == '\n') {        /* empty line -> end of record */
            //printf("\n\nEOR\n");
            pkg = pkg_ldtags(na, pkg, &pkgt, pkgo);
            pkg->size = tmpkg.size;
            pkg->fsize = tmpkg.fsize;
            pkg->btime = tmpkg.btime;
            pkg->itime = tmpkg.itime;
            pkg->groupid = tmpkg.groupid;
            pkg->recno = tmpkg.recno;
            pkg->fmtime = tmpkg.fmtime;
            pkg_loaded = 1;
			//if (pkg)
            //    printf("ld %s\n", pkg_snprintf_s(pkg));
            break;
        }

        if (*line == ' ') {      /* continuation */
            logn(LOGERR, _("%s:%lu: syntax error"), fn, ul_offs);
            nerr++;
            goto l_end;
        }
		
            
        while (nread && line[nread - 1] == '\n')
            line[--nread] = '\0';
        
        p = val = line + 1;
        if (*line == '\0' || *p != ':') {
            logn(LOGERR, "%s:%lu[%s]: ':' expected", fn, ul_offs,
                 to_printable(linebuf, nread));
            nerr++;
            goto l_end;
        }
            
        *val++ = '\0';
        n_assert(*line && *(line + 1) == '\0');
        tag = *line;

        if (*val != ' ') {      /* no space after ':' => binary tag */
            tag_binsize = *val;
            val++;
        }
        
        val = eatws(val);
        
        switch (tag) {
            case PKG_STORETAG_NAME:
            case PKG_STORETAG_EVR:
            case PKG_STORETAG_ARCH:
            case PKG_STORETAG_OS:
            case PKG_STORETAG_FN:
                if (tag_binsize != PKG_STORETAG_SIZENIL) {
                    logn(LOGERR, errmg_ldtag, fn, ul_offs, tag);
                    nerr++;
                    goto l_end;
                }
                
                if (!add2pkgtags(&pkgt, tag, val, fn, offs)) {
                    nerr++;
                    goto l_end;
                }
                break;

            case PKG_STORETAG_BINF:
                if (pkgt.flags & PKGT_HAS_SIZE) {
                    logn(LOGERR, _("%s:%lu: syntax error"), fn, ul_offs);
                    nerr++;
                    goto l_end;
                }
                
                memset(&tmpkg, 0, sizeof(tmpkg)); /* make it nicer in the future */
                pkg_restore_fields(st, &tmpkg);
                pkgt.flags |= PKGT_HAS_SIZE | PKGT_HAS_FSIZE | PKGT_HAS_BTIME |
                    PKGT_HAS_GROUPID;
                break;
                

            case PKG_STORETAG_CAPS:
                if (pkgt.flags & PKGT_HAS_CAP) {
                    logn(LOGERR, errmg_double_tag, fn, ul_offs, *line);
                    nerr++;
                    goto l_end;
                }
                
                pkgt.caps = capreq_arr_restore_st(na, st);
                pkgt.flags |= PKGT_HAS_CAP;
                break;
                    
            case PKG_STORETAG_REQS:
                if (pkgt.flags & PKGT_HAS_REQ) {
                    logn(LOGERR, errmg_double_tag, fn, ul_offs, *line);
                    nerr++;
                    goto l_end;
                }
                    
                pkgt.reqs = capreq_arr_restore_st(na, st);
                if (pkgt.reqs == NULL) {
                    logn(LOGERR, errmg_ldtag, fn, ul_offs, *line);
                    nerr++;
                    goto l_end;
                }
                pkgt.flags |= PKGT_HAS_REQ;
                break;
                    
            case PKG_STORETAG_CNFLS:
                if (pkgt.flags & PKGT_HAS_CNFL) {
                    logn(LOGERR, _(errmg_double_tag), fn, ul_offs, *line);
                    nerr++;
                    goto l_end;
                }
                    
                pkgt.cnfls = capreq_arr_restore_st(na, st);
                
                if (pkgt.cnfls == NULL) {
                    logn(LOGERR, errmg_ldtag, fn, ul_offs, *line);
                    nerr++;
                    goto l_end;
                    
                } else if (n_array_size(pkgt.cnfls) == 0) {
                    n_array_free(pkgt.cnfls);
                    pkgt.cnfls = NULL;
                }
                
                if (pkgt.cnfls)
                    pkgt.flags |= PKGT_HAS_CNFL;
                break;

            case PKG_STORETAG_DEPFL:
                if (pkgfl_restore_st(na, &pkgt.pkgfl, st, NULL, 0) < 0) {
                    logn(LOGERR, errmg_ldtag, fn, ul_offs, *line);
                    nerr++;
                    goto l_end;
                }
                if (pkgt.pkgfl)
                    pkgt.flags |= PKGT_HAS_FILES;
                break;
                    
            case PKG_STORETAG_FL:
                pkgt.nodep_files_offs = n_stream_tell(st);
                //printf("flag_fullflist %d, %p\n", flag_fullflist, depdirs);
                if ((ldflags & PKGDIR_LD_FULLFLIST) == 0 && depdirs == NULL) {
                    pkgfl_skip_st(st);
                        
                } else {
                    tn_tuple *fl;
                    if (pkgfl_restore_st(na, &fl, st, depdirs, 1) < 0) {
                        logn(LOGERR, errmg_ldtag, fn, ul_offs, *line);
                        nerr++;
                        goto l_end;
                    }

                    if (fl == NULL)
                        break;
                    
                    if (pkgt.pkgfl == NULL) {
                        pkgt.pkgfl = fl;
                        pkgt.flags |= PKGT_HAS_FILES;
                            
                    } else {
                        int i, n;
                        tn_tuple *ffl;
                        
                        ffl = n_tuple_new(na, n_tuple_size(pkgt.pkgfl) +
                                          n_tuple_size(fl), NULL);

                        n = 0;
                        for (i=0; i < n_tuple_size(pkgt.pkgfl); i++)
                            n_tuple_set_nth(ffl, n++, n_tuple_nth(pkgt.pkgfl, i));
                        
                        for (i=0; i < n_tuple_size(fl); i++)
                            n_tuple_set_nth(ffl, n++, n_tuple_nth(fl, i));
                        
                        pkgt.pkgfl = ffl;
                    }
                }
                break;

            case PKG_STORETAG_UINF:
                pkgt.pkguinf_offs = n_stream_tell(st);
                if ((ldflags & PKGDIR_LD_DESC) == 0) {
                    pkguinf_skip_rpmhdr(st);
					
                } else {
                    pkgt.pkguinf = pkguinf_restore_rpmhdr_st(na, st, 0);
                    if (pkgt.pkguinf == NULL) {
                        logn(LOGERR, errmg_ldtag, fn, ul_offs, *line);
                        nerr++;
                        goto l_end;
                    }
                }
				
				n_stream_seek(st, 1, SEEK_CUR);	/* eat '\n' */
                break;

            default:
                if (poldek_VERBOSE > 1) 
                    logn(LOGWARN, "%s:%lu: skipped unknown tag '%c'", fn,
                         ul_offs, tag);
                
                if (!pkg_store_skiptag(tag, tag_binsize, st)) {
                    logn(LOGERR, "%s:%lu: %c: unknown tag binsize (%c)",
                         fn, offs, tag,
                         isascii(tag_binsize) ? tag_binsize : '-');
                    nerr++;
                    goto l_end;
                }
                break;
        }
    }
    
    
 l_end:

    if (pkg && (nerr > 0 || !pkg_loaded)) {
        if (pkg_loaded)
            pkg_free(pkg);
        pkg = NULL;
    }
    
    return pkg;
}


#define sizeof_pkgt(memb) (sizeof((pkgt)->memb) - 1)
static
int add2pkgtags(struct pkgtags_s *pkgt, char tag, char *value,
                const char *pathname, off_t offs) 
{
    int err = 0;
    const char *errmg_double_tag = "%s:%lu: double '%c' tag";
    unsigned long ul_offs = offs;
    
    switch (tag) {
        case PKG_STORETAG_NAME:
            if (pkgt->flags & PKGT_HAS_NAME) {
                logn(LOGERR, errmg_double_tag, pathname, ul_offs, tag);
                err++;
            } else {
                memcpy(pkgt->name, value, sizeof(pkgt->name)-1);
                pkgt->flags |= PKGT_HAS_NAME;
            }
            break;

        case PKG_STORETAG_EVR:
            if (pkgt->flags & PKGT_HAS_EVR) {
                logn(LOGERR, errmg_double_tag, pathname, ul_offs, tag);
                err++;
            } else {
                memcpy(pkgt->evr, value, sizeof(pkgt->evr)-1);
                pkgt->evr[sizeof(pkgt->evr)-1] = '\0';
                pkgt->flags |= PKGT_HAS_EVR;
            }
            break;
            
        case PKG_STORETAG_ARCH:
            if (pkgt->flags & PKGT_HAS_ARCH) {
                logn(LOGERR, errmg_double_tag, pathname, ul_offs, tag);
                err++;
            } else {
                memcpy(pkgt->arch, value, sizeof(pkgt->arch)-1);
                pkgt->arch[sizeof(pkgt->arch)-1] = '\0';
                pkgt->flags |= PKGT_HAS_ARCH;
            }
            break;

        case PKG_STORETAG_OS:
            if (pkgt->flags & PKGT_HAS_OS) {
                logn(LOGERR, errmg_double_tag, pathname, ul_offs, tag);
                err++;
            } else {
                memcpy(pkgt->os, value, sizeof(pkgt->os) - 1);
                pkgt->os[ sizeof(pkgt->os) - 1 ] = '\0';
                pkgt->flags |= PKGT_HAS_OS;
            }
            break;
            
        case PKG_STORETAG_FN:
            if (pkgt->flags & PKGT_HAS_FN) {
                logn(LOGERR, errmg_double_tag, pathname, ul_offs, tag);
                err++;
            } else {
                memcpy(pkgt->fn, value, sizeof(pkgt->fn)-1);
                pkgt->flags |= PKGT_HAS_FN;
            }
            break;
            
        default:
            logn(LOGERR, "%s:%lu: unknown tag '%c'", pathname, ul_offs, tag);
            n_assert(0);
    }
    
    return err == 0;
}

static
struct pkg *pkg_ldtags(tn_alloc *na, struct pkg *pkg,
                       struct pkgtags_s *pkgt, struct pkg_offs *pkgo) 
{
    const char *ver, *rel;
    char *arch = NULL, *os = NULL;
    int32_t epoch;

    if (pkg == NULL) {
        if (!(pkgt->flags & (PKGT_HAS_NAME | PKGT_HAS_EVR)))
            return NULL;
    
    
        if (*pkgt->name == '\0' || *pkgt->evr == '\0') 
            return NULL;
        
        if (!poldek_util_parse_evr(pkgt->evr, &epoch, &ver, &rel))
            return NULL;
        
        if (ver == NULL || rel == NULL) {
            logn(LOGERR, _("%s: failed to parse evr string"), pkgt->name);
            return NULL;
        }
    }
    
    if (pkgt->flags & PKGT_HAS_OS) 
        os = pkgt->os;
    
    if (pkgt->flags & PKGT_HAS_ARCH) 
        arch = pkgt->arch;

    if (pkg == NULL) {
        pkg = pkg_new_ext(na, pkgt->name, epoch, ver, rel, arch, os, 
                          (pkgt->flags & PKGT_HAS_FN) ? pkgt->fn : NULL,
                          pkgt->size, pkgt->fsize, pkgt->btime);
    } else {
        /* os && arch should be included in given pkg */
        n_assert(os == NULL);
        n_assert(arch == NULL);
        
        pkg = pkg_new_ext(na, pkg->name, pkg->epoch, pkg->ver, pkg->rel,
                          pkg_arch(pkg), pkg_os(pkg), 
                          (pkgt->flags & PKGT_HAS_FN) ? pkgt->fn : NULL,
                          pkgt->size, pkgt->fsize, pkgt->btime);
        
    }
    
    if (pkg == NULL) {
        logn(LOGERR, _("error reading %s's data"), pkgt->name);
        return NULL;
    }
    
    pkg->groupid = pkgt->groupid;

    msg(10, " load  %s\n", pkg_snprintf_s(pkg));

    if (pkgt->flags & PKGT_HAS_CAP) {
        n_assert(pkgt->caps && n_array_size(pkgt->caps));
        pkg->caps = pkgt->caps;
        pkgt->caps = NULL;
    }
    
    if (pkgt->flags & PKGT_HAS_REQ) {
        n_assert(pkgt->reqs && n_array_size(pkgt->reqs));
        pkg->reqs = pkgt->reqs;
        pkgt->reqs = NULL;
    }

    if (pkgt->flags & PKGT_HAS_CNFL) {
        n_assert(pkgt->cnfls && n_array_size(pkgt->cnfls));
        n_array_sort(pkgt->cnfls);
        pkg->cnfls = pkgt->cnfls;
        pkgt->cnfls = NULL;
    }

    if (pkgt->flags & PKGT_HAS_FILES) {
        if (n_tuple_size(pkgt->pkgfl) == 0) {
            n_tuple_free(na, pkgt->pkgfl);
            pkgt->pkgfl = NULL;
        } else {
            pkg->fl = pkgt->pkgfl;
            n_tuple_sort_ex(pkg->fl, (tn_fn_cmp)pkgfl_ent_cmp);
            //pkgfl_dump(pkg->fl);
            pkgt->pkgfl = NULL;
        }
    }

    

    if (pkgo) {
        pkgo->nodep_files_offs = pkgt->nodep_files_offs;
        pkgo->pkguinf_offs = pkgt->pkguinf_offs;
    }

    if (pkgt->pkguinf != NULL) {
        pkg->pkg_pkguinf = pkgt->pkguinf;
        pkg_set_ldpkguinf(pkg);
        pkgt->pkguinf = NULL;
        
    } else {
        n_assert(pkg_has_ldpkguinf(pkg) == 0);
    }

    return pkg;
}

