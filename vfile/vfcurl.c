/* 
  Copyright (C) 2000, 2002 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <curl/curl.h>
#include <curl/easy.h>
#include <trurl/narray.h>
#include <trurl/nstr.h>
#include <trurl/nassert.h>

#define VFILE_INTERNAL
#include "vfile.h"

static const char *modname = "curl";

#define PROGRESSBAR_WIDTH 50

static int progress (void *clientp, size_t dltotal, size_t dlnow,
                     size_t ultotal, size_t ulnow);


#define PBAR_ST_VIRGIN      0
#define PBAR_ST_RUNNING     1
#define PBAR_ST_FINISHED    2
#define PBAR_ST_TERMINATED  3
#define PBAR_ST_DISABLED    5

struct progress_bar {
    size_t  total;
    size_t  prev;
    size_t  point;
    int     width;
    int     state;
    int     is_tty;
    size_t  prev_n;
    size_t  total_downloaded;
    size_t  resume_from;
};


static CURL *curlh = NULL;


int vfile_curl_init(void) 
{
    char *errmsg = "curl_easy_setopt failed";
    
    if ((curlh = curl_easy_init()) == NULL)
        return 0;
    
#if 0                           /* curl bug */
    if (curl_easy_setopt(curlh, CURLOPT_MAXCONNECTS, 16) != 0) {
        vfile_err_fn(errmsg);
        return 0;
    }
#endif

    if (curl_easy_setopt(curlh, CURLOPT_MUTE, *vfile_verbose > 1 ? 0:1) != 0) {
        vfile_err_fn(errmsg);
        return 0;
    }

    if (*vfile_verbose > 2) {
        if (curl_easy_setopt(curlh, CURLOPT_VERBOSE, 1) != 0) {
            vfile_err_fn(errmsg);
            return 0;
        }
        
        if (curl_easy_setopt(curlh, CURLOPT_NOPROGRESS, 1) != 0) {
            vfile_err_fn(errmsg);
            return 0;
        }
        
    } else {
        if (curl_easy_setopt(curlh, CURLOPT_VERBOSE, 0) != 0) {
            vfile_err_fn(errmsg);
            return 0;
        }
        
        if (curl_easy_setopt(curlh, CURLOPT_NOPROGRESS, 0) != 0) {
            vfile_err_fn(errmsg);
            return 0;
        }
    }
    
    if (curl_easy_setopt(curlh, CURLOPT_PROGRESSFUNCTION, progress) != 0) {
        vfile_err_fn(errmsg);
        return 0;
    }

    if (curl_easy_setopt(curlh, CURLOPT_USERPWD,
                         "anonymous:poldek@znienacka.net") != 0) {
        vfile_err_fn(errmsg);
        return 0;
    }

#if 0 /* disabled, cURL's timeouts make no sense: CURLOPT_TIMEOUT
         it is a time to transfer file */
    if (curl_easy_setopt(curlh, CURLOPT_TIMEOUT, 300) != 0) { /* read to */
        vfile_err_fn(errmsg);
        return 0;
    }

    if (curl_easy_setopt(curlh, CURLOPT_CONNECTTIMEOUT, 300) != 0) {
        vfile_err_fn(errmsg);
        return 0;
    }
#endif
    return 1;
}


int do_vfile_curl_fetch(const char *dest, const char *url)
{
    struct progress_bar bar = {0, 0, 0, PROGRESSBAR_WIDTH, 0, 0, 0, 0, 0};
    struct stat st;
    FILE *stream;
    int  curl_rc;
    char err[CURL_ERROR_SIZE * 2];
    char *errmsg = "curl_easy_setopt failed";

    
    bar.is_tty = isatty(fileno(stdout));
    

    *err = '\0';

    if (curl_easy_setopt(curlh, CURLOPT_ERRORBUFFER, err) != 0) {
        vfile_err_fn(errmsg);
        return -1;
    }

    if (curl_easy_setopt(curlh, CURLOPT_PROGRESSDATA, &bar) != 0) {
        vfile_err_fn(errmsg);
        return -1;
    }
    
    if (curl_easy_setopt(curlh, CURLOPT_URL, url) != 0) {
        vfile_err_fn(errmsg);
        return -1;
    }

    if ((stream = fopen(dest, "a+")) == NULL) {
        vfile_err_fn("fopen %s: %m\n", dest);
        return -1;
    }

    if (curl_easy_setopt(curlh, CURLOPT_FILE, stream) != 0) {
        vfile_err_fn(errmsg);
        fclose(stream);
        return -1;
    };

    if (fstat(fileno(stream), &st) != 0) {
        vfile_err_fn("fstat %s: %m\n", dest);
        fclose(stream);
        return -1;
    }
    
    //printf("resume from %d\n", st.st_size);
    curl_easy_setopt(curlh, CURLOPT_RESUME_FROM, st.st_size);
    bar.resume_from = st.st_size;
    
    curl_rc = curl_easy_perform(curlh);
    fclose(stream);
    
    if (bar.state == PBAR_ST_RUNNING || bar.state == PBAR_ST_FINISHED)
         vfile_msg_fn("_\n");

    if (curl_rc != CURLE_OK) {
        char *p;
        
        if (*err == '\0') {
            snprintf(err, sizeof(err), "%s", "unknown error");
            
        } else if ((p = strchr(err, '\n'))) { /* curl w/o */
            *p = '\0';
            if (p != err && *(p - 1) == '\r')
                *(p - 1) = '\0';
        }
        
        vfile_err_fn("curl: %s [%d]\n", err, curl_rc);
        unlink(dest);
    }
    
    return curl_rc;
}


