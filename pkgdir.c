/* 
  Copyright (C) 2000, 2001 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_GETLINE
# define _GNU_SOURCE 1
#else
# error "getline() is needed, sorry"
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

#include <vfile/vfile.h>

#include "log.h"
#include "misc.h"
#include "rpmadds.h"
#include "pkgdir.h"
#include "pkg.h"


static char *filefmt_version = "0.4";
static const char depdirs_tag[] = "DEPDIRS: ";


#define PKGT_HAS_NAME  (1 << 0)
#define PKGT_HAS_EVR   (1 << 1)
#define PKGT_HAS_CAP   (1 << 3)
#define PKGT_HAS_REQ   (1 << 4)
#define PKGT_HAS_CNFL  (1 << 5)
#define PKGT_HAS_FILES (1 << 6)
#define PKGT_HAS_ARCH  (1 << 7)
#define PKGT_HAS_SIZE  (1 << 8)
#define PKGT_HAS_BTIME (1 << 9)

struct pkgtags_s {
    unsigned   flags;
    char       name[64];
    char       evr[64];
    char       arch[64];
    uint32_t   size;
    uint32_t   btime;
    tn_array   *caps;
    tn_array   *reqs;
    tn_array   *cnfls;
    tn_array   *pkgfl;
    off_t      other_files_offs; /* non dep files tag off_t */
    
    struct pkguinf *pkguinf;
    off_t      pkguinf_offs;
};

static
int add2pkgtags(struct pkgtags_s *pkgt, char tag, char *value,
                const char *pathname, int nline);
static
void pkgtags_clean(struct pkgtags_s *pkgt);

static
struct pkg *pkg_new_from_tags(struct pkgtags_s *pkgt);

static int check_digest(struct vfile *vf, const char *path);

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

static char *setup_pkgprefix(const char *path) 
{
    char *dn = NULL, *bn, *buf, *rpath = NULL;
    int len;

    len = strlen(path);
    buf = alloca(len + 1);
    memcpy(buf, path, len);
    buf[len] = '\0';
    
    n_basedirnam(buf, &dn, &bn);
    if (dn)
        rpath = strdup(dn);
    else
        rpath = strdup(".");

    return rpath;
}

static struct vfile *open_idx(const char *path, int vfmode,  
                              char *idx_path, int idx_path_size) 
{
    struct vfile *vf = NULL;
    
    if (path[strlen(path) - 1] != '/') {
        vf = vfile_open(path, VFT_STDIO, vfmode);
        if (idx_path) 
            strncpy(idx_path, path, idx_path_size);
        
    } else {
        
        if (idx_path == NULL) {
            idx_path = alloca(PATH_MAX);
            idx_path_size = PATH_MAX;
        }
            
        snprintf(idx_path, idx_path_size, "%s%s", path, "Packages.gz");

        if ((vf = vfile_open(idx_path, VFT_STDIO, vfmode)) == NULL) {
            snprintf(idx_path, idx_path_size, "%s%s", path, "Packages");
            vf = vfile_open(idx_path, VFT_STDIO, vfmode);
        }
    }
    
    return vf;
}



int update_pkgdir_idx(const char *path) 
{
    struct vfile *vf;
    char idxpath[PATH_MAX];
    int rc;

    vf = open_idx(path, VFM_RO | VFM_CACHE | VFM_MDUP, idxpath,
                  sizeof(idxpath));
    if (vf == NULL)
        return 0;
    
    rc = check_digest(vf, idxpath);
    vfile_close(vf);
    return rc;
}


struct pkgdir *pkgdir_new(const char *path, const char *pkg_prefix)
{
    struct pkgdir     *pkgdir = NULL;
    struct vfile      *vf;
    char              *line, *linebuf;
    int               line_size;
    int               nerr = 0, n, nline, nread;
    tn_array          *depdirs = NULL;
    char              idxpath[PATH_MAX];
    

    vf = open_idx(path, VFM_RO | VFM_CACHE, idxpath, sizeof(idxpath));
    if (vf == NULL)
        return NULL;
    
