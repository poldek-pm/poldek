#include "test.h"
#include "poldek_intern.h"
#include "capreq.h"

static struct capreq *new_capreq(tn_alloc *na, char *name, int e, char *v, char *r)
{
    // capreq_new(na, name, epoch, version, release, relflags, flags);
    return capreq_new(na, name, e, v, r, v || r ? REL_EQ : 0, 0);
}

void do_test_capreq_store(tn_alloc *na, tn_array *caps) {
    char buf1[4096], buf2[4096];

    tn_buf *nbuf = n_buf_new(1024);
    capreq_arr_store(caps, nbuf);

    /* skip data size (we have inconsistency here) */
    tn_buf *rbuf = n_buf_new(0);
    n_buf_init(rbuf,
               n_buf_ptr(nbuf) + sizeof(uint16_t),
               n_buf_size(nbuf) - sizeof(uint16_t));

    tn_array *re = capreq_arr_restore(na, rbuf);
    n_buf_free(rbuf);

    expect_notnull(re);
    expect_int(n_array_size(re), n_array_size(caps));

    for (int i = 0; i < n_array_size(caps); i++) {
        struct capreq *orig = n_array_nth(caps, i);
        struct capreq *restored = n_array_nth(re, i);
        //printf("RESTORE[%d] %p %s vs. %s\n", i, na, capreq_stra(orig), capreq_stra(restored));

        expect_int(strlen(capreq_name(orig)), strlen(capreq_name(restored)));

        expect_str(capreq_str(buf1, sizeof(buf1), orig),
                   capreq_str(buf2, sizeof(buf2), restored));
    }
    n_array_cfree(&re);
    n_buf_free(nbuf);
}



START_TEST(test_cap) {
    tn_array *arr = capreq_arr_new(4);
    n_array_push(arr, new_capreq(NULL, "foo", 0, NULL, NULL));
    n_array_push(arr, new_capreq(NULL, "bar", 0, "1", "1"));
    n_array_push(arr, new_capreq(NULL, "baz", 10, "1", "1"));

    do_test_capreq_store(NULL, arr);
    n_array_free(arr);


    tn_alloc *na = n_alloc_new(4, TN_ALLOC_OBSTACK);
    arr = capreq_arr_new(4);
    n_array_push(arr, new_capreq(na, "foo", 0, NULL, NULL));
    n_array_push(arr, new_capreq(na, "bar", 0, "1", "1"));
    n_array_push(arr, new_capreq(na, "baz", 10, "1", "1"));

    do_test_capreq_store(na, arr);
    n_array_free(arr);


}
END_TEST

#define LONG_NAME "(glibc and (langpacks-en or langpacks-en_AG or langpacks-en_AU or langpacks-en_BW or langpacks-en_CA or langpacks-en_DK or langpacks-en_GB or langpacks-en_HK or langpacks-en_IE or langpacks-en_IL or langpacks-en_IN or langpacks-en_NG or langpacks-en_NZ or langpacks-en_PH or langpacks-en_SC or langpacks-en_SG or langpacks-en_US or langpacks-en_ZA or langpacks-en_ZM or langpacks-en_ZW))"

START_TEST(test_long_capname) {
    tn_alloc *na = n_alloc_new(4, TN_ALLOC_OBSTACK);
    tn_array *arr = capreq_arr_new(2);
    //n_array_push(arr, new_capreq(LONG_NAME LONG_NAME, 0));
    n_array_push(arr, new_capreq(na, "foo", 0, "1", "1"));
    n_array_push(arr, new_capreq(na, LONG_NAME, 0, "1", "2"));
    do_test_capreq_store(na, arr);
}
END_TEST



NTEST_RUNNER("store",
             test_cap,
             test_long_capname
    );
