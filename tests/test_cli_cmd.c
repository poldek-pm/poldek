#include <argp.h>
#include "test.h"
#include "poldek.h"
#include "cli/cli.h"
#include "cli/cmd.h"

static int probe_ishelp, probe_ran, probe_parse_rc;

/* a command that parses its options itself, with the argp it is given */
static int probe_cmd(struct cmdctx *cmdctx, int argc, const char **argv,
                     struct argp *argp)
{
    probe_ishelp = (cmdctx->rtflags & CMDCTX_ISHELP) != 0;
    probe_ran = 1;
    probe_parse_rc = argp_parse(argp, argc, (char**)argv, ARGP_NO_EXIT, 0,
                                cmdctx);
    return 1;
}

static struct poclidek_cmd command_probe = {
    .flags = COMMAND_EMPTYARGS, .name = "probe", .arg = "[ARG]",
    .doc = "probe", .cmd_fn = probe_cmd,
};

/* such a command is told that it was asked for help before it is called */
START_TEST (test_cmd_fn_knows_help) {
    struct poldek_ctx *ctx;
    struct poclidek_ctx *cctx;

    poldeklib_init();
    ctx = poldek_new(0);
    fail_unless(poldek_setup(ctx), "setup failed");
    cctx = poclidek_new(ctx);
    fail_if(cctx == NULL);
    fail_unless(poclidek_add_command(cctx, &command_probe));

    probe_ran = 0;
    probe_parse_rc = -1;
    poclidek_execline(cctx, NULL, "probe --help");
    fail_unless(probe_ran, "probe --help: not called");
    fail_unless(probe_ishelp, "probe --help: no CMDCTX_ISHELP");
    fail_unless(probe_parse_rc == 0, "probe --help: argp_parse() failed");

    probe_ran = 0;
    probe_parse_rc = -1;
    poclidek_execline(cctx, NULL, "probe x");
    fail_unless(probe_ran, "probe x: not called");
    fail_if(probe_ishelp, "probe x: CMDCTX_ISHELP");
    fail_unless(probe_parse_rc == 0, "probe x: argp_parse() failed");

    /* past "--" it is an argument of the command, as for the argp commands */
    probe_ran = 0;
    probe_ishelp = 1;
    probe_parse_rc = -1;
    poclidek_execline(cctx, NULL, "probe -- --help");
    fail_unless(probe_ran, "probe -- --help: not called");
    fail_if(probe_ishelp, "probe -- --help: CMDCTX_ISHELP");
    fail_unless(probe_parse_rc == 0, "probe -- --help: argp_parse() failed");

    poclidek_free(cctx);
    poldek_free(ctx);
    poldeklib_destroy();
}
END_TEST

NTEST_RUNNER("cli command", test_cmd_fn_knows_help);