int vfile_curl_fetch(const char *dest, const char *url)
{
    int curl_rc, vf_errno = 0;

    curl_rc = do_vfile_curl_fetch(dest, url);

    if (curl_rc == CURLE_FTP_BAD_DOWNLOAD_RESUME ||
        curl_rc == CURLE_FTP_COULDNT_USE_REST ||
        curl_rc == CURLE_HTTP_RANGE_ERROR) {

        curl_rc = do_vfile_curl_fetch(dest, url);
    }

    if (curl_rc != CURLE_OK)
        vf_errno = EIO;
    
    if (curl_rc == CURLE_HTTP_NOT_FOUND ||
        curl_rc == CURLE_FTP_COULDNT_RETR_FILE)
        vf_errno = ENOENT;

    if (vf_errno)
        vfile_set_errno(modname, vf_errno);
    
    return curl_rc == CURLE_OK;
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

/* progress() taken from curl, modified by me */

/* 
 * Copyright (C) 2000, Daniel Stenberg, <daniel@haxx.se>, et al.
 *
 * In order to be useful for every potential user, curl and libcurl are
 * dual-licensed under the MPL and the MIT/X-derivate licenses.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the MPL or the MIT/X-derivate
 * licenses. You may pick one of these licenses.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 * $Id$
 *****************************************************************************/

static
int progress (void *clientp, size_t dltotal, size_t dlnow,
              size_t unused0, size_t unused1)
{
  /* The original progress-bar source code was written for curl by Lars Aas,
     and this new edition inherites some of his concepts. */
    struct progress_bar *bar;
    char   line[256], outline[256], format[40];
    float  frac, percent;
    int    barwidth, n;
    size_t total;
    double total_size, amount_size;


    unused0 = unused1;
    bar = (struct progress_bar*)clientp;
    

    if (bar->state == PBAR_ST_DISABLED)
        return 0;
    
    
    

    if (bar->state == PBAR_ST_VIRGIN) {
        bar->total_downloaded = dltotal;
        if (dltotal == dlnow  ||   /* downloaded before progress() call */
            dltotal < 512) {       /* too small to show to */
            bar->state = PBAR_ST_DISABLED;
            return 0;
        }
        bar->state = PBAR_ST_RUNNING;
    }

    total = dltotal;
    total_size = total;
    amount_size = bar->point = dlnow;
    if (total && total < bar->point) {    /* cURL w/o  */
        vfile_err_fn("cURL bug detected: current size %d, total size %d\n",
                     bar->point, total);
        bar->point = total;
        amount_size = total;
    }

#define HASH_SIZE 4096
    if (total == 0) {           /* what is this? */
        int prevblock = bar->prev / HASH_SIZE;
        int thisblock = bar->point / HASH_SIZE;
        while ( thisblock > prevblock ) {
            vfile_msg_fn("_.");
            prevblock++;
        }
        
    } else if (bar->state == PBAR_ST_RUNNING) {
        if (total == bar->point)
            bar->state = PBAR_ST_FINISHED;
        
        bar->state = 1;         
        frac = (float) bar->point / (float) total;
        percent = frac * 100.0f;

        if (bar->width != PROGRESSBAR_WIDTH) {
            vfile_err_fn("cURL bug detected: memory overrun\n");
            bar->width = PROGRESSBAR_WIDTH;
        }
        
        barwidth = bar->width - 7;
        n = (int) (((float)barwidth) * frac);

        if (n <= (int)bar->prev_n)
            return 0;
        
        n_assert(n < (int)sizeof(line) - 1);
            
        if (!bar->is_tty) {
            int k;
            
            k = n - bar->prev_n;
            memset(line, '.', k);
            line[k] = '\0';
            vfile_msg_fn("_%s", line);
            
        } else {
            char unit_line[19], amount_str[16], total_str[16];
            int nn;
            

            nbytes2str(total_str, sizeof(total_str), total);
            nbytes2str(amount_str, sizeof(amount_str), amount_size);

            if (bar->state == PBAR_ST_FINISHED)
                nn = snprintf(unit_line, sizeof(unit_line), "[%s]", total_str);
            else 
                nn = snprintf(unit_line, sizeof(unit_line), "[%s of %s]",
                              amount_str, total_str);
            
            memset(&unit_line[nn], ' ', sizeof(unit_line) - nn - 1);
            unit_line[sizeof(unit_line) - 1] = '\0';
            
            memset(line, '.', n);
            line[n] = '\0';

            snprintf(format, sizeof(format), "%%-%ds %%5.1f%%%% %%s", barwidth );
            snprintf(outline, sizeof(outline), format, line, percent, unit_line);
            vfile_msg_fn("_\r%s", outline);
        }
        bar->prev_n = n;
        if (total == bar->point)
            bar->state = PBAR_ST_FINISHED;
    }
    
        
    bar->prev = bar->point;
    if (bar->state == PBAR_ST_FINISHED) {
        if (bar->is_tty)
            vfile_msg_fn("_\n");
        else
            vfile_msg_fn("_Done\n");
        bar->state = PBAR_ST_TERMINATED;
    }

    return 0;
}

