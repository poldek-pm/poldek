/* $Id$ */
#ifndef POLDEK_FILEINDEX_H
#define POLDEK_FILEINDEX_H

#include <obstack.h>
#include <trurl/nhash.h>

#include "pkgfl.h"

struct file_index {
    tn_hash  *dirs;             /* dirname => tn_array *files */
    struct   obstack obs;
};
    

int  file_index_init(struct file_index *fi, int nelem);
void file_index_destroy(struct file_index *fi);

void file_index_setup(struct file_index *fi); 

void *file_index_add_dirname(struct file_index *fi, const char *dirname);

int file_index_add_basename(struct file_index *fi, void *fidx_dir,
                            struct flfile *flfile,
                            struct pkg *pkg);

int  file_index_lookup(struct file_index *fi, char *path,
                       struct pkg *pkgs[], int size);

int file_index_find_conflicts(const struct file_index *fi, tn_array *errs,
                              int strict);

#endif /* POLDEK_FILEINDEX_H */
    
    
