/* $Id$ */
#ifndef POLDEK_PKGMISC_H
#define POLDEK_PKGMISC_H

struct pkgscore_s {
    char        pkgbuf[512];
    int         pkgname_off;
    struct pkg  *pkg;
};

void pkgscore_match_init(struct pkgscore_s *psc, struct pkg *pkg);
int pkgscore_match(struct pkgscore_s *psc, const char *mask);
void packages_score(tn_array *pkgs, tn_array *patterns, unsigned scoreflag);

int packages_dump(tn_array *pkgs, const char *path, int fqfn);
int packages_fetch(tn_array *pkgs, const char *destdir, int nosubdirs);

struct poldek_ts;
int packages_rpminstall(tn_array *pkgs, struct poldek_ts *ts);


#define PKGVERIFY_MD   (1 << 0)
#define PKGVERIFY_GPG  (1 << 1)
#define PKGVERIFY_PGP  (1 << 2)

int package_verify_sign(const char *path, unsigned flags);

/* looks if pkg->pkgdir has set VERSIGN flag */
int package_verify_pgpg_sign(const struct pkg *pkg, const char *localpath);


int parse_evr(char *evrstr, int32_t *epoch,
              const char **ver, const char **rel);

int parse_nevr(char *nevrstr, const char **name,
               int32_t *epoch, const char **ver, const char **rel);



#endif