    n = 0;
    nline = 0;
    line_size = 4096;
    line = linebuf = malloc(line_size);

    while ((nread = getline(&line, &line_size, vf->vf_stream)) > 0) {
        char *p;

        nline++;
        if (nline == 1) {
            char *p;
            int lnerr = 0;
                
            if (*line != '#')
                lnerr++;
            
            else if ((p = strstr(line, "poldeksindex")) == NULL) 
                lnerr++;
                    
            else {
                p += strlen("poldeksindex");
                p = eatws(p);
                if (*p != 'v')
                    lnerr++;
                else 
                    p++;
                
                if (lnerr == 0 && strncmp(p, filefmt_version,
                                          strlen(filefmt_version)) != 0) {
                    log(LOGERR, "%s: usupported version %s (need to be %s)\n",
                        path, p, filefmt_version);
                    nerr++;
                    goto l_end;
                }
            }

            if (lnerr) {
                log(LOGERR, "%s: not a poldek index file\n", path);
                nerr++;
                goto l_end;
            }
            continue;
        }
        
	if (*line != '#' && *line != '%')
            break;
        
        if (*line == '%') {
            while (nread && line[nread - 1] == '\n')
                line[--nread] = '\0';
            line++;
            
            if (strncmp(line, depdirs_tag, strlen(depdirs_tag)) == 0) {
                char *dir;
                n_assert(depdirs == NULL);

                depdirs = n_array_new(16, free, (tn_fn_cmp)strcmp);
                p = line + strlen(depdirs_tag);
                p = eatws(p);
                
                while ((dir = next_tokn(&p, ':', NULL)) != NULL) 
                    n_array_push(depdirs, strdup(dir));
                n_array_push(depdirs, strdup(p));
                
                if (n_array_size(depdirs)) 
                    n_array_sort(depdirs);
            }
            break;              /* finish at %DEPDIRS */
        }
        
    }
    
    free(linebuf);


    if (depdirs == NULL) {
        log(LOGERR, "%s: missing %s tag\n",
            vf->vf_tmpath ? vf->vf_tmpath : path, depdirs_tag);
        nerr++;
        goto l_end;
    }
    
    
    pkgdir = malloc(sizeof(*pkgdir));

    
    if (pkg_prefix) 
        pkgdir->path = strdup(pkg_prefix);
    else 
        pkgdir->path = setup_pkgprefix(idxpath);
    
    pkgdir->idxpath = strdup(idxpath);
    pkgdir->vf = vf;
    pkgdir->depdirs = depdirs;
    n_array_ctl(pkgdir->depdirs, TN_ARRAY_AUTOSORTED);
    n_array_sort(pkgdir->depdirs);
    pkgdir->flags = PKGDIR_LDFROM_IDX;
    pkgdir->pkgs = pkgs_array_new(1024);

 l_end:

    if (nerr) {
        vfile_close(vf);
        if (depdirs)
            n_array_free(depdirs);
    }
    
    return pkgdir;
}


void pkgdir_free(struct pkgdir *pkgdir) 
{
    if (pkgdir->path) {
        free(pkgdir->path);
        pkgdir->path = NULL;
    }

    if (pkgdir->idxpath) {
        free(pkgdir->idxpath);
        pkgdir->idxpath = NULL;
    }

    if (pkgdir->depdirs) {
        n_array_free(pkgdir->depdirs);
        pkgdir->depdirs = NULL;
    }

    if (pkgdir->pkgs) {
        n_array_free(pkgdir->pkgs);
        pkgdir->pkgs = NULL;
    }

    if (pkgdir->vf) {
        vfile_close(pkgdir->vf);
        pkgdir->vf = NULL;
    }

    pkgdir->flags = 0;
    free(pkgdir);
}


