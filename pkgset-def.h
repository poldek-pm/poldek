/* $Id$ */
#ifndef  POLDEK_PKGSETDEF_H
#define  POLDEK_PKGSETDEF_H

#include <obstack.h>

#include <trurl/narray.h>

#include "fileindex.h"
#include "capreqidx.h"


#define PKGSET_INDEXES_INIT      (1 << 16)
#define PKGSET_READFULLTXTINDEX  (1 << 17)

struct pkgset {
    tn_array           *pkgs;      /* *pkg[]    */
    tn_array           *ordered_pkgs;
    unsigned           flags;
    char               *path;      /* path | URL */
    struct vfile       *vf;        /* Packages handle */
    

    tn_array           *depdirs;   /*  char* []  */
    int                nerrors;
    
    tn_array           *rpmcaps;
    
    struct capreq_idx  cap_idx;    /* 'name'  => *pkg[]  */
    struct capreq_idx  req_idx;    /*  -"-               */
    struct capreq_idx  obs_idx;    /*  -"-               */     
    struct file_index  file_idx;   /* 'file'  => *pkg[]  */
};

int pkgset_order(struct pkgset *ps);

#endif /* POLDEK_PKGSETDEF_H*/
