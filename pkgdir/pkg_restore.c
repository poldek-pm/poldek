/*
  Copyright (C) 2000 - 2002 Pawel A. Gajda <mis@k2.net.pl>

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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nbuf.h>
#include <trurl/nstream.h>


#include <vfile/vfile.h>

#define PKGDIR_INTERNAL

#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "pkgdir.h"
#include "pkg.h"
#include "pkgmisc.h"
#include "h2n.h"
#include "pkgroup.h"

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
#define PKGT_F_v017   (1 << 15);

struct pkgtags_s {
    unsigned   flags;
    char       name[64];
    char       evr[64];
    char       arch[64];
    char       os[64];
    uint32_t   size;
    uint32_t   fsize;
    uint32_t   btime;
    uint32_t   groupid;
    tn_array   *caps;
    tn_array   *reqs;
    tn_array   *cnfls;
    tn_array   *pkgfl;
    off_t      nodep_files_offs; /* non dep files tag off_t */
    
    struct pkguinf *pkguinf;
    off_t      pkguinf_offs;
};

extern int pkg_restore_fields(tn_stream *st, struct pkg *pkg);

static
int add2pkgtags(struct pkgtags_s *pkgt, char tag, char *value,
                const char *pathname, off_t offs);

static
struct pkg *pkg_ldtags(struct pkg *pkg,
                       struct pkgtags_s *pkgt, struct pkg_offs *pkgo);

static 
int restore_pkg_fields_v0_17(tn_stream *st, uint32_t *size,
                             uint32_t *fsize, uint32_t *btime,
                             uint32_t *groupid);