int pkgdir_load(struct pkgdir *pkgdir, tn_array *depdirs, unsigned ldflags)
{
    struct pkgtags_s   pkgt;
    char               *line_bufs[2], *line;
    int                line_bufs_size[2], *line_size, last_value_len = 0;
    char               last_tag = '\0';
    char               *last_value = NULL, *last_value_endp = NULL;
    int                nerr = 0, n, nline, nread, i;
    struct             vfile *vf;
    int                flag_skip_bastards = 0, flag_fullflist = 0;
    int                flag_lddesc = 0;
    tn_array           *only_dirs;

#if 0    
    for (i=0; i<n_array_size(depdirs); i++) {
        printf("DEP %s\n", n_array_nth(depdirs, i));
    }
#endif    

    if (ldflags & PKGDIR_LD_SKIPBASTS) 
        flag_skip_bastards = 1;

    if (ldflags & PKGDIR_LD_FULLFLIST)
        flag_fullflist = 1;

    if (ldflags & PKGDIR_LD_DESC)
        flag_lddesc = 1;

    only_dirs = NULL;

    if (flag_fullflist == 0) {
        only_dirs = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
        for (i=0; i<n_array_size(depdirs); i++) {
            char *dn = n_array_nth(depdirs, i);
            if (n_array_bsearch(pkgdir->depdirs, dn) == NULL) {
                DBGMSG_F("ONLYDIR for %s: %s\n", pkgdir->path, dn);
                if (*dn == '/' && *(dn + 1) != '\0') 
                    dn++;
                n_array_push(only_dirs, dn);
            }
        }
        
        if (n_array_size(only_dirs) == 0) {
            n_array_free(only_dirs);
            only_dirs = NULL;
        }
    }
    

    vf = pkgdir->vf;
    n = 0;
    nline = 0;
    last_value_endp = NULL;

    memset(&pkgt, 0, sizeof(pkgt));
    pkgt.flags = 0;
    

    line_bufs_size[0] = line_bufs_size[1] = 4*4096;
    line_bufs[0] = malloc(line_bufs_size[0]);
    line_bufs[1] = malloc(line_bufs_size[1]);
    line = line_bufs[0];
    line_size = &line_bufs_size[0];


    while ((nread = getline(&line_bufs[n], &line_bufs_size[n],
                            vf->vf_stream)) > 0) {
        char *p, *q;

        line = line_bufs[n];
        if (++n == 2)
            n = 0;
        
        nline++;
        
        if (*line == '\n') {        /* empty line -> end of record */
            struct pkg *pkg;
            if (last_value_endp) {
                if (!add2pkgtags(&pkgt, last_tag, last_value,
                                 pkgdir->path, nline)) {
                    nerr++;
                    goto l_end;
                }
                
                DBGMSG("INSERT %c = %s\n", last_tag, last_value);
                last_value_endp = NULL;
            }

            DBGMSG("\n\nEOR\n");
            pkg = pkg_new_from_tags(&pkgt);
            if (pkg) {
                pkg->pkgdir = pkgdir;
                n_array_push(pkgdir->pkgs, pkg);
                pkg = NULL;
            }
            
            pkgtags_clean(&pkgt);
            
        } else if (*line == ' ') {      /* continuation */
            int nleft;
            
            log(LOGERR, "%s:%d: syntax error\n", pkgdir->path, nline);
            exit(EXIT_FAILURE);
            n_assert(last_value_endp);
            n_assert(last_value);
            
            nleft =  (last_value_endp - last_value);
            if (nleft > 2) {
                *last_value_endp++ = '\n';
                nleft--;
                strncat(last_value, line, nleft);
                last_value_endp[nleft] = '\0';
                last_value_endp += strlen(last_value_endp);
                *last_value_endp = '\0';
            }
            
        } else {
            while (nread && line[nread - 1] == '\n')
                line[--nread] = '\0';
            
            q = line + 1;

            if (*line == '\0' || *q != ':') {
                log(LOGERR, "%s:%d(%s): ':' expected\n", pkgdir->path, nline, line);
                nerr++;
                goto l_end;
            }

            if (last_value_endp) {
                DBGMSG("INSERT %c = %s\n", last_tag, last_value);
                add2pkgtags(&pkgt, last_tag, last_value, pkgdir->path, nline);
            }

            if (*line == 'P') {
                if (pkgt.flags & PKGT_HAS_CAP) {
                    log(LOGERR, "%s:%d: double cap tag\n", pkgdir->path, nline);
                    break;
                }
                
                pkgt.caps = capreq_arr_restore(vf->vf_stream,
                                               flag_skip_bastards);
                pkgt.flags |= PKGT_HAS_CAP;
                last_value_endp = NULL;
                continue;
                
                
            } else if (*line ==  'R') {
                if (pkgt.flags & PKGT_HAS_REQ) {
                    log(LOGERR, "%s:%d: double req tag\n", pkgdir->path, nline);
                    break;
                }
                
                pkgt.reqs = capreq_arr_restore(vf->vf_stream,
                                               flag_skip_bastards);
                pkgt.flags |= PKGT_HAS_REQ;
                last_value_endp = NULL;
                continue;
                
            } else if (*line == 'C') {
                if (pkgt.flags & PKGT_HAS_CNFL) {
                    log(LOGERR, "%s:%d: double cnfl tag\n", pkgdir->path, nline);
                    break;
                }
                
                pkgt.cnfls = capreq_arr_restore(vf->vf_stream,
                                                flag_skip_bastards);
                pkgt.flags |= PKGT_HAS_CNFL;
                last_value_endp = NULL;
                continue;
                
            } else if (*line == 'L') { /* files */
                pkgt.pkgfl = pkgfl_restore_f(vf->vf_stream, NULL);
                n_assert(pkgt.pkgfl);
                //printf("DUMP %p %d\n", pkgt.pkgfl, n_array_size(pkgt.pkgfl));
                //pkgfl_dump(pkgt.pkgfl);
                if (pkgt.pkgfl)
                    pkgt.flags |= PKGT_HAS_FILES;
                last_value_endp = NULL;
                continue;
                
            } else if (*line == 'l') {
                pkgt.other_files_offs = ftell(vf->vf_stream);
                
                if (flag_fullflist == 0 && only_dirs == NULL) {
                    pkgfl_skip_f(vf->vf_stream);
                    
                } else {
                    tn_array *fl;
                    
                    fl = pkgfl_restore_f(vf->vf_stream, only_dirs);

                    if (pkgt.pkgfl == NULL) {
                        pkgt.pkgfl = fl;
                        pkgt.flags |= PKGT_HAS_FILES;
                        
                    } else {
                        while (n_array_size(fl)) 
                            n_array_push(pkgt.pkgfl, n_array_shift(fl));
                            
                        n_array_free(fl);
                    }
                }
                
                last_value_endp = NULL;
                continue;
                
            } else if (*line == 'U') { /* pkguinf binary data */

                if (flag_lddesc) {
                    pkgt.pkguinf = pkguinf_restore(vf->vf_stream, 0);
                    pkgt.pkguinf_offs = 0;
                } else {
                    pkgt.pkguinf_offs = ftell(vf->vf_stream);
                    pkguinf_skip(vf->vf_stream);
                }
                	
                last_value_endp = NULL;
                continue;
            } 
            
            
            p = q;
            
            *q++ = '\0';
            q = eatws(q);
            n_assert(*line && *(line + 1) == '\0');

            last_tag = *line;
            last_value = q;
            last_value_len = nread - (q - line);
            last_value_endp = last_value + last_value_len;
            *last_value_endp = '\0';
        }
    }
    
 l_end:
    
    pkgtags_clean(&pkgt);
    free(line_bufs[0]);
    free(line_bufs[1]);

    if (nerr == 0 && n_array_size(pkgdir->pkgs) == 0) {
        msg(2, "%s: empty(?)\n", pkgdir->path);
        nerr = 1;
    }

    return nerr ? 0 : n_array_size(pkgdir->pkgs);
}


