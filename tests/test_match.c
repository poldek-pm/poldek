#include "test.h"

static const char *maflags_snprintf_s(unsigned maflags)
{
    static char buf[128];
    int n = 0, nsave;

    if (maflags == 0)
        return "";

    n += n_snprintf(&buf[n], sizeof(buf) - n, "[promote=");
    nsave = n;

    if (maflags & POLDEK_MA_PROMOTE_VERSION)
        n += n_snprintf(&buf[n], sizeof(buf) - n, "V,");

    if (maflags & POLDEK_MA_PROMOTE_REQEPOCH)
        n += n_snprintf(&buf[n], sizeof(buf) - n, "reqE,");

    if (maflags & POLDEK_MA_PROMOTE_CAPEPOCH)
        n += n_snprintf(&buf[n], sizeof(buf) - n, "capE,");

    if (n == nsave)
        return "";

    buf[n - 1] = ']';
    return buf;
}

#if 0                           /* unused */
static const char *rel_snprintf_s(unsigned rel)
{
    static char buf[128];
    int n = 0;

    if (rel == 0)
        return "";

    if (rel & REL_GT)
        n += n_snprintf(&buf[n], sizeof(buf) - n, ">");

    if (rel & REL_EQ)
        n += n_snprintf(&buf[n], sizeof(buf) - n, "=");

    if (rel & REL_LT)
        n += n_snprintf(&buf[n], sizeof(buf) - n, "<");

    if (n == 0)
        return "";

    return buf;
}
#endif

static int do_test_pkgmatch(char *capevr, char *evr, int relation, unsigned maflags,
                     int expected)
{
    struct pkg *pkg;
    struct capreq *req;
    const char *ver, *rel;
    int rc;
    int32_t epoch;

    if (!poldek_util_parse_evr(n_strdup(capevr), &epoch, &ver, &rel))
        return -1;

    pkg = pkg_new("poo", epoch, ver, rel,  NULL, NULL);
    req = capreq_new_evr(NULL, "poo", n_strdup(evr), relation, 0);

    rc = pkg_xmatch_req(pkg, req, maflags) ? 1 : 0;
    msgn_i(1, 2, "%s match%s %s => %s", pkg_evr_snprintf_s(pkg),
           maflags_snprintf_s(maflags), capreq_snprintf_s(req),
           rc ? "YES" : "NO");

    fail_if(rc != expected,
            "match %s <=> %s not equal %d",
            pkg_evr_snprintf_s(pkg), capreq_snprintf_s(req), expected);
    pkg_free(pkg);
    capreq_free(req);
    return rc;
}

static
int do_test_capmatch(char *capevr, char *evr, int relation, unsigned maflags,
                     int expected)
{
    struct capreq *cap, *req;
    int rc;

    if (capevr)
        cap = capreq_new_evr(NULL, "coo", n_strdup(capevr), REL_EQ, 0);
    else
        cap = capreq_new(NULL, "coo", 0, NULL, NULL, 0, 0);

    req = capreq_new_evr(NULL, "coo", n_strdup(evr), relation, 0);

    rc = cap_xmatch_req(cap, req, maflags) ? 1 : 0;
    msgn_i(1, 2, "%s match%s %s => %s", capreq_snprintf_s(cap),
           maflags_snprintf_s(maflags), capreq_snprintf_s0(req),
           rc ? "YES" : "NO");

    fail_if(rc != expected,
            "match %s <=> %s not equal %d",
            capreq_snprintf_s0(cap), capreq_snprintf_s(req), expected);

    capreq_free(cap);
    capreq_free(req);
    return rc;
}

