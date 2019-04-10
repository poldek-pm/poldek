/*
  Copyright (C) 2000 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <locale.h>

#include <trurl/trurl.h>

#include "compiler.h"
#include "poldek.h"
#define POCLIDEK_ITSELF
#include "poclidek.h"

tn_array *execute_packages_command(struct poclidek_ctx *cctx, const char *command)
{
    struct poclidek_rcmd *cmd;
    tn_array *pkgs = NULL;

    cmd = poclidek_rcmd_new(cctx, NULL);

    if (poclidek_rcmd_execline(cmd, command))
        pkgs = poclidek_rcmd_get_packages(cmd);

    poclidek_rcmd_free(cmd);
    return pkgs;
}

tn_buf *execute_command_capture_output(struct poclidek_ctx *cctx, const char *command)
{
    struct poclidek_rcmd *cmd;
    tn_buf *nbuf = NULL;

    cmd = poclidek_rcmd_new(cctx, NULL);

    if (poclidek_rcmd_execline(cmd, command))
        nbuf = poclidek_rcmd_get_buf(cmd);

    poclidek_rcmd_free(cmd);
    return nbuf;
}

int execute_command(struct poclidek_ctx *cctx, const char *command)
{
    struct poclidek_rcmd *cmd;
    int rc;

    cmd = poclidek_rcmd_new(cctx, NULL);
    rc = poclidek_rcmd_execline(cmd, command);
    poclidek_rcmd_free(cmd);

    return rc;
}


void printf_nbuf(tn_buf *nbuf)
{
    tn_buf_it it;           /* n_buf iterator */
    char *p, line[1024];
    unsigned n, i;

    i = 0;
    n_buf_it_init(&it, nbuf);

    while ((p = n_buf_it_gets(&it, &n))) {
        if (n > sizeof(line) - 1)
            n = sizeof(line) - 1;

        memcpy(line, p, n);
        line[n] = '\0';
        printf("  %.2d: %s\n", i++, line);
    }
}



int main(void)
{
    struct poldek_ctx *ctx;
    struct poclidek_ctx  *cctx;
    tn_array *pkgs, *installed_pkgs;
    tn_buf *nbuf;
    char command[1024];
    int i;

    setlocale(LC_MESSAGES, "");
    setlocale(LC_CTYPE, "");

    poldeklib_init();           /* initialize library */

    ctx = poldek_new(0);        /* poldek context */
    poldek_load_config(ctx, "poclidek_demo-poldek.conf", NULL, 0); /* load test_cli.rc config */
    //poldek_load_config(ctx, NULL, NULL, 0); /* load default config */

    poldek_setup(ctx);          /* setup internals (cache dir, pm, etc) */

    cctx = poclidek_new(ctx);   /* poclidek (CLI interface) handler */

    printf("Available packages:\n");
    pkgs = execute_packages_command(cctx, "ls -q");
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        printf("  %s\n", pkg_id(pkg));
    }

    printf("Installed packages:\n");
    installed_pkgs = execute_packages_command(cctx, "cd /installed; ls -q");
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        printf("  %s\n", pkg_id(pkg));
    }
    n_array_free(installed_pkgs);

    /* see cli/desc.c for libpoldek API details */
    printf("Description of rpm package:\n");
    nbuf = execute_command_capture_output(cctx, "desc rpm");
    if (nbuf) {
        printf_nbuf(nbuf);
        n_buf_free(nbuf);
    }

    execute_command(cctx, "cd"); /* back to /all-avail */

    n_snprintf(command, sizeof(command), "install -t %s", pkg_id(n_array_nth(pkgs, 0)));
    printf("Executing \"%s\"\n", command);
    execute_command(cctx, command);

    n_snprintf(command, sizeof(command), "get -d /tmp %s", pkg_id(n_array_nth(pkgs, 0)));
    printf("Executing %s:\n", command);
    execute_command(cctx, command);

    poclidek_free(cctx);
    poldek_free(ctx);

    poldeklib_destroy();
}