#define sizeof_pkgt(memb) (sizeof((pkgt)->memb) - 1)
static
int add2pkgtags(struct pkgtags_s *pkgt, char tag, char *value,
                const char *pathname, int nline) 
{
    int err = 0;
    
    switch (tag) {
        case 'N':
            if (pkgt->flags & PKGT_HAS_NAME) {
                log(LOGERR, "%s:%d: double name tag\n", pathname, nline);
                err++;
            } else {
                memcpy(pkgt->name, value, sizeof(pkgt->name)-1);
                pkgt->flags |= PKGT_HAS_NAME;
            }
            break;
            
        case 'V':
            if (pkgt->flags & PKGT_HAS_EVR) {
                log(LOGERR, "%s:%d: double evr tag\n", pathname, nline);
                err++;
            } else {
                memcpy(pkgt->evr, value, sizeof(pkgt->evr)-1);
                pkgt->evr[sizeof(pkgt->evr)-1] = '\0';
                pkgt->flags |= PKGT_HAS_EVR;
            }
            break;
            
        case 'A':
            if (pkgt->flags & PKGT_HAS_ARCH) {
                log(LOGERR, "%s:%d: double arch tag\n", pathname, nline);
                err++;
            } else {
                memcpy(pkgt->arch, value, sizeof(pkgt->arch)-1);
                pkgt->arch[sizeof(pkgt->arch)-1] = '\0';
                pkgt->flags |= PKGT_HAS_ARCH;
            }
            break;

        case 'S':
            if (pkgt->flags & PKGT_HAS_SIZE) {
                log(LOGERR, "%s:%d: double size tag\n", pathname, nline);
                err++;
            } else {
                pkgt->size = atoi(value);
                pkgt->flags |= PKGT_HAS_SIZE;
            }
            break;
            
        case 'T':
            if (pkgt->flags & PKGT_HAS_BTIME) {
                log(LOGERR, "%s:%d: double btime tag\n", pathname, nline);
                err++;
            } else {
                pkgt->btime = atoi(value);
                pkgt->flags |= PKGT_HAS_BTIME;
            }
            break;
            
        default:
            log(LOGERR, "%s:%d: unknown tag %c\n", pathname, nline, tag);
            n_assert(0);
    }
    
    return err == 0;
}

        
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
    
    pkgt->other_files_offs = 0;
    pkgt->flags = 0;
    pkgt->pkguinf_offs = 0;

    if (pkgt->pkguinf) {
        pkguinf_free(pkgt->pkguinf);
        pkgt->pkguinf = 0;
    }
}
    

