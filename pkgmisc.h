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

struct pm_ctx;
void packages_fetch_summary(struct pm_ctx *pmctx,
                            tn_array *pkgs, const char *destdir, int nosubdirs);
int packages_fetch(struct pm_ctx *pmctx,
                   tn_array *pkgs, const char *destdir, int nosubdirs);

struct poldek_ts;
int packages_rpminstall(tn_array *pkgs, struct poldek_ts *ts);

#define PKGVERIFY_MD   (1 << 0)
#define PKGVERIFY_GPG  (1 << 1)
#define PKGVERIFY_PGP  (1 << 2)

unsigned pkg_get_verify_signflags(struct pkg *pkg);


int parse_evr(char *evrstr, int32_t *epoch,
              const char **ver, const char **rel);

int parse_nevr(char *nevrstr, const char **name,
               int32_t *epoch, const char **ver, const char **rel);


/* pkgmark_set */

struct pkgmark_set;
struct pkgmark_set *pkgmark_set_new(void);
void pkgmark_set_free(struct pkgmark_set *pms);
int pkgmark_set(struct pkgmark_set *pms, struct pkg *pkg, int set,
                uint32_t flag);

int pkgmark_isset(struct pkgmark_set *pms, struct pkg *pkg, uint32_t flag);

tn_array *pkgmark_get_packages(struct pkgmark_set *pmark, uint32_t flag);

#define PKGMARK_MARK        (1 << 1)  /* marked directly, i.e. by the user*/
#define PKGMARK_DEP         (1 << 2)  /* marked by deps */
#define PKGMARK_RM          (1 << 3) /* marked for removal */
#define PKGMARK_INTERNAL    (1 << 4)

#define pkg_hand_mark(pms, pkg) pkgmark_set(pms, pkg, 1, PKGMARK_MARK)
#define pkg_dep_mark(pms, pkg) pkgmark_set(pms, pkg, 1, PKGMARK_DEP)
#define pkg_unmark(pms, pkg) pkgmark_set(pms, pkg, 0, PKGMARK_DEP|PKGMARK_MARK)
    
#define pkg_is_dep_marked(pms, pkg) pkgmark_isset(pms, pkg, PKGMARK_DEP)
#define pkg_is_hand_marked(pms, pkg)  pkgmark_isset(pms, pkg, PKGMARK_MARK)
#define pkg_is_marked(pms, pkg) pkgmark_isset(pms, pkg, PKGMARK_MARK|PKGMARK_DEP) 
#define pkg_isnot_marked(pms, pkg) (!pkgmark_isset(pms, pkg, PKGMARK_MARK|PKGMARK_DEP))

#define pkg_mark_i(pms, pkg) pkgmark_set(pms, pkg, 1, PKGMARK_INTERNAL)
#define pkg_unmark_i(pms, pkg) pkgmark_set(pms, pkg, 0, PKGMARK_INTERNAL)
#define pkg_is_marked_i(pms, pkg) pkgmark_isset(pms, pkg, PKGMARK_INTERNAL)

#define pkg_rm_mark(pms, pkg) pkgmark_set(pms, pkg, 1, PKGMARK_RM)
#define pkg_is_rm_marked(pms, pkg) pkgmark_isset(pms, pkg, PKGMARK_RM)


#define pkg_rm_unmark(pms, pkg) pkgmark_set(pms, pkg, 0, PKGMARK_RM)

void pkgmark_massset(struct pkgmark_set *pmark, int set, uint32_t flag);

int packages_mark(struct pkgmark_set *pms,
                  const tn_array *pkgs, const tn_array *avpkgs, int withdeps);

#endif

