/* $Id$ */
#ifndef POLDEK_PKGMISC_H
#define POLDEK_PKGMISC_H

int parse_evr(char *evrstr, int32_t *epoch,
                     const char **ver, const char **rel);
int parse_nevr(char *nevrstr, const char **name,
                      int32_t *epoch, const char **ver, const char **rel);

#endif

