/* $Id$ */
#ifndef POLDEK_RPM_ADDS_H
#define POLDEK_RPM_ADDS_H

#include <stdint.h>

#include <rpm/rpmlib.h>

/* rpmlib prototype */
int	rpmvercmp(const char * one, const char * two);
  
void rpm_headerEntryFree(void *e, int type);


/* calculates size of RPM_STRING_ARRAY_TYPE entry */
int rpm_headerRSATSize(void *e, int count, int type);

/*
  Extracts epoch, version and release from "epoch:version-release" string
  Returns length of version + release. WARN: evrstr is modified.
*/
int parse_evr(char *evrstr, int32_t *epoch, char **version, char **release);


/*
  Extracts epoch, version and release from "name-epoch:version-release" string
  Returns length of version + release. WARN: nevrstr is modified.
*/
int parse_nevr(char *nevrstr, const char **name,
               int32_t *epoch, const char **version, const char **release);


int rpmhdr_nevr(Header h, char **name,
                uint32_t **epoch, char **version, char **release);

char *rpmhdr_snprintf(char *buf, size_t size, Header h);


#endif /* RPM_ADDS_H */
