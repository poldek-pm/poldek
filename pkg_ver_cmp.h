#ifndef POLDEK_PKG_VER_CMP_H
#define POLDEK_PKG_VER_CMP_H

extern int pm_rpm_arch_score(const char *arch);
extern int pm_rpm_vercmp(const char *one, const char *two);

#define pkg_version_compare(v1, v2) pm_rpm_vercmp(v1, v2)
#define pm_architecture_score(arch) pm_rpm_arch_score(arch)

#endif