inline static char *eatws(char *str) 
{
    while (isspace(*str))
        str++;
    return str;
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

struct pkg *pkg_restore(tn_stream *st, struct pkg *pkg, 
                        tn_array *depdirs, unsigned ldflags,
                        struct pkg_offs *pkgo, const char *fn)
{
    struct pkgtags_s   pkgt;
    struct pkg         tmpkg;
    off_t              offs;
    char               linebuf[4096];
    int                nerr = 0, nread, with_pkg = 0;

    const  char        *errmg_double_tag = "%s:%ld: double '%c' tag";
    const  char        *errmg_ldtag = "%s:%ld: load '%c' tag error";
    
#if 0
    if (depdirs) 
        for (i=0; i<n_array_size(depdirs); i++) {
            printf("DEP %s\n", n_array_nth(depdirs, i));
        }
#endif    

    if (pkg)
        with_pkg = 1;
    
    memset(&pkgt, 0, sizeof(pkgt));
    
    while ((nread = n_stream_gets(st, linebuf, sizeof(linebuf))) > 0) {
        char *p, *val, *line;
        
        offs = n_stream_tell(st);
        line = linebuf;
        //printf("line[%ld] = (%s)\n", offs, line);
        if (*line == '\n') {        /* empty line -> end of record */
            //printf("\n\nEOR\n");
            pkg = pkg_ldtags(pkg, &pkgt, pkgo);
            pkg->size = tmpkg.size;
            pkg->fsize = tmpkg.fsize;
            pkg->btime = tmpkg.btime;
            pkg->itime = tmpkg.itime;
            pkg->groupid = tmpkg.groupid;
            pkg->recno = tmpkg.recno;
			//if (pkg)
            //    printf("ld %s\n", pkg_snprintf_s(pkg));
            break;
        }

        if (*line == ' ') {      /* continuation */
            logn(LOGERR, _("%s:%ld: syntax error"), fn, offs);
            nerr++;
            goto l_end;
        }
		
            
        while (nread && line[nread - 1] == '\n')
            line[--nread] = '\0';
        
        p = val = line + 1;
        if (*line == '\0' || *p != ':') {
            logn(LOGERR, _("%s:%ld:%s ':' expected"), fn, offs, line);
            nerr++;
            goto l_end;
        }
            
        *val++ = '\0';
        val = eatws(val);
        n_assert(*line && *(line + 1) == '\0');

        switch (*line) {
            case 'N':
            case 'V':
            case 'A':
            case 'O':
            case 'S':
            case 'T':
                if (!add2pkgtags(&pkgt, *line, val, fn, offs)) {
                    nerr++;
                    goto l_end;
                }
                break;

            case 'F':
                if (pkgt.flags & PKGT_HAS_SIZE) {
                    logn(LOGERR, _("%s:%ld: syntax error"), fn, offs);
                    nerr++;
                    goto l_end;
                }
                restore_pkg_fields_v0_17(st, &pkgt.size, &pkgt.fsize,
                                         &pkgt.btime, &pkgt.groupid);
                pkgt.flags |= PKGT_HAS_SIZE | PKGT_HAS_FSIZE | PKGT_HAS_BTIME |
                    PKGT_HAS_GROUPID;
                pkgt.flags |= PKGT_F_v017;
                break;

            case 'f':
                if (pkgt.flags & PKGT_HAS_SIZE) {
                    logn(LOGERR, _("%s:%ld: syntax error"), fn, offs);
                    nerr++;
                    goto l_end;
                }
                
                memset(&tmpkg, 0, sizeof(tmpkg)); /* make it nicer in the future */
                pkg_restore_fields(st, &tmpkg);
                pkgt.flags |= PKGT_HAS_SIZE | PKGT_HAS_FSIZE | PKGT_HAS_BTIME |
                    PKGT_HAS_GROUPID;
                break;
                

            case 'P':
                if (pkgt.flags & PKGT_HAS_CAP) {
                    logn(LOGERR, errmg_double_tag, fn, offs, *line);
                    nerr++;
                    goto l_end;
                }
                
                pkgt.caps = capreq_arr_restore_f(st);
                pkgt.flags |= PKGT_HAS_CAP;
                break;
                    
            case 'R':
                if (pkgt.flags & PKGT_HAS_REQ) {
                    logn(LOGERR, errmg_double_tag, fn, offs, *line);
                    nerr++;
                    goto l_end;
                }
                    
                pkgt.reqs = capreq_arr_restore_f(st);
                if (pkgt.reqs == NULL) {
                    logn(LOGERR, errmg_ldtag, fn, offs, *line);
                    nerr++;
                    goto l_end;
                }
                pkgt.flags |= PKGT_HAS_REQ;
                break;
                    
            case 'C':
                if (pkgt.flags & PKGT_HAS_CNFL) {
                    logn(LOGERR, _(errmg_double_tag), fn, offs, *line);
                    nerr++;
                    goto l_end;
                }
                    
                pkgt.cnfls = capreq_arr_restore_f(st);
                
                if (pkgt.cnfls == NULL) {
                    logn(LOGERR, errmg_ldtag, fn, offs, *line);
                    nerr++;
                    goto l_end;
                    
                } else if (n_array_size(pkgt.cnfls) == 0) {
                    n_array_free(pkgt.cnfls);
                    pkgt.cnfls = NULL;
                }
                
                if (pkgt.cnfls)
                    pkgt.flags |= PKGT_HAS_CNFL;
                break;

            case 'L':
                pkgt.pkgfl = pkgfl_restore_f(st, NULL, 0);
                if (pkgt.pkgfl == NULL) {
                    logn(LOGERR, errmg_ldtag, fn, offs, *line);
                    nerr++;
                    goto l_end;
                }
                n_assert(pkgt.pkgfl);
                //printf("DUMP %p %d\n", pkgt.pkgfl, n_array_size(pkgt.pkgfl));
                //pkgfl_dump(pkgt.pkgfl);
                pkgt.flags |= PKGT_HAS_FILES;
                break;
                    
            case 'l':
                pkgt.nodep_files_offs = n_stream_tell(st);
                //printf("flag_fullflist %d, %p\n", flag_fullflist, depdirs);
                if ((ldflags && PKGDIR_LD_FULLFLIST) == 0 && depdirs == NULL) {
                    pkgfl_skip_f(st);
                        
                } else {
                    tn_array *fl;
                        
                    fl = pkgfl_restore_f(st, depdirs, 1);
                    if (fl == NULL) {
                        logn(LOGERR, errmg_ldtag, fn, offs, *line);
                        nerr++;
                        goto l_end;
                    }
                    
                    if (pkgt.pkgfl == NULL) {
                        pkgt.pkgfl = fl;
                        pkgt.flags |= PKGT_HAS_FILES;
                            
                    } else {
                        while (n_array_size(fl)) 
                            n_array_push(pkgt.pkgfl, n_array_shift(fl));
                        n_array_free(fl);
                    }
                }
                break;

            case 'U':
                pkgt.pkguinf_offs = n_stream_tell(st);
                if ((ldflags & PKGDIR_LD_DESC) == 0) {
                    pkguinf_skip_rpmhdr(st);
					
                } else {
                    pkgt.pkguinf = pkguinf_restore_rpmhdr_st(st, 0);
                    if (pkgt.pkguinf == NULL) {
                        logn(LOGERR, errmg_ldtag, fn, offs, *line);
                        nerr++;
                        goto l_end;
                    }
                }
				
				n_stream_seek(st, 1, SEEK_CUR);	/* eat '\n' */
                break;

            default:
                logn(LOGERR, "%s:%ld: unknown tag '%c'", fn, offs, *line);
                nerr++;
                goto l_end;
        }
    }
    
    
 l_end:

    if (pkg && nerr > 0) {
        if (with_pkg == 0)
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
    const char *errmg_double_tag = "%s:%d: double '%c' tag";
    
    switch (tag) {
        case 'N':
            if (pkgt->flags & PKGT_HAS_NAME) {
                logn(LOGERR, errmg_double_tag, pathname, offs, tag);
                err++;
            } else {
                memcpy(pkgt->name, value, sizeof(pkgt->name)-1);
                pkgt->flags |= PKGT_HAS_NAME;
            }
            break;
            
        case 'V':
            if (pkgt->flags & PKGT_HAS_EVR) {
                logn(LOGERR, errmg_double_tag, pathname, offs, tag);
                err++;
            } else {
                memcpy(pkgt->evr, value, sizeof(pkgt->evr)-1);
                pkgt->evr[sizeof(pkgt->evr)-1] = '\0';
                pkgt->flags |= PKGT_HAS_EVR;
            }
            break;
            
        case 'A':
            if (pkgt->flags & PKGT_HAS_ARCH) {
                logn(LOGERR, errmg_double_tag, pathname, offs, tag);
                err++;
            } else {
                memcpy(pkgt->arch, value, sizeof(pkgt->arch)-1);
                pkgt->arch[sizeof(pkgt->arch)-1] = '\0';
                pkgt->flags |= PKGT_HAS_ARCH;
            }
            break;

        case 'O':
            if (pkgt->flags & PKGT_HAS_OS) {
                logn(LOGERR, errmg_double_tag, pathname, offs, tag);
                err++;
            } else {
                memcpy(pkgt->os, value, sizeof(pkgt->os) - 1);
                pkgt->os[ sizeof(pkgt->os) - 1 ] = '\0';
                pkgt->flags |= PKGT_HAS_OS;
            }
            break;
            
        case 'S':
            if (pkgt->flags & PKGT_HAS_SIZE) {
                logn(LOGERR, errmg_double_tag, pathname, offs, tag);
                err++;
            } else {
                pkgt->size = atoi(value);
                pkgt->flags |= PKGT_HAS_SIZE;
            }
            break;

        case 's':
            if (pkgt->flags & PKGT_HAS_FSIZE) {
                logn(LOGERR, errmg_double_tag, pathname, offs, tag);
                err++;
            } else {
                pkgt->size = atoi(value);
                pkgt->flags |= PKGT_HAS_FSIZE;
            }
            break;
            
        case 'T':
            if (pkgt->flags & PKGT_HAS_BTIME) {
                logn(LOGERR, errmg_double_tag, pathname, offs, tag);
                err++;
            } else {
                pkgt->btime = atoi(value);
                pkgt->flags |= PKGT_HAS_BTIME;
            }
            break;
            
        default:
            logn(LOGERR, "%s:%ld: unknown tag '%c'", pathname, offs, tag);
            n_assert(0);
    }
    
    return err == 0;
}

#if 0        
static void pkgtags_clean(struct pkgtags_s *pkgt) 
{
    if (pkgt->flags & PKGT_HAS_REQ)
        if (pkgt->reqs)
            n_array_free(pkgt->reqs);

    if (pkgt->flags & PKGT_HAS_CAP)
        if (pkgt->caps)
            n_array_free(pkgt->caps);

    if (pkgt->flags & PKGT_HAS_CNFL)
        if (pkgt->cnfls)
            n_array_free(pkgt->cnfls);

    if (pkgt->flags & PKGT_HAS_FILES)
        if (pkgt->pkgfl) 
            n_array_free(pkgt->pkgfl);
    
    if (pkgt->pkguinf) 
        pkguinf_free(pkgt->pkguinf);
        pkgt->pkguinf = 0;
    

    memset(pkgt, 0, sizeof(*pkgt));
    pkgt->caps = pkgt->reqs = pkgt->cnfls = pkgt->pkgfl = NULL;
    pkgt->pkguinf = NULL;
}
#endif    

static
struct pkg *pkg_ldtags(struct pkg *pkg,
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
        
        if (!parse_evr(pkgt->evr, &epoch, &ver, &rel))
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
        pkg = pkg_new_ext(pkgt->name, epoch, ver, rel, arch, os, 
                          pkgt->size, pkgt->fsize, pkgt->btime);

        if (pkg == NULL) {
            logn(LOGERR, _("error reading %s's data"), pkgt->name);
            return NULL;
        }
        
    } else {
        pkg->size = pkgt->size;
        pkg->fsize = pkgt->fsize;
        pkg->btime = pkgt->btime;
        
        
        if (os && pkg->os == NULL)
            pkg->os = n_strdup(os);

        if (arch && pkg->arch == NULL)
            pkg->arch = n_strdup(arch);
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
        if (n_array_size(pkgt->pkgfl) == 0) {
            n_array_free(pkgt->pkgfl);
            pkgt->pkgfl = NULL;
        } else {
            pkg->fl = pkgt->pkgfl;
            n_array_sort(pkg->fl);
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


static 
int restore_pkg_fields_v0_17(tn_stream *st, uint32_t *size, uint32_t *fsize,
                             uint32_t *btime, uint32_t *groupid) 
{
    uint8_t n, dummy;

    
    n_stream_read_uint8(st, &n);
    
    n_stream_read_uint32(st, size);
    n_stream_read_uint32(st, fsize);
    n_stream_read_uint32(st, btime);
    n_stream_read_uint32(st, groupid);
    n_stream_read_uint8(st, &dummy);               /* eat '\n' */
    return n == 4;
}

