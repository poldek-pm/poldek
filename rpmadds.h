/* $Id$ */
#ifndef POLDEK_RPM_ADDS_H
#define POLDEK_RPM_ADDS_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdint.h>
#include <rpm/rpmlib.h>


#ifdef HAVE_RPM_4_0_4           /* missing prototypes in public headers */
int headerGetRawEntry(Header h, int_32 tag,
                      /*@null@*/ /*@out@*/ hTYP_t type,
                      /*@null@*/ /*@out@*/ hPTR_t * p, 
                      /*@null@*/ /*@out@*/ hCNT_t c);
char ** headerGetLangs(Header h);
#endif

/* rpmlib prototype */
int rpmvercmp(const char * one, const char * two);


#define rpm_headerIsSource(h)  headerIsEntry((h), RPMTAG_SOURCEPACKAGE)
int rpm_headerReadFD(FD_t fdt, Header *hdr, const char *path);
int rpm_headerReadFile(const char *path, Header *hdr);

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