START_TEST (test_pkg_match) {
    int i, e, v, r = 0;
    int reltab[] = { REL_GT, REL_GT | REL_EQ, REL_EQ, REL_LT | REL_EQ, REL_LT, 0 };
    char *prev_evr = NULL;
    struct pair {
        char *capevr;
        char *evr;
        int rel;
        unsigned maflags;
        int expected;

    } pairs[] = {
        {"0:1.2-1", "0:1.0",  REL_GT, 0, 1 },
        {"0:1.2-1", "1:1.1",  REL_GT, 0, 0 },
        {"1:1.2-1", "1:1.1",  REL_GT, 0, 1 },
        {"2:1.2-1", "1:1.1",  REL_GT, 0, 1 },

        {"0:1.0-1", "0:1.0",  REL_EQ, 0, 1 },
        {"0:1.0-1", "1:1.0",  REL_EQ, 0, 0 },
        {"1:1.0-1", "0:1.0",  REL_EQ, 0, 0 },
        {"1:1.0-1", "1:1.0",  REL_EQ, 0, 1 },

        {"0:1.0-1", "0:1.0",  REL_EQ | REL_GT, 0, 1 },
        {"0:1.0-2", "0:1.0-1",  REL_EQ | REL_GT, 0, 1 },
        {"0:1.0-1", "0:1.0-2",  REL_EQ | REL_GT, 0, 0 },

        /* promote_epoch */
        {"0:1.0-1", "1:1.0",  REL_EQ | REL_GT, 0, 0 },
        {"0:1.0-1", "1:1.0",  REL_EQ | REL_GT, POLDEK_MA_PROMOTE_EPOCH, 0 },

        {"1:1.0-1", "1:1.0",  REL_EQ | REL_GT, POLDEK_MA_PROMOTE_EPOCH, 1 },
        {"1:1.0-1", "1.0",    REL_EQ | REL_GT, POLDEK_MA_PROMOTE_EPOCH, 1 },

        {"0:1.0-1", "1:1.0",  REL_EQ | REL_GT, 0, 0 },
        { 0, 0, 0, 0, 0 },
    };

    i = 0;
    msg(1, "\n");
    while (pairs[i].evr) {
        struct pair *p = &pairs[i++];
        do_test_pkgmatch(p->capevr, p->evr, p->rel, p->maflags, p->expected);
        if (p->maflags == 0)    /* cap_match_req behaves different */
            do_test_capmatch(p->capevr, p->evr, p->rel, p->maflags, p->expected);
    }
    msg(1, "\n");
    for (e=0; e < 2; e++) {
        for (v=1; v < 3; v++) {
            char lt_evr_e[128], gt_evr_e[128], lt_evr_v[128], gt_evr_v[128];
            *lt_evr_e = '\0';
            *gt_evr_e = '\0';

            if (e > 0) {
                n_snprintf(lt_evr_e, sizeof(lt_evr_e), "%d:%d.0-0.1", e - 1, v, r);
                n_snprintf(gt_evr_e, sizeof(gt_evr_e), "%d:%d.0-10", e - 1, v, r);
            }

            n_snprintf(lt_evr_v, sizeof(lt_evr_v), "%d:%d.0-0.1", e, v, r);
            n_snprintf(gt_evr_v, sizeof(gt_evr_v), "%d:%d.2-1", e, v, r);

            for (r=1; r < 3; r++) {
                char evr[128];
                n_snprintf(evr, sizeof(evr), "%d:%d.1-%d", e, v, r);
                if (!prev_evr)
                    prev_evr = n_strdup(evr);

                for (i=0; reltab[i] != 0; i++) {
                    int expected, rel = reltab[i];

                    expected = (rel == REL_GT || rel ==  REL_LT) ? 0 : 1;
                    do_test_pkgmatch(evr, evr, rel, 0, expected);
                    do_test_capmatch(evr, evr, rel, 0, expected);

                    expected = (rel == REL_GT || rel == (REL_GT | REL_EQ)) ? 1 : 0;
                    do_test_pkgmatch(evr, lt_evr_v, rel, 0, expected);
                    do_test_capmatch(evr, lt_evr_v, rel, 0, expected);

                    expected = (rel == REL_LT || rel == (REL_LT | REL_EQ)) ? 1 : 0;
                    do_test_pkgmatch(evr, gt_evr_v, rel, 0, expected);
                    do_test_capmatch(evr, gt_evr_v, rel, 0, expected);
                }
            }
        }
    }
}
END_TEST




START_TEST (test_cap_match) {
    int i;
    struct pair {
        char *capevr;
        char *evr;
        int rel;
        unsigned maflags;
        int expected;

    } pairs[] = {
        {"0:1.0", "1:1.0",  REL_EQ | REL_GT, 0, 0 },
        {"0:1.0", "1:1.0",  REL_EQ | REL_GT, POLDEK_MA_PROMOTE_REQEPOCH, 0 },
        {"0:1.0", "1:1.0",  REL_EQ | REL_GT, POLDEK_MA_PROMOTE_CAPEPOCH, 1 },

        {"1:1.0", "1:1.0",  REL_EQ | REL_GT, 0, 1 },
        {"1:1.0", "1:1.0",  REL_EQ | REL_GT, POLDEK_MA_PROMOTE_EPOCH, 1 },

        {"1:1.0", "1.0",  REL_EQ | REL_GT, 0, 1 },
        {"1:1.0", "1.0",  REL_EQ | REL_GT, POLDEK_MA_PROMOTE_CAPEPOCH, 1 },
        {"1:1.0", "1.0",  REL_EQ | REL_GT, POLDEK_MA_PROMOTE_REQEPOCH, 1 },

        {"1:1.0", "1.0",  REL_EQ, 0, 0 },
        {"1:1.0", "1.0",  REL_EQ, POLDEK_MA_PROMOTE_CAPEPOCH, 0 },
        {"1:1.0", "1.0",  REL_EQ, POLDEK_MA_PROMOTE_REQEPOCH, 1 },

        {"0:1.0", "1:1.0",  REL_EQ, 0, 0 },
        {"0:1.0", "1:1.0",  REL_EQ, POLDEK_MA_PROMOTE_CAPEPOCH, 1 },
        {"0:1.0", "1:1.0",  REL_EQ, POLDEK_MA_PROMOTE_REQEPOCH, 0 },
        {NULL, "1.0",  REL_EQ, 0, 0 },
        {NULL, "1.0",  REL_EQ, POLDEK_MA_PROMOTE_VERSION, 1 },
        {NULL, "1.0-1",  REL_GT, POLDEK_MA_PROMOTE_VERSION, 1 },
        {NULL, "1:1.0-3",  REL_GT, POLDEK_MA_PROMOTE_VERSION, 0 },
   {NULL, "1:1.0-3",  REL_GT, POLDEK_MA_PROMOTE_VERSION | POLDEK_MA_PROMOTE_REQEPOCH, 0 },
   {NULL, "1:1.0-3",  REL_GT, POLDEK_MA_PROMOTE_VERSION | POLDEK_MA_PROMOTE_CAPEPOCH, 1 },

        { 0, 0, 0, 0, 0 },
    };

    i = 0;
    msg(1, "\n");
    while (pairs[i].evr) {
        struct pair *p = &pairs[i++];
        do_test_capmatch(p->capevr, p->evr, p->rel, p->maflags, p->expected);
    }
}
END_TEST

NTEST_RUNNER("EVR match", test_pkg_match, test_cap_match);
