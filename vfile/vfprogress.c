/*
  Copyright (C) 2001 - 2003 Pawel A. Gajda <mis@k2.net.pl>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
  $Id$
*/

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <trurl/nassert.h>

#include "i18n.h"
#define VFILE_INTERNAL
#include "vfile.h"

#define PROGRESSBAR_WIDTH 30

void vf_progress_init(struct vf_progress_bar *bar)
{
    memset(bar, 0, sizeof(*bar));

    bar->width = PROGRESSBAR_WIDTH;
    bar->is_tty = isatty(fileno(stdout));
}


static int nbytes2str(char *buf, int bufsize, unsigned long nbytes) 
{
    char unit = 'B';
    double nb;

    nb = nbytes;
    
    if (nb > 1024) {
        nb /= 1024;
        unit = 'K';
    }
    
    if (nb > 1024) {
        nb /= 1024;
        unit = 'M';
    }

    return snprintf(buf, bufsize, "%.1f%c", nb, unit);
}

static int eta2str(char *buf, int bufsize, struct vf_progress_bar *bar) 
{
    int mm, ss, n = 0;
    float eta = bar->eta + 0.5;

    
    mm = (int)(eta / 60.0);
    ss = (int)eta % 60;
    if (mm || ss)
        n = n_snprintf(&buf[n], bufsize - n, "%.2d:%.2d ETA", mm, ss);
    return n;
}


static void calculate_tt(long total, long amount, struct vf_progress_bar *bar)
{
    time_t		    current_time;

    current_time = time(NULL);
    if (current_time == bar->time_last)
        return;
    
    bar->time_last = current_time;
    bar->transfer_rate = (float)amount / (current_time - bar->time_base);
    if (bar->transfer_rate > 0)
        bar->eta = (total - amount) / bar->transfer_rate;

    bar->transfer_rate /= 1024.0;
}


void vf_progress(long total, long amount, void *data)
{
    struct vf_progress_bar  *bar = data;
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
                total < 1024) {       /* too small to show to */
                bar->state = VF_PROGRESS_DISABLED;
                return;
            }
        }
        bar->time_base = bar->time_last = time(NULL);
        bar->state = VF_PROGRESS_RUNNING;
        bar->maxlen = 0;
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

    if (amount > 0 && amount != total && (10 * percent) - bar->prev_perc < 4) {
        //printf("v %ld, %d  %ld, %f -> %f\n", n, bar->prev_perc,
        //       bar->prev_n, 10 * percent,
        //(10 * percent) - (float)bar->prev_perc);
        return;
    }
    calculate_tt(total, amount, bar);
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
        char unit_line[45], amount_str[16], total_str[16];
        int nn;
            
        nbytes2str(total_str, sizeof(total_str), total);
        nbytes2str(amount_str, sizeof(amount_str), amount);

        if (total == amount) {
            if (bar->time_base == bar->time_last) /* fetched in less than 1s */
                bar->transfer_rate = total / 1024.0;
                    
            nn = n_snprintf(unit_line, sizeof(unit_line), "[%s (%.1fK/s)]",
                            total_str, bar->transfer_rate);
        } else {
            int n = 0;
            char eta_str[64];

            n = eta2str(eta_str, sizeof(eta_str), bar);
            
            nn = n_snprintf(unit_line, sizeof(unit_line),
                            "[%s of %s (%.1fK/s)%s%s]",
                            amount_str, total_str, bar->transfer_rate,
                            n ? ", ": "",
                            n ? eta_str : "");
        }
        if (nn > bar->maxlen)
            bar->maxlen = nn;

        
        if (nn < bar->maxlen) {
            int unit_n = bar->maxlen - nn;
            n_assert((int)sizeof(unit_line) > nn + unit_n);
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

