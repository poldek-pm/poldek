/* $Id$ */
#ifndef POLDEK_RPM_ADDS_H
#define POLDEK_RPM_ADDS_H

#include <stdint.h>

#include <rpm/rpmlib.h>

void rpm_headerEntryFree(void *e, int type);

/* CAUTION:
   No side-effects in arguments please! They are evaluated MANY times.
*/
#define rpm_headerEntryFree(e, t)                                     \
    do { if (e &&                                                     \
            (t == RPM_STRING_ARRAY_TYPE || t == RPM_I18NSTRING_TYPE)) \
                free(e);                                              \
    } while(0)


/* calculates size of RPM_STRING_ARRAY_TYPE entry */
int rpm_headerRSATSize(void *e, int count, int type);


/*
  Extracts epoch, version and release from "epoch:version-release" string
  Returns length of version + release. WARN: evrstr is modified.
*/
int parse_evr(char *evrstr, int32_t *epoch, char **version, char **release);

int rpmhdr_nevr(Header h, char **name,
                uint32_t **epoch, char **version, char **release);

int rpmfile_nevr(const char *path, char **name,
                 uint32_t **epoch, char **version, char **release);

#endif /* RPM_ADDS_H */
