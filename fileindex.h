/* $Id$ */
#ifndef POLDEK_FILEINDEX_H
#define POLDEK_FILEINDEX_H

#include <trurl/nhash.h>
#include <trurl/nmalloc.h>

#include "pkgfl.h"

struct pkg_file_cnfl {
    uint8_t       shared;
    struct pkg    *pkg0;
    struct pkg    *pkg1;
    char          msg[0];
};

struct file_index {
    tn_hash   *dirs;             /* dirname => tn_array *files */
    tn_hash   *cnflh;
    tn_alloc  *na;
};

struct file_index *file_index_new(int nelem);
void file_index_free(struct file_index *fi);

void file_index_setup(struct file_index *fi); 

void *file_index_add_dirname(struct file_index *fi, const char *dirname);


int file_index_add_basename(struct file_index *fi, void *fidx_dir,
                            struct flfile *flfile,
                            struct pkg *pkg);

void file_index_setup_idxdir(void *fdn);

int file_index_remove(struct file_index *fi, const char *dirname,
                      const char *basename,
                      struct pkg *pkg);

int file_index_lookup(const struct file_index *fi,
                      const char *apath, int apath_len, 
                      struct pkg *pkgs[], int size);

int file_index_find_conflicts(const struct file_index *fi, int strict);
int file_index_report_conflicts(const struct file_index *fi, tn_array *pkgs);
int file_index_report_orphans(const struct file_index *fi, tn_array *pkgs);
int file_index_report_semiorphans(const struct file_index *fi, tn_array *pkgs);
#endif /* POLDEK_FILEINDEX_H */
    
    
