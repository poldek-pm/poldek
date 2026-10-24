/*
  Copyright (C) 2026 PLD Linux Team <feedback@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <trurl/narray.h>
#include <trurl/nmalloc.h>
#include <trurl/nstr.h>
#include <trurl/n_snprintf.h>

#include <vfile/p_open.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "conf.h"
#include "poldek.h"
#include "poldek_intern.h"
#include "pm_hooks.h"

#define HOOKS_SKIPPED    (1 << 0) /* nothing to run for this ts */
#define HOOKS_PRE_DONE   (1 << 1) /* post-install hooks are due */
#define HOOKS_PRE_FAILED (1 << 2)

/* what PLD's run-parts skips: dot files, rpm's leftovers and backups */
static int is_hook_name(const char *name)
{
    static const char *skipped[] = { "~", ",v", ".rpmsave", ".rpmnew",
                                     ".rpmorig", NULL };
    size_t len = strlen(name);
    int i;

    if (*name == '.')
        return 0;

    for (i = 0; skipped[i]; i++) {
        size_t n = strlen(skipped[i]);

        if (len >= n && strcmp(name + len - n, skipped[i]) == 0)
            return 0;
    }

    return 1;
}

/* the hooks in dir, sorted; empty if dir does not exist, NULL with errno
   set if it cannot be read */
static tn_array *list_hooks(const char *dir)
{
    tn_array      *hooks;
    struct dirent *ent;
    DIR           *d;
    int           err;

    if ((d = opendir(dir)) == NULL)
        return errno == ENOENT ? n_array_new(1, free, (tn_fn_cmp)strcmp) : NULL;

    hooks = n_array_new(8, free, (tn_fn_cmp)strcmp);
    for (;;) {
        char path[PATH_MAX];
        struct stat st;
        int n;

        errno = 0;
        if ((ent = readdir(d)) == NULL)
            break;

        if (!is_hook_name(ent->d_name))
            continue;

        n = n_snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (stat(path, &st) != 0) {
            logn(LOGWARN, "%s: %m", path);
            continue;
        }

        if (S_ISREG(st.st_mode) && (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
            n_array_push(hooks, n_strdupl(path, n));
    }

    /* readdir() returns NULL on error too: a partial list would let the
       operation go on without some of its hooks */
    err = errno;
    closedir(d);
    if (err) {
        n_array_free(hooks);
        errno = err;
        return NULL;
    }

    n_array_sort(hooks);
    return hooks;
}

static int run_hook(const char *path, const char *sudo, int loglevel)
{
    struct p_open_st pst;
    const char *cmd = path;
    char *argv[3];
    int n = 0, ec;

    if (sudo) {
        cmd = sudo;
        argv[n++] = n_basenam(sudo);
    }
    argv[n++] = (char *)path;
    argv[n] = NULL;

    msg(1, _("Running %s\n"), path);

    p_st_init(&pst);
    if (p_open(&pst, 0, cmd, argv) == NULL) {
        logn(loglevel, "%s", pst.errmsg ? pst.errmsg : path);
        p_st_destroy(&pst);
        return 0;
    }

    poldek__relay_output(&pst, 1, n_basenam(path));

    if ((ec = p_close(&pst)) != 0) {
        if (pst.errmsg)
            logn(loglevel, "%s", pst.errmsg);
        else
            logn(loglevel, _("%s exited with %d"), path, ec);
    }

    p_st_destroy(&pst);
    return ec == 0;
}

/* a failing pre-install hook stops the rest, post-install ones all run */
static int run_hooks(const char *hooksdir, const char *subdir,
                     const char *sudo, int is_pre)
{
    int i, ok = 1, loglevel = is_pre ? LOGERR : LOGWARN;
    char dir[PATH_MAX];
    tn_array *hooks;

    n_snprintf(dir, sizeof(dir), "%s/%s", hooksdir, subdir);
    if ((hooks = list_hooks(dir)) == NULL) {
        logn(loglevel, "%s: %m", dir);
        return 0;
    }

    for (i = 0; i < n_array_size(hooks); i++) {
        if (!run_hook(n_array_nth(hooks, i), sudo, loglevel)) {
            ok = 0;
            if (is_pre)
                break;
        }
    }

    n_array_free(hooks);
    return ok;
}

static const char *hooks_dir(const struct poldek_ts *ts)
{
    const char *dir = NULL;

    if (ts->ctx->htconf)
        dir = poldek_conf_get(poldek_conf_get_section(ts->ctx->htconf, "global"),
                              "hooks dir", NULL);

    return dir && *dir ? dir : NULL;
}

int poldek__ts_hooks_pre(struct poldek_ts *ts, const char *sudo)
{
    const char *dir;
    struct stat st;

    if (ts->_hooks)
        return (ts->_hooks & HOOKS_PRE_FAILED) == 0;

    ts->_hooks = HOOKS_SKIPPED;

    /* a test run changes nothing, and another root is not the system
       the hooks look after */
    if ((dir = hooks_dir(ts)) == NULL || ts->getop(ts, POLDEK_OP_RPMTEST) ||
        (ts->rootdir && strcmp(ts->rootdir, "/") != 0))
        return 1;

    if (stat(dir, &st) != 0 && errno == ENOENT) {
        logn(LOGWARN, "%s: %m", dir);
        return 1;
    }

    if (!run_hooks(dir, "pre-install.d", sudo, 1)) {
        logn(LOGERR, _("pre-install hook failed, no packages were changed"));
        ts->_hooks = HOOKS_PRE_FAILED;
        return 0;
    }

    ts->_hooks = HOOKS_PRE_DONE;
    ts->_hooks_sudo = sudo;
    return 1;
}

int poldek__ts_hooks_failed(const struct poldek_ts *ts)
{
    return (ts->_hooks & HOOKS_PRE_FAILED) != 0;
}

void poldek__ts_hooks_post(struct poldek_ts *ts)
{
    const char *dir;

    if ((ts->_hooks & HOOKS_PRE_DONE) && (dir = hooks_dir(ts)))
        run_hooks(dir, "post-install.d", ts->_hooks_sudo, 0);

    ts->_hooks = 0;
    ts->_hooks_sudo = NULL;
}
