/* $Id$ */
#ifndef POLDEK_RPM_ADDS_H
#define POLDEK_RPM_ADDS_H

#include <stdint.h>

#include <rpm/rpmlib.h>
  
void rpm_headerEntryFree(void *e, int type);


/* calculates size of RPM_STRING_ARRAY_TYPE entry */
int rpm_headerRSATSize(void *e, int count, int type);

/*
  Extracts epoch, version and release from "epoch:version-release" string
  Returns length of version + release. WARN: evrstr is modified.
*/
int parse_evr(char *evrstr, int32_t *epoch, char **version, char **release);

int rpmhdr_nevr(Header h, char **name,
                uint32_t **epoch, char **version, char **release);

#endif /* RPM_ADDS_H */
