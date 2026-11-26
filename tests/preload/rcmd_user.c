/*
  Preloaded into poldek by sh/13-cli: a program that runs its commands
  through rcmd, as the Python binding does, with the transaction poldek made
  for them (direct mode and --shcmd), with the mask in RCMD_MASK added to it
  first. What they leave in the caller's pipe is printed, the packages as
  "rcmd package: <id>" and the text as "rcmd output: <line>", and the
  arguments the transaction is left with as "rcmd argument: <mask>".
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <trurl/trurl.h>

#include "poldek.h"
#include "pkg.h"
#define POCLIDEK_ITSELF
#include "cli/poclidek.h"

static struct poclidek_rcmd *new_rcmd(struct poclidek_ctx *cctx,
                                      struct poldek_ts *ts)
{
    const char *mask;

    unsetenv("LD_PRELOAD");     /* not for rpm or anything else poldek runs */

    if (ts && (mask = getenv("RCMD_MASK")) != NULL)
        poldek_ts_add_pkgmask(ts, mask);

    return poclidek_rcmd_new(cctx, ts);
}

static int print_result(struct poclidek_rcmd *rcmd, struct poldek_ts *ts,
                        int rc)
{
    tn_array *pkgs;
    tn_buf *nbuf;

    if ((pkgs = poclidek_rcmd_get_packages(rcmd)) != NULL) {
        for (int i = 0; i < n_array_size(pkgs); i++)
            printf("rcmd package: %s\n", pkg_id(n_array_nth(pkgs, i)));
        n_array_free(pkgs);
    }

    /* the text is not 0-terminated */
    if ((nbuf = poclidek_rcmd_get_buf(rcmd)) != NULL) {
        const char *out = n_buf_ptr(nbuf), *end = out + n_buf_size(nbuf);

        while (out < end) {
            const char *nl = memchr(out, '\n', end - out);
            int n = (nl ? nl : end) - out;

            printf("rcmd output: %.*s\n", n, out);
            out += n + 1;
        }
        n_buf_free(nbuf);
    }

    /* the summary would tell the same, but the testing mode installs one
       package at a time and drops it after each */
    if (ts) {
        tn_array *masks = poldek_ts_get_args_asmasks(ts, 0);

        for (int i = 0; i < n_array_size(masks); i++)
            printf("rcmd argument: %s\n", (char *)n_array_nth(masks, i));
        n_array_free(masks);
    }

    poclidek_rcmd_free(rcmd);
    return rc;
}

__attribute__((visibility("default")))
int poclidek_exec(struct poclidek_ctx *cctx, struct poldek_ts *ts,
                  int argc, const char **argv)
{
    struct poclidek_rcmd *rcmd = new_rcmd(cctx, ts);

    return print_result(rcmd, ts, poclidek_rcmd_exec(rcmd, argc, argv));
}

__attribute__((visibility("default")))
int poclidek_execline(struct poclidek_ctx *cctx, struct poldek_ts *ts,
                      const char *cmdline)
{
    struct poclidek_rcmd *rcmd = new_rcmd(cctx, ts);

    return print_result(rcmd, ts, poclidek_rcmd_execline(rcmd, cmdline));
}