static
struct pkg *pkg_new_from_tags(struct pkgtags_s *pkgt) 
{
    struct pkg *pkg;
    char *version, *release;
    int32_t epoch;
    
    if (!(pkgt->flags & (PKGT_HAS_NAME | PKGT_HAS_EVR | PKGT_HAS_ARCH)))
        return NULL;
    
    
    if (*pkgt->name == '\0' || *pkgt->evr == '\0' || *pkgt->arch == '\0') 
        return NULL;
    
    if (!parse_evr(pkgt->evr, &epoch, &version, &release))
        return NULL;
    
    if (version == NULL || release == NULL) {
        log(LOGERR, "failed to extract version and release from evr\n");
        return NULL;
    }

    pkg = pkg_new(pkgt->name, epoch, version, release, pkgt->arch,
                  pkgt->size, pkgt->btime);
    
    if (pkg == NULL) {
        log(LOGERR, "Error reading %s's data", pkgt->name);
        return NULL;
    }

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
            //pkgfl_dump(pkg->fl);
            pkgt->pkgfl = NULL;
        }
    }

    pkg->other_files_offs = pkgt->other_files_offs;
    if (pkgt->pkguinf_offs) {
        n_assert(pkg_has_ldpkguinf(pkg) == 0);
        pkg->pkg_pkguinf_offs = pkgt->pkguinf_offs;
    } else {
        pkg->pkg_pkguinf = pkgt->pkguinf;
        pkg_set_ldpkguinf(pkg);
        pkgt->pkguinf = NULL;
    }
    	
    return pkg;
}

int fprintf_pkg_caps(const struct pkg *pkg, FILE *stream) 
{
    tn_array *arr;
    int i;

    arr = n_array_new(32, NULL, NULL);
    for (i=0; i<n_array_size(pkg->caps); i++) {
        struct capreq *cr = n_array_nth(pkg->caps, i);
        if (pkg_eq_capreq(pkg, cr))
            continue;
        n_array_push(arr, cr);
    }

    if (n_array_size(arr)) 
        i = capreq_arr_store(arr, stream, "P:\n");
    else
        i = 1;
    
    n_array_free(arr);
    return i;
}
	

