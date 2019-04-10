/*
  Copyright (C) 2001 - 2007 Pawel A. Gajda <mis@pld-linux.org>

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

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <trurl/nassert.h>

#include "i18n.h"
#include "vfile.h"
#include "vfile_intern.h"

#define PROGRESSBAR_WIDTH 30

/* state */
#define VF_PROGRESS_VIRGIN    0
#define VF_PROGRESS_DISABLED  1
#define VF_PROGRESS_RUNNING   2

struct tty_progress_bar {
    int        width;
    int        state;
    int        is_tty;
    int        prev_n;
    int        prev_perc;
    time_t     time_base;
    time_t     time_last;
    float      transfer_rate;
    float      eta; /* estimated time of arrival */
    int        maxlen;
    int        freq;
};

static void *tty_progress_new(void *data, const char *label);
static void tty_progress(void *data, long total, long amount);
static void tty_progress_reset(void *data);

struct vf_progress vf_tty_progress = {
    NULL, tty_progress_new, tty_progress, tty_progress_reset, free
};

static void *tty_progress_new(void *data, const char *label)
{
    struct tty_progress_bar *bar;

    label = label;
    data = data;

    bar = n_malloc(sizeof(*bar));
    memset(bar, 0, sizeof(*bar));

    bar->width = PROGRESSBAR_WIDTH;
    bar->is_tty = isatty(fileno(stdout));
    return bar;
}

static void tty_progress_reset(void *data)
{
    struct tty_progress_bar *bar = data;
    bar->width = PROGRESSBAR_WIDTH;
    bar->is_tty = isatty(fileno(stdout));
}


static int nbytes2str(char *buf, int bufsize, unsigned long nbytes)
{
    char unit = 'B';
    double nb;

    nb = nbytes;

    if (nb > 1024) {
        nb /= 1024.0;
        unit = 'K';
    }

    if (nb > 1024) {
        nb /= 1024.0;
        unit = 'M';
    }

    return snprintf(buf, bufsize, "%.1f%c", nb, unit);
}

static int eta2str(char *buf, int bufsize, struct tty_progress_bar *bar)
{
    int hh, mm, ss, n = 0;
    float eta = bar->eta + 0.5;

    hh = (int)(eta / 60.0 / 60.0);
    mm = (int)(eta / 60.0) % 60;
    ss = (int)eta % 60;
    if (hh)
        n = n_snprintf(&buf[n], bufsize - n, "%.2d:%.2d:%.2d ETA", hh, mm, ss);
    else if (mm || ss)
        n = n_snprintf(&buf[n], bufsize - n, "%.2d:%.2d ETA", mm, ss);
    return n;
}


static void calculate_tt(long total, long amount, struct tty_progress_bar *bar)
{
    time_t current_time;

    current_time = time(NULL);
    if (current_time == bar->time_last) {
        bar->freq++;
        return;
    }

    bar->freq = 0;
    bar->time_last = current_time;
    bar->transfer_rate = (float)amount / (current_time - bar->time_base);
    if (bar->transfer_rate > 0)
        bar->eta = (total - amount) / bar->transfer_rate;
}

