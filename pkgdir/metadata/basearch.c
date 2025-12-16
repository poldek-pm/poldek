#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>

#include "pm/pm.h"
#ifdef HAVE_RPMORG
# include "pm/rpmorg/pm_rpm.h"
#else
# error "rpm5 is not yet supported"
#endif

// taken from libdnf
static const struct {
    const char  base[16];
    const char  native[11][13];
} amap[] =  {
    { "aarch64",    { "aarch64" } },
    { "alpha",      { "alpha", "alphaev4", "alphaev45", "alphaev5", "alphaev56", "alphaev6", "alphaev67", "alphaev68", "alphaev7", "alphapca56" } },
    { "arm",        { "armv5tejl", "armv5tel", "armv5tl", "armv6l", "armv7l", "armv8l" } },
    { "armhfp",     { "armv6hl", "armv7hl", "armv7hnl", "armv8hl", "armv8hnl", "armv8hcnl" } },
    { "i386",       { "i386", "athlon", "geode", "i386", "i486", "i586", "i686" } },
    { "ia64",       { "ia64" } },
    { "mips",       { "mips" } },
    { "mipsel",     { "mipsel" } },
    { "mips64",     { "mips64" } },
    { "mips64el",   { "mips64el" } },
    { "noarch",     { "noarch" } },
    { "ppc",        { "ppc" } },
    { "ppc64",      { "ppc64", "ppc64iseries", "ppc64p7", "ppc64pseries" } },
    { "ppc64le",    { "ppc64le" } },
    { "riscv32",    { "riscv32" } },
    { "riscv64",    { "riscv64" } },
    { "riscv128",   { "riscv128" } },
    { "s390",       { "s390" } },
    { "s390x",      { "s390x" } },
    { "sh3",        { "sh3" } },
    { "sh4",        { "sh4", "sh4a" } },
    { "sparc",      { "sparc", "sparc64", "sparc64v", "sparcv8", "sparcv9", "sparcv9v" } },
    { "x86_64",     { "x86_64", "amd64", "ia32e" } },
    { "loongarch64",{ "loongarch64" } }
};

static const char *find_base_arch(const char *native) {
    int n = sizeof(amap) / sizeof(amap[0]);

    for (int i = 0; i < n; i++) {
        for (int j = 0; *amap[i].native[j] != '\0' ; j++) {
            if (strcmp(amap[i].native[j], native) == 0) {
                return amap[i].base;
            }
        }
    }
    return NULL;
}

const char *get_base_arch() {
    const char *arch;

    void *ctx = pm_rpm_init();

    rpmGetArchInfo(&arch, NULL);
    arch = find_base_arch(arch);

    pm_rpm_destroy(ctx);

    return arch;
}