static
int fprintf_pkg(const struct pkg *pkg, FILE *stream, tn_array *depdirs, int nodesc)
{
    fprintf(stream, "N: %s\n", pkg->name);
    if (pkg->epoch)
        fprintf(stream, "V: %d:%s-%s\n", pkg->epoch, pkg->ver, pkg->rel);
    else 
        fprintf(stream, "V: %s-%s\n", pkg->ver, pkg->rel);
    
    fprintf(stream, "A: %s\n", pkg->arch);
    fprintf(stream, "S: %u\n", pkg->size);
    fprintf(stream, "T: %u\n", pkg->btime);

    if (pkg->caps && n_array_size(pkg->caps))
        fprintf_pkg_caps(pkg, stream);
    
    if (pkg->reqs && n_array_size(pkg->reqs)) 
        capreq_arr_store(pkg->reqs, stream, "R:\n");
    
    if (pkg->cnfls && n_array_size(pkg->cnfls)) 
        capreq_arr_store(pkg->cnfls, stream, "C:\n");
    
    if (pkg->fl && n_array_size(pkg->fl)) {
        fprintf(stream, "L:\n");
        pkgfl_store_f(pkg->fl, stream, depdirs, PKGFL_DEPDIRS);
    
        fprintf(stream, "l:\n");
        pkgfl_store_f(pkg->fl, stream, depdirs, PKGFL_NOTDEPDIRS);
    }

    if (nodesc == 0 && pkg_has_ldpkguinf(pkg)) {
        fprintf(stream, "U:\n");
        pkguinf_store(pkg->pkg_pkguinf, stream);
        fprintf(stream, "\n");
    }
    
    fprintf(stream, "\n");
    return 1;
}


static 
void put_fheader(FILE *stream, const struct pkgdir *pkgdir) 
{
    time_t t;
    char datestr[128];
    
    t = time(0);
    strftime(datestr, sizeof(datestr),
             "%a, %d %b %Y %H:%M:%S GMT", gmtime(&t));
    
    fprintf(stream,
            "# poldeksindex v%s\n"
            "# This file was generated by poldek on %s.\n"
            "# PLEASE DO *NOT* EDIT or poldek will hate you.\n"
            "# Contains %d packages\n",
            filefmt_version,
            datestr, n_array_size(pkgdir->pkgs));
}


static 
void put_tocfheader(FILE *stream, const struct pkgdir *pkgdir) 
{
    time_t t;
    char datestr[128];
    
    t = time(0);
    strftime(datestr, sizeof(datestr),
             "%a, %d %b %Y %H:%M:%S GMT", gmtime(&t));
    
    fprintf(stream,
            "# poldekstocf v%s\n"
            "# This file was generated by poldek on %s.\n"
            "# PLEASE DO *NOT* EDIT ME or poldek will hate you.\n"
            "# Contains %d packages\n",
            filefmt_version,
            datestr, n_array_size(pkgdir->pkgs));
}


static char *mktoc_pathname(char *dest, size_t size, const char *pathname) 
{
    char *ext, *bn = NULL;
    char *suffix = "-toc";

    
    if (strlen(pathname) + strlen(suffix) + 1 > size)
        return NULL;

    bn = n_basenam(pathname);
    if ((ext = strrchr(bn, '.')) == NULL) {
        snprintf(dest, size, "%s%s", pathname, suffix);
    } else {
        int len = ext - pathname + 1;
        n_assert(len + strlen(suffix) + strlen(ext) + 1 < size);
        n_strncpy(dest, pathname, len);
        strcat(dest, suffix);
        strcat(dest, ext);
        dest[size - 1] = '\0';
    }

    return dest;
}