static void tty_progress(void *data, long total, long amount)
{
    struct tty_progress_bar *bar = data;
    char                    line[256], outline[256], fmt[40];
    float                   frac, percent;
    long                    n;

    if (bar->state == VF_PROGRESS_DISABLED)
        return;

    if (amount == -1) { /* aborted */
        if (bar->state == VF_PROGRESS_RUNNING)
            vf_log(VFILE_LOG_INFO | VFILE_LOG_TTY, "\n");

        bar->state = VF_PROGRESS_DISABLED;
        return;
    }

    if (bar->state == VF_PROGRESS_VIRGIN) {
        if (total > 0) {
            if (total == amount ||   /* downloaded before progress() call */
                total < 2048) {       /* too small to show to */
                bar->state = VF_PROGRESS_DISABLED;
                return;
            }
        }
        bar->time_base = bar->time_last = time(NULL);
        bar->state = VF_PROGRESS_RUNNING;
        bar->maxlen = 0;
	bar->freq = 0;
    }

#define HASH_SIZE 8192

    if (total == 0) {
        n = amount/HASH_SIZE;
        while (n > bar->prev_n++)
            vf_log(VFILE_LOG_INFO | VFILE_LOG_TTY, ".");
        return;
    }

    frac = (float) amount / (float) total;
    percent = frac * 100.0f;

    n = (int) (((float)bar->width) * frac);
    calculate_tt(total, amount, bar);

    /* Skip refresh if progress less than 0.4% or
        refresh frequency is greater than 3Hz  */
    if (amount > 0 && amount != total &&
       ((10 * percent) - bar->prev_perc < 4 || bar->freq > 3)) {
        //DBGF("v %ld, %d  %ld, %f -> %f\n", n, bar->prev_perc, bar->prev_n,
        //     10 * percent, (10 * percent) - (float)bar->prev_perc);
        return;
    }

    n_assert(bar->prev_n < 100);
    if (!bar->is_tty) {
        int k;

        k = n - bar->prev_n;
        n_assert(k >= 0);
        n_assert(k < (int)sizeof(line));
        memset(line, '.', k);
        line[k] = '\0';
        vf_log(VFILE_LOG_INFO | VFILE_LOG_TTY, "%s", line);

        if (amount && amount == total) { /* last notification */
            vf_log(VFILE_LOG_INFO | VFILE_LOG_TTY, _("done"));
            vf_log(VFILE_LOG_INFO | VFILE_LOG_TTY, "\n");
            bar->state = VF_PROGRESS_DISABLED;
        }

    } else {
        char unit_line[45], amount_str[16], total_str[16], transfer_str[16];
        int nn;

        nbytes2str(total_str, sizeof(total_str), total);
        nbytes2str(amount_str, sizeof(amount_str), amount);
        nbytes2str(transfer_str, sizeof(transfer_str), bar->transfer_rate);

        if (total == amount) {
            if (bar->time_base == bar->time_last) { /* fetched in less than 1s */
                bar->transfer_rate = total;
                nbytes2str(transfer_str, sizeof(transfer_str),
                           bar->transfer_rate);
            }

            nn = n_snprintf(unit_line, sizeof(unit_line), "[%s (%s/s)]",
                            total_str, transfer_str);
        } else {
            int en = 0;
            char eta_str[64];

            en = eta2str(eta_str, sizeof(eta_str), bar);
            nn = n_snprintf(unit_line, sizeof(unit_line),
                            "[%s of %s (%s/s)] [%s]",
                            amount_str, total_str, transfer_str,
                            en ? eta_str : "--:--:--");
        }
        if (nn > bar->maxlen)
            bar->maxlen = nn;

        if (nn < bar->maxlen) {
            int unit_n = bar->maxlen - nn;
            //n_assert((int)sizeof(unit_line) > nn + unit_n);
            memset(&unit_line[nn], ' ', unit_n);
        }

        unit_line[sizeof(unit_line) - 1] = '\0';

        n_assert(n >= 0);
        n_assert(n < (int) sizeof(line));
        memset(line, '.', n);
        line[n] = '\0';

        snprintf(fmt, sizeof(fmt), "%%-%ds %%5.1f%%%% %%s", bar->width);
        snprintf(outline, sizeof(outline), fmt, line, percent, unit_line);

        if (total == amount) {
            bar->state = VF_PROGRESS_DISABLED;
            vf_log(VFILE_LOG_INFO | VFILE_LOG_TTY, "\r%s\n", outline);

        } else
            vf_log(VFILE_LOG_INFO | VFILE_LOG_TTY, "\r%s", outline);
    }

    bar->prev_n = n;
    bar->prev_perc = 10 * percent;
}


void *vf_progress_new(const char *label)
{
    struct vf_progress *p = vfile_conf.bar;
    return p->new(p->data, label);
}

void vf_progress_reset(void *bar)
{
    struct vf_progress *p = vfile_conf.bar;
    p->reset(bar);
}

void vf_progress(void *bar, long total, long amount)
{
    struct vf_progress *p = vfile_conf.bar;
    p->progress(bar, total, amount);
}

void vf_progress_free(void *bar)
{
    struct vf_progress *p = vfile_conf.bar;
    if (p->free)
        p->free(bar);
}
