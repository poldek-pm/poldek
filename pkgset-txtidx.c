/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zlib.h>
#include <rpm/rpmlib.h>
#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nbuf.h>

#include <vfile/vfile.h>

#include "log.h"
#include "depdirs.h"
#include "rpmadds.h"
#include "misc.h"
#include "capreq.h"
#include "pkg.h"
#include "pkgu.h"
#include "pkgset-def.h"
#include "pkgset.h"
#include "pkgset-load.h"

static char *filefmt_version = "0.3.1";

#define PKGT_HAS_NAME  (1 << 0)
#define PKGT_HAS_EVR   (1 << 1)
#define PKGT_HAS_PATH  (1 << 2)
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
    char       path[PATH_MAX];
    tn_array   *caps;
    tn_array   *reqs;
    tn_array   *cnfls;
    tn_array   *pkgfl;
    off_t      other_files_offs; /* non dep files tag off_t */
    
    struct pkguinf *pkguinf;
    off_t      pkguinf_offs;
};

static const char depdirs_tag[] = "DEPDIRS: ";

static
int parse_capreq_tag(char *str, tn_array *capreqs, int addbastards);
static struct capreq *parse_capreq_token(char *token, int addbastard);

static
int add2pkgtags(struct pkgtags_s *pkgt, char tag, char *value,
                const char *pathname, int nline, int addbastards);
static
void pkgtags_clean(struct pkgtags_s *pkgt);

static
struct pkg *pkg_new_from_tags(struct pkgtags_s *pkgt);


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