static int check_digest(struct vfile *vf, const char *path) 
{
    char            md1[128], md2[128];
    int             rc, fd, md2_size, md1_size = sizeof(md1);
    off_t           offs;
    

    msg(1, "Verifying file %s...", path);
    
    offs = ftell(vf->vf_stream);
    if (fseek(vf->vf_stream, 0L, SEEK_SET) != 0) {
        log(LOGERR, "%s: fseek(0): %d %m\n", path, offs);
        return 0;
    }

    mhexdigest(vf->vf_stream, md1, &md1_size);

    if (fseek(vf->vf_stream, offs, SEEK_SET) != 0) {
        log(LOGERR, "%s: fseek(%ld): %m\n", path, offs);
        return 0;
    }
    
    
    if (md1_size <= 0) 
        return 0;

    n_assert(vf->vf_mdtmpath);
    
    if ((fd = open(vf->vf_mdtmpath, O_RDONLY)) < 0) {
        log(LOGERR, "open %s: %m\n", vf->vf_mdtmpath);
        return 0;
    }
    	
    md2_size = read(fd, md2, sizeof(md2));
    close(fd);
    
    rc = md1_size == md2_size && strcmp(md1, md2) == 0;
    msg(1, "_ %s\n", rc ? "OK" : "BAD");
    return rc;
}


static int creat_digest_file(const char *pathname) 
{
    struct vfile    *vf;
    unsigned char   md[128];
    char            path[PATH_MAX], *ext;
    int             md_size = sizeof(md);
    
    if ((vf = vfile_open(pathname, VFT_STDIO, VFM_RO)) == NULL)
        return 0;

    if ((ext = strrchr(n_basenam(pathname), '.')) == NULL)
        snprintf(path, sizeof(path), "%s%s", pathname, ".md");
    else {
        int len = ext - pathname + 1;
        n_strncpy(path, pathname, len);
        strcat(path, ".md");
    }
    	
    mhexdigest(vf->vf_stream, md, &md_size);
    vfile_close(vf);

    if (md_size) {
        if ((vf = vfile_open(path, VFT_STDIO, VFM_RW)) == NULL)
            return 0;
        fprintf(vf->vf_stream, "%s", md);
        vfile_close(vf);
    }

    return md_size;
}

int pkgdir_create_idx(struct pkgdir *pkgdir, const char *pathname, int nodesc)
{
    struct vfile *vf, *vf_toc;
    char tocpath[PATH_MAX];
    int i;
    

    if (mktoc_pathname(tocpath, sizeof(tocpath), pathname) == NULL) {
        log(LOGERR, "Cannot prepare tocpath!?");
        return 0;
    }
    
    if ((vf = vfile_open(pathname, VFT_STDIO, VFM_RW)) == NULL)
        return 0;

    if ((vf_toc = vfile_open(tocpath, VFT_STDIO, VFM_RW)) == NULL) {
        vfile_close(vf);
        return 0;
    }
    
    put_fheader(vf->vf_stream, pkgdir);
    put_tocfheader(vf_toc->vf_stream, pkgdir);
    
    if (pkgdir->depdirs && n_array_size(pkgdir->depdirs)) {
        fprintf(vf->vf_stream, "%%%s", depdirs_tag);
        
        for (i=0; i<n_array_size(pkgdir->depdirs); i++) {
            fprintf(vf->vf_stream, "%s%c",
                    (char*)n_array_nth(pkgdir->depdirs, i),
                    i + 1 == n_array_size(pkgdir->depdirs) ? '\n':':');
        }
    }
    
    n_array_sort(pkgdir->pkgs);
    
    for (i=0; i<n_array_size(pkgdir->pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);
        
        fprintf_pkg(pkg, vf->vf_stream, pkgdir->depdirs, nodesc);
        fprintf(vf_toc->vf_stream, "%s\n", pkg_snprintf_s(pkg));
    }


    vfile_close(vf);
    vfile_close(vf_toc);
    
    return creat_digest_file(pathname);
}


