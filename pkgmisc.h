/* $Id$ */
#ifndef POLDEK_PKGMISC_H
#define POLDEK_PKGMISC_H


/*  === pkgscore ===  */
struct pkgscore_s {
    char        pkgbuf[512];
    int         pkgname_off;
    struct pkg  *pkg;
};

void pkgscore_match_init(struct pkgscore_s *psc, struct pkg *pkg);
int pkgscore_match(struct pkgscore_s *psc, const char *mask);
void packages_score(tn_array *pkgs, tn_array *patterns, unsigned scoreflag);

/*  === utils ===  */
int packages_dump(tn_array *pkgs, const char *path, int fqfn);


struct pm_ctx;
/* pmctx is needed to call pm_verify_signature() */
void packages_fetch_summary(struct pm_ctx *pmctx,
                            tn_array *pkgs, const char *destdir, int nosubdirs);

int packages_fetch(struct pm_ctx *pmctx,
                   tn_array *pkgs, const char *destdir, int nosubdirs);

int packages_fetch_remove(tn_array *pkgs, const char *destdir);



#define PKGVERIFY_MD   (1 << 0)
#define PKGVERIFY_GPG  (1 << 1)
#define PKGVERIFY_PGP  (1 << 2)
unsigned pkg_get_verify_signflags(struct pkg *pkg);



int poldek_util_parse_evr(char *evrstr, int32_t *epoch,
                          const char **ver, const char **rel);
int poldek_util_parse_nevr(char *nevrstr, const char **name,
                           int32_t *epoch, const char **ver, const char **rel);


/*  === pkgmark_set ===  */
struct pkgmark_set;
struct pkgmark_set *pkgmark_set_new(int size);
void pkgmark_set_free(struct pkgmark_set *pms);
int pkgmark_set(struct pkgmark_set *pms, struct pkg *pkg, int set,
                uint32_t flag);

int pkgmark_isset(struct pkgmark_set *pms, struct pkg *pkg, uint32_t flag);

tn_array *pkgmark_get_packages(struct pkgmark_set *pmark, uint32_t flag);

// pkg_set_mARKfLAG
#define pkg_set_mf(pms, pkg, flag) pkgmark_set(pms, pkg, 1, flag)
#define pkg_clr_mf(pms, pkg, flag) pkgmark_set(pms, pkg, 0, flag)
#define pkg_isset_mf(pms, pkg, flag) pkgmark_isset(pms, pkg, flag)

#define PKGMARK_MARK        (1 << 1)  /* marked directly, i.e. by the user*/
#define PKGMARK_DEP         (1 << 2)  /* marked by deps */
#define pkg_hand_mark(pms, pkg) pkgmark_set(pms, pkg, 1, PKGMARK_MARK)
#define pkg_dep_mark(pms, pkg) pkgmark_set(pms, pkg, 1, PKGMARK_DEP)
#define pkg_unmark(pms, pkg) pkgmark_set(pms, pkg, 0, PKGMARK_DEP|PKGMARK_MARK)

#define pkg_is_dep_marked(pms, pkg) pkgmark_isset(pms, pkg, PKGMARK_DEP)
#define pkg_is_hand_marked(pms, pkg)  pkgmark_isset(pms, pkg, PKGMARK_MARK)
#define pkg_is_marked(pms, pkg) pkgmark_isset(pms, pkg, PKGMARK_MARK|PKGMARK_DEP) 
#define pkg_isnot_marked(pms, pkg) (!pkgmark_isset(pms, pkg, PKGMARK_MARK|PKGMARK_DEP))


#define PKGMARK_RM          (1 << 3) /* marked for removal */
#define pkg_rm_mark(pms, pkg) pkgmark_set(pms, pkg, 1, PKGMARK_RM)
#define pkg_is_rm_marked(pms, pkg) pkgmark_isset(pms, pkg, PKGMARK_RM)
#define pkg_rm_unmark(pms, pkg) pkgmark_set(pms, pkg, 0, PKGMARK_RM)


#define PKGMARK_INTERNAL    (1 << 4)
#define pkg_mark_i(pms, pkg) pkgmark_set(pms, pkg, 1, PKGMARK_INTERNAL)
#define pkg_unmark_i(pms, pkg) pkgmark_set(pms, pkg, 0, PKGMARK_INTERNAL)
#define pkg_is_marked_i(pms, pkg) pkgmark_isset(pms, pkg, PKGMARK_INTERNAL)


#define PKGMARK_UNMETDEPS   (1 << 5)
#define pkg_set_unmetdeps(pms, pkg) pkgmark_set(pms, pkg, 1, PKGMARK_UNMETDEPS)
#define pkg_clr_unmetdeps(pms, pkg) pkgmark_set(pms, pkg, 0, PKGMARK_UNMETDEPS)
#define pkg_has_unmetdeps(pms, pkg) pkgmark_isset(pms, pkg, PKGMARK_UNMETDEPS)


#define PKGMARK_WHITE     (1 << 10)
#define PKGMARK_GRAY      (1 << 11)
#define PKGMARK_BLACK     (1 << 12)
#define PKGMARK_ALLCOLORS (PKGMARK_WHITE | PKGMARK_GRAY | PKGMARK_BLACK)

#if 0
#define pkg_set_color(pms, pkg, c) \
   do { pkg_clr_mf(pms, pkg, ~(PKGMARK_ALLCOLORS)); \
        pkg_set_mf(pms, pkg, c); } while (0)

#define pkg_is_color(pms, pkg, c) pkg_isset_mf(pms, pkg, c)
#endif

void pkgmark_massset(struct pkgmark_set *pmark, int set, uint32_t flag);

/* mark packages (PKGMARK_{MARK,DEP}) to pms */
int packages_mark(struct pkgmark_set *pms, const tn_array *pkgs, int withdeps);
/* .. and then verify marked set  */
int pkgmark_verify_set_conflicts(struct pkgmark_set *pms);

struct pkgset;
int packages_verify_dependecies(tn_array *pkgs, struct pkgset *ps);

#endif