int pkgset_load_txtidx(struct pkgset *ps, int ldflags, const char *pathname)
{
    struct pkgtags_s pkgtags;
    char *line_bufs[2], *line;
    int   line_bufs_size[2], *line_size, last_value_len = 0;
    char  last_tag = '\0';
    char *last_value = NULL, *last_value_endp = NULL;
    int   nerr = 0, n, nline, nread;
    struct vfile *vf;
    int flag_addbastards = 1, flag_fullflist = 0, flag_lddesc = 0;

    if (ldflags & PKGSET_IDX_LDSKIPBASTS) 
        flag_addbastards = 0;

    if (ldflags & PKGSET_IDX_LDFULLFLIST)
        flag_fullflist = 1;

    if (ldflags & PKGSET_IDX_LDDESC)
        flag_lddesc = 1;

    
    if ((vf = vfile_open(pathname, VFT_STDIO, VFM_RO | VFM_CACHE)) == NULL) 
        return 0;
    
    n = 0;
    nline = 0;
    
    last_value_endp = NULL;

    memset(&pkgtags, 0, sizeof(pkgtags));
    pkgtags.flags = 0;
    

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
        if (*line == '#') {
            
            if (nline == 1) {
                char *p;
                int lnerr = 0;
                
                if ((p = strstr(line, "poldeksindex")) == NULL) {
                    lnerr++;
                    
                } else {
                    p += strlen("poldeksindex");
                    p = eatws(p);
                    if (*p != 'v')
                        lnerr++;
                    else 
                        p++;
                }

                if (lnerr) {
                    log(LOGERR, "%s: not a poldek index file\n", pathname);
                    nerr++;
                    goto l_end;
                }
                
                
                if (strncmp(p, filefmt_version, strlen(filefmt_version)) != 0){
                    log(LOGERR, "%s: not a poldek index file\n", pathname);
                    nerr++;
                    goto l_end;
                }
            }
            
        } else if (*line == '%') {
            while (nread && line[nread - 1] == '\n')
                line[--nread] = '\0';
            line++;
            
            if (strncmp(line, depdirs_tag, strlen(depdirs_tag)) == 0) {
                char *dir;
                n_assert(ps->depdirs == NULL);

                ps->depdirs = n_array_new(16, free, (tn_fn_cmp)strcmp);
                p = line + strlen(depdirs_tag);
                p = eatws(p);
                while ((dir = next_tokn(&p, ':', NULL))) 
                    n_array_push(ps->depdirs, strdup(dir));
                n_array_push(ps->depdirs, strdup(p));
                
                if (n_array_size(ps->depdirs)) {
                    n_array_sort(ps->depdirs);
                    init_depdirs(ps->depdirs);
                }
            }
            
        } else if (*line == '\n') {        /* empty line -> end of record */
            struct pkg *pkg;
            if (last_value_endp) {
                if (!add2pkgtags(&pkgtags, last_tag, last_value,
                                 pathname, nline, flag_addbastards)) {
                    nerr++;
                    goto l_end;
                }
                
                DBGMSG("INSERT %c = %s\n", last_tag, last_value);
                last_value_endp = NULL;
            }

            DBGMSG("\n\nEOR\n");
            pkg = pkg_new_from_tags(&pkgtags);
            if (pkg) {
                pkg->pkg_stream = vf->vf_stream;
                n_array_push(ps->pkgs, pkg);
            }
            
            pkgtags_clean(&pkgtags);
            
        } else if (*line == ' ') {      /* continuation */
            int nleft;
            
            log(LOGERR, "%s:%d: syntax error\n", pathname, nline);
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
                log(LOGERR, "%s:%d: ':' expected\n", pathname, nline);
                nerr++;
                goto l_end;
            }

            if (last_value_endp) {
                DBGMSG("INSERT %c = %s\n", last_tag, last_value);
                add2pkgtags(&pkgtags, last_tag, last_value, pathname,
                            nline, flag_addbastards);
            }

            if (*line == 'L') { /* files */
                pkgtags.pkgfl = pkgfl_restore_f(vf->vf_stream);
                if (pkgtags.pkgfl)
                    pkgtags.flags |= PKGT_HAS_FILES;
                last_value_endp = NULL;
                continue;
                
            } else if (*line == 'l') {
                
                if (flag_fullflist == 0) {
                    pkgtags.other_files_offs = ftell(vf->vf_stream);
                    pkgfl_skip_f(vf->vf_stream);
                    
                } else {
                    tn_array *fl;
                    int i;

                    fl = pkgfl_restore_f(vf->vf_stream);

                    if (pkgtags.pkgfl == NULL) {
                        pkgtags.pkgfl = fl;
                        
                    } else {
                        for (i=0; i<n_array_size(fl); i++) 
                            n_array_push(pkgtags.pkgfl, n_array_nth(fl, i));
                        
                        n_array_free(fl);
                    }
                    
                }
                
                last_value_endp = NULL;
                continue;
                
            } else if (*line == 'U') { /* pkguinf binary data */

                if (flag_lddesc) {
                    pkgtags.pkguinf = pkguinf_restore(vf->vf_stream, 0);
                    pkgtags.pkguinf_offs = 0;
                } else {
                    pkgtags.pkguinf_offs = ftell(vf->vf_stream);
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
    
    pkgtags_clean(&pkgtags);
    free(line_bufs[0]);
    free(line_bufs[1]);

    if (nerr == 0 && n_array_size(ps->pkgs) == 0) {
        msg(2, "%s: empty(?)\n", pathname);
        nerr = 1;
    }

    if (nerr) {
        vfile_close(vf);
        ps->vf = NULL;
        
    } else {
        ps->vf = vf;
    }
    
    return nerr ? 0 : n_array_size(ps->pkgs);
}


int pkgset_update_txtidx(const char *pathname)
{
    struct vfile *vf;
    
    if ((vf = vfile_open(pathname, VFT_STDIO, VFM_RO | VFM_NORM)) == NULL) 
        return 0;
    
    vfile_close(vf);
    return 1;
}


#define sizeof_pkgt(memb) (sizeof((pkgt)->memb) - 1)
static
int add2pkgtags(struct pkgtags_s *pkgt, char tag, char *value,
                const char *pathname, int nline, int addbastards) 
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


        case 'F':
            if (pkgt->flags & PKGT_HAS_PATH) {
                log(LOGERR, "%s:%d: double path tag\n", pathname, nline);
                err++;
            } else {
                memcpy(pkgt->path, value, sizeof(pkgt->path)-1);
                pkgt->evr[sizeof(pkgt->path)-1] = '\0';
                pkgt->flags |= PKGT_HAS_PATH;
            }
            break;
            
        case 'P':
            if (pkgt->flags & PKGT_HAS_CAP) {
                log(LOGERR, "%s:%d: double cap tag\n", pathname, nline);
                err++;
                break;
            }
            
            pkgt->caps = capreq_arr_new();
            parse_capreq_tag(value, pkgt->caps, addbastards);
            if (n_array_size(pkgt->caps) == 0) {
                n_array_free(pkgt->caps);
                pkgt->caps = NULL;
                log(LOGERR, "%s:%d: syntax error while parsing P tag\n",
                    pathname, nline);
                err++;
            }
            pkgt->flags |= PKGT_HAS_CAP;
            break;

        case 'R':
            if (pkgt->flags & PKGT_HAS_REQ) {
                log(LOGERR, "%s:%d: double req tag\n", pathname, nline);
                err++;
                break;
            }

            pkgt->reqs = capreq_arr_new();
            parse_capreq_tag(value, pkgt->reqs, addbastards);
            if (n_array_size(pkgt->reqs) == 0) {
                n_array_free(pkgt->reqs);
                pkgt->reqs = NULL;
                log(LOGERR, "%s:%d: syntax error while parsing R tag\n",
                    pathname, nline);
                err++;
            }
            
            pkgt->flags |= PKGT_HAS_REQ;
            break;
            
        case 'C':
            if (pkgt->flags & PKGT_HAS_CNFL) {
                log(LOGERR, "%s:%d: double cnfl tag\n", pathname, nline);
                err++;
                break;
            }

            pkgt->cnfls = capreq_arr_new();
            parse_capreq_tag(value, pkgt->cnfls, addbastards);
            if (n_array_size(pkgt->cnfls) > 0) {
                pkgt->flags |= PKGT_HAS_CNFL;
            } else {
                n_array_free(pkgt->cnfls);
                pkgt->cnfls = NULL;
                if (addbastards) {
                    log(LOGERR, "%s:%d: syntax error while parsing C tag\n",
                        pathname, nline);
                    err++;
                }
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
    char *path;
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

    path = (pkgt->flags & PKGT_HAS_PATH) ? pkgt->path : NULL;

    
    pkg = pkg_new(pkgt->name, epoch, version, release, pkgt->arch,
                  pkgt->size, pkgt->btime, path);
    
    
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

/*
  token := <name> ([LWS <rel> LWS <evr>]) 
  rel   := ">=" | "<=" | "="
  evr   := [<epoch>:]<version>[-<release>]
*/
static
struct capreq *parse_capreq_token(char *token, int addbastard)
{
    char *name, *evr;
    int32_t flags;
    char *s, *rel;

    name = NULL;
    evr = NULL;
    flags = 0;
    
    DBGMSG("capreqtoken: %s -> ", token);

    token = eatws(token);
    
    if (*token == '\0')
        return NULL;

    if ((s = strchr(token, ' ')) != NULL) /* looking space after name */
        *s++ = '\0';
    
    name = token;
    if (*name == '!') {          /* bastard */
        if (!addbastard) 
            return NULL;
        
        flags |= CAPREQ_PLDEKBAST;
        name++;
    }
    
    if (*name == '*') {          /* prereq */
        flags |= CAPREQ_PREREQ;
        name++;
    }

    if (*name == '^') {          /* uninstall prereq */
        flags |= CAPREQ_PREREQ_UN;
        name++;
    }

    token = s;
    if (token == NULL)
        return capreq_new_evr(name, evr, flags);

    token = eatws(token);
    
    if (*token == '\0')
        return capreq_new_evr(name, evr, flags);

    if ((s = strchr(token, ' ')) == NULL) /* lookup space after relation */
        goto l_inv_rel;
    else {
        *s++ = '\0';
        rel = token;
        token = s;

        token = eatws(token);

        if ((s = strchr(token, ' '))) 
            *s = '\0';
            
        evr = token;

        if (evr == NULL || *evr == '\0') {
            DBGMSG("syntax error: evr missing\n");
            return 0;
        }
    }
    
    if (*rel == '=') {                                  /*  =  */
        if (*(rel+1) == '\0')               
            flags |= REL_EQ;
        else
            goto l_inv_rel;
        
    } else if (*rel == '>' || *rel == '<') {             /* [<>]= */
        flags |= (*rel == '<') ? REL_LT : REL_GT;
        
        rel++;
        
        if (*rel == '=' && *(rel+1) == '\0')
            flags |= REL_EQ;
        
        else if (*rel != '\0')
            goto l_inv_rel;
        
    } else {
 l_inv_rel:
        log(LOGERR, "syntax error: invalid relation\n");
        return NULL;
    }

    return capreq_new_evr(name, evr, flags);
}


static
int parse_capreq_tag(char *str, tn_array *capreqs, int addbastards) 
{
    struct capreq *pr;
    char *depstr, *p, *q;
    int n = 0;
    
    p = str;
    while (p) {
        if ((q = strchr(p, ','))) 
            *q++ = '\0';
        
        depstr = p;
        p = q;
        
        DBGMSG("capreq = %p %s\n", depstr, depstr);
        if ((pr = parse_capreq_token(depstr, addbastards))) {
            n_array_push(capreqs, pr);
            n++;
        }
    }
    
    return n;
}


static
int fprintf_pkg(const struct pkg *pkg, FILE *stream, int nodesc)
{
    int len;

    fprintf(stream, "N: %s\n", pkg->name);
    if (pkg->epoch)
        fprintf(stream, "V: %d:%s-%s\n", pkg->epoch, pkg->ver, pkg->rel);
    else 
        fprintf(stream, "V: %s-%s\n", pkg->ver, pkg->rel);
    
    fprintf(stream, "A: %s\n", pkg->arch);
    fprintf(stream, "S: %u\n", pkg->size);
    fprintf(stream, "T: %u\n", pkg->btime);
    
    if (pkg->caps && n_array_size(pkg->caps)) {
        int i, tagprinted = 0;
        
        n_array_sort_ex(pkg->caps, (tn_fn_cmp)capreq_cmp_name_evr);
        for (i=0; i<n_array_size(pkg->caps); i++) {
            char prbuf[1024];
            struct capreq *cap;

            cap = n_array_nth(pkg->caps, i);
            if (pkg_eq_capreq(pkg, cap))
                continue;

            if (!tagprinted) {
                len += fprintf(stream, "P: ");
                tagprinted = 1;
            }
            
            capreq_snprintf(prbuf, sizeof(prbuf), cap);
            prbuf[sizeof(prbuf) - 1] = '\0';
            
            fprintf(stream, prbuf);
            if (i < n_array_size(pkg->caps) - 1)
                fprintf(stream, ", ");
        }
        if (tagprinted)
            fprintf(stream, "\n");
    }

    
    if (pkg->reqs && n_array_size(pkg->reqs)) {
        int i;

        n_array_sort_ex(pkg->reqs, (tn_fn_cmp)capreq_cmp_name_evr);
        len += fprintf(stream, "R: ");
        for (i=0; i<n_array_size(pkg->reqs); i++) {
            char prbuf[1024*100];
            capreq_snprintf(prbuf, sizeof(prbuf), n_array_nth(pkg->reqs, i));
            prbuf[sizeof(prbuf) - 1] = '\0';
            fprintf(stream, prbuf);
            if (i < n_array_size(pkg->reqs) - 1)
                fprintf(stream, ", ");
        }
        fprintf(stream, "\n");
    }

    if (pkg->cnfls && n_array_size(pkg->cnfls)) {
        int i;
        
        n_array_sort_ex(pkg->cnfls, (tn_fn_cmp)capreq_cmp_name_evr);
        len += fprintf(stream, "C: ");
        for (i=0; i<n_array_size(pkg->cnfls); i++) {
            char prbuf[1024*100];
            capreq_snprintf(prbuf, sizeof(prbuf), n_array_nth(pkg->cnfls, i));
            prbuf[sizeof(prbuf) - 1] = '\0';
            fprintf(stream, prbuf);
            if (i < n_array_size(pkg->cnfls) - 1)
                fprintf(stream, ", ");
        }
        fprintf(stream, "\n");
    }

    if (pkg->fl) {
        fprintf(stream, "L:\n");
        pkgfl_store_f(pkg->fl, stream, PKGFL_DEPDIRS);
    
        fprintf(stream, "l:\n");
        pkgfl_store_f(pkg->fl, stream, PKGFL_NOTDEPDIRS);
    }

    if (!nodesc && pkg_has_ldpkguinf(pkg)) {
        fprintf(stream, "U:\n");
        pkguinf_store(pkg->pkg_pkguinf, stream);
        fprintf(stream, "\n");
    }
    
    fprintf(stream, "\n");
    return 1;
}

static 
void put_fheader(FILE *stream, const struct pkgset *ps) 
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
            datestr, n_array_size(ps->pkgs));
}


static 
void put_tocfheader(FILE *stream, const struct pkgset *ps) 
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
            datestr, n_array_size(ps->pkgs));
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

int pkgset_create_txtidx(struct pkgset *ps, const char *pathname, int nodesc)
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
    
    put_fheader(vf->vf_stream, ps);
    put_tocfheader(vf_toc->vf_stream, ps);
    
    if (ps->depdirs && n_array_size(ps->depdirs)) {
        fprintf(vf->vf_stream, "%%%s", depdirs_tag);
        for (i=0; i<n_array_size(ps->depdirs); i++) {
            fprintf(vf->vf_stream, "%s%c", (char*)n_array_nth(ps->depdirs, i),
                    i + 1 == n_array_size(ps->depdirs) ? '\n':':');
        }
        
        init_depdirs(ps->depdirs);
    }
    
    n_array_sort(ps->pkgs);
    
    for (i=0; i<n_array_size(ps->pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->pkgs, i);
        
        fprintf_pkg(pkg, vf->vf_stream, nodesc);
        fprintf(vf_toc->vf_stream, "%s\n", pkg_snprintf_s(pkg));
    }
    

    vfile_close(vf);
    vfile_close(vf_toc);
    return 1;
}