int load_dir(const char *dirpath, tn_array *pkgs)
{
    struct dirent  *ent;
    struct stat    st;
    DIR            *dir;
    int            n;

    
    if ((dir = opendir(dirpath)) == NULL) {
	log(LOGERR, "opendir %s: %m\n", dirpath);
	return 0;
    }

    n = 0;
    while( (ent = readdir(dir)) ) {
        char path[PATH_MAX];
        
        if (fnmatch("*.rpm", ent->d_name, 0) != 0) 
            continue;

        if (fnmatch("*.src.rpm", ent->d_name, 0) == 0) 
            continue;

        snprintf(path, sizeof(path), "%s/%s", dirpath, ent->d_name);
        
        if (stat(path, &st) != 0) {
            log(LOGERR, "stat %s: %m", path);
            continue;
        }
        
        if (S_ISREG(st.st_mode)) {
            Header h;
            FD_t fdt;
            
            if ((fdt = Fopen(path, "r")) == NULL) {
                log(LOGERR, "open %s: %s\n", path, rpmErrorString());
                continue;
            }
            
            if (rpmReadPackageHeader(fdt, &h, NULL, NULL, NULL) != 0) {
                log(LOGERR, "%s: read header failed, skiped\n", path);
                
            } else {
                struct pkg *pkg;
                 

                if (headerIsEntry(h, RPMTAG_SOURCEPACKAGE)) /* omit src.rpms */
                    continue;

                if ((pkg = pkg_ldhdr(h, path, PKG_LDWHOLE))) {
                    pkg->pkg_pkguinf = pkguinf_ldhdr(h);
                    pkg_set_ldpkguinf(pkg);
                    n_array_push(pkgs, pkg);
                    n++;
                }
                headerFree(h);
            }
            Fclose(fdt);
            
            if (n && n % 100 == 0) 
                msg_l(2, "_%d..", n);
            	
        }
    }

    if (n)
        msg_l(2, "_%d\n", n);
    closedir(dir);
    return n;
}


static void is_depdir_req(const struct capreq *req, tn_array *depdirs) 
{
    if (capreq_is_file(req)) {
        const char *reqname;
        char *p;
        int reqlen;
        
        reqname = capreq_name(req);
        reqlen = strlen(reqname);
        
        p = strrchr(reqname, '/');
        
        if (p != reqname) {
            char *dirname;
            int len;

            len = p - reqname;
            dirname = alloca(len + 1);
            memcpy(dirname, reqname, len);
            dirname[len] = '\0';
            p = dirname;

            
        } else if (*(p+1) != '\0') {
            char *dirname;
            dirname = alloca(reqlen + 1);
            memcpy(dirname, reqname, reqlen + 1);
            p = dirname;
        }

        if (*(p+1) != '\0' && *p == '/')
            p++;
        
        if (n_array_bsearch(depdirs, p) == NULL) {
            n_array_push(depdirs, strdup(p));
            n_array_sort(depdirs);
        }
    }
}


static void pkgdir_setup_depdirs(struct pkgdir *pkgdir) 
{
    int i;

    n_assert(pkgdir->depdirs == NULL);
    pkgdir->depdirs = n_array_new(16, free, (tn_fn_cmp)strcmp);
    n_array_ctl(pkgdir->depdirs, TN_ARRAY_AUTOSORTED);
    
    
    for (i=0; i<n_array_size(pkgdir->pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);

        if (pkg->reqs) 
            n_array_map_arg(pkg->reqs, (tn_fn_map2) is_depdir_req,
                            pkgdir->depdirs);
    }
}


struct pkgdir *pkgdir_load_dir(const char *path) 
{
    struct pkgdir *pkgdir = NULL;
    tn_array      *pkgs;

    pkgs = pkgs_array_new(1024);
    
    if (load_dir(path, pkgs) >= 0) {
        pkgdir = malloc(sizeof(*pkgdir));
    
        pkgdir->path = strdup(path);
        pkgdir->idxpath = NULL;
        pkgdir->depdirs = NULL;
        pkgdir->pkgs = pkgs;
        pkgdir->flags = PKGDIR_LDFROM_DIR;
        pkgdir->vf = NULL;
        
        if (n_array_size(pkgs)) 
            pkgdir_setup_depdirs(pkgdir);
    }
    
    return pkgdir;
}

int pkgdir_isremote(struct pkgdir *pkgdir)
{
    return vfile_url_type(pkgdir->path) != VFURL_PATH;
}
