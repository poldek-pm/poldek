#ifndef POLDEK_PKG_VER_CMP_H
#define POLDEK_PKG_VER_CMP_H

/* rpmlib prototype */
extern int rpmvercmp(const char * one, const char * two);
#define pkg_version_compare(v1, v2) rpmvercmp(v1, v2)

#endif
