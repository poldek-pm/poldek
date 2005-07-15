#ifndef PKGDIR_METADATA_LOAD_H
#define PKGDIR_METADATA_LOAD_H

void metadata_loadmod_init(void);
void metadata_loadmod_destroy(void);


struct repomd_ent {
    char type[32];
    //char checksum[64];
    //char checksum_type[8];
    time_t ts;
    char location[0];
};

/* name => repomd_ent */
tn_hash *metadata_load_repomd(const char *path);
tn_array *metadata_load_primary(tn_alloc *na, const char *path);



#endif
