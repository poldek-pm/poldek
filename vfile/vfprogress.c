/*
  Copyright (C) 2001 - 2002 Pawel A. Gajda <mis@k2.net.pl>

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

#define PROGRESSBAR_WIDTH 50

void vfile_progress_init(struct vf_progress_bar *bar)
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


void vfile_progress(long total, long amount, void *data)
{
    struct vf_progress_bar  *bar = data;
    char                    line[256], outline[256], fmt[40];
    float                   frac, percent;
    long                    n;


    if (bar->state == VF_PROGRESS_DISABLED)
        return;
    
    if (bar->state == VF_PROGRESS_VIRGIN) {
        if (total > 0) {
            if (total == amount ||   /* downloaded before progress() call */
                total < 1024) {       /* too small to show to */
                bar->state = VF_PROGRESS_DISABLED;
                return;
            }
        }
        
        bar->state = VF_PROGRESS_RUNNING;
    }
    
#define HASH_SIZE 8192
    
    if (total == 0) {
        n = amount/HASH_SIZE;
        while (n > bar->prev_n++)
            vfile_msgtty_fn(".");
        return;
    }
    
    frac = (float) amount / (float) total;
    percent = frac * 100.0f;
        
    n = (int) (((float)bar->width) * frac);
    
    //if (n <= bar->prev_n)
    //    return;
            
    n_assert(bar->prev_n < 100);

    if (!bar->is_tty) {
        int k;
        
        k = n - bar->prev_n;
        n_assert(k >= 0);
        n_assert(k < (int)sizeof(line));
        memset(line, '.', k);
        line[k] = '\0';
        vfile_msgtty_fn("%s", line);
        
        if (amount && amount == total) { /* last notification */
            vfile_msgtty_fn(_("done\n"));
            bar->state = VF_PROGRESS_DISABLED;
        }
        
    } else {
        char unit_line[23], amount_str[16], total_str[16];
        int nn, unit_n;
            
        
        nbytes2str(total_str, sizeof(total_str), total);
        nbytes2str(amount_str, sizeof(amount_str), amount);

        if (total == amount)
            nn = snprintf(unit_line, sizeof(unit_line), "[%s]", total_str);
        else 
            nn = snprintf(unit_line, sizeof(unit_line), "[%s of %s]",
                          amount_str, total_str);

        unit_n = sizeof(unit_line) - nn - 1;
        if (unit_n > 0)
            memset(&unit_line[nn], ' ', unit_n);
        unit_line[sizeof(unit_line) - 1] = '\0';

        n_assert(n >= 0);
        n_assert(n < (int) sizeof(line));
        memset(line, '.', n);
        line[n] = '\0';

        snprintf(fmt, sizeof(fmt), "%%-%ds %%5.1f%%%% %%s", bar->width);
        snprintf(outline, sizeof(outline), fmt, line, percent, unit_line);
        
        if (total == amount) {
            bar->state = VF_PROGRESS_DISABLED;
            vfile_msgtty_fn("\r%s\n", outline);
            
        } else 
            vfile_msgtty_fn("\r%s", outline);
    }
    
    bar->prev_n = n;
}

