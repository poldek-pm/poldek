/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
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

#include "vfile.h"

#define PROGRESSBAR_WIDTH 75

static int progress (void *clientp, size_t dltotal, size_t dlnow,
                     size_t ultotal, size_t ulnow);


struct progress_bar {
    size_t  total;
    size_t  prev;
    size_t  point;
    int     width;
    int     state;
    int     is_tty;
    size_t  prev_n;
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

    if (curl_easy_setopt(curlh, CURLOPT_TIMEOUT, 120) != 0) { /* read to */
        vfile_err_fn(errmsg);
        return 0;
    }

    if (curl_easy_setopt(curlh, CURLOPT_CONNECTTIMEOUT, 120) != 0) {
        vfile_err_fn(errmsg);
        return 0;
    }

    return 1;
}


int vfile_curl_fetch(const char *dest, const char *url)
{
    struct progress_bar bar = {0, 0, 0, PROGRESSBAR_WIDTH, 0, 0, 0};
    struct stat st;
    FILE *stream;
    int rc = 1;
    char err[CURL_ERROR_SIZE * 2];
    char *errmsg = "curl_easy_setopt failed";

    
    bar.is_tty = isatty(fileno(stdout));
    

    *err = '\0';

    if (curl_easy_setopt(curlh, CURLOPT_ERRORBUFFER, err) != 0) {
        vfile_err_fn(errmsg);
        return 0;
    }

    if (curl_easy_setopt(curlh, CURLOPT_PROGRESSDATA, &bar) != 0) {
        vfile_err_fn(errmsg);
        return 0;
    }
    
    if (curl_easy_setopt(curlh, CURLOPT_URL, url) != 0) {
        vfile_err_fn(errmsg);
        return 0;
    }

    if ((stream = fopen(dest, "a+")) == NULL) {
        vfile_err_fn("fopen %s: %m\n", dest);
        return 0;
    }

    if (curl_easy_setopt(curlh, CURLOPT_FILE, stream) != 0) {
        vfile_err_fn(errmsg);
        fclose(stream);
        return 0;
    };

    if (fstat(fileno(stream), &st) != 0) {
        vfile_err_fn("fstat %s: %m\n", dest);
        fclose(stream);
        return 0;
    }
    	
    curl_easy_setopt(curlh, CURLOPT_RESUME_FROM, st.st_size);

    rc = curl_easy_perform(curlh);

    if (bar.state < 3)
         vfile_msg_fn("\n");

    if (rc == CURLE_OK) {
        rc = 1;
        
    } else {
        char *p;
        
        
        if (*err == '\0') {
            snprintf(err, sizeof(err), "%s", "unknown error");
            
        } else if ((p = strchr(err, '\n'))) { /* curl w/o */
            *p = '\0';
            if (p != err && *(p - 1) == '\r')
                *(p - 1) = '\0';
        }
        
        vfile_err_fn("curl: %s\n", err);
        unlink(dest);
        rc = 0;
    }

    fclose(stream);
    return rc;
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
              size_t ultotal, size_t ulnow)
{
  /* The original progress-bar source code was written for curl by Lars Aas,
     and this new edition inherites some of his concepts. */
    struct progress_bar *bar;
    char   line[256], outline[256], format[40];
    float  frac, percent;
    int    barwidth, n;
    size_t total;
    
    
    bar = (struct progress_bar*)clientp;
    total = dltotal + ultotal;

    

    bar->point = dlnow + ulnow; /* we've come this far */
    
    if (total && total < bar->point) {    /* cURL w/o  */
        vfile_err_fn("cURL bug detected: current size %d, total size %d\n", bar->point, total);
        bar->point = total;
    }

#define HASH_SIZE 4096
    if (total == 0) {
        int prevblock = bar->prev / HASH_SIZE;
        int thisblock = bar->point / HASH_SIZE;
        while ( thisblock > prevblock ) {
            vfile_msg_fn(".");
            prevblock++;
        }
        
    } else if (bar->state < 2) {
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
            vfile_msg_fn("%s", line);
            
        } else {
            memset(line, '.', n);
            line[n] = '\0';
            
            snprintf(format, sizeof(format), "%%-%ds %%5.1f%%%%", barwidth );
            snprintf(outline, sizeof(outline), format, line, percent );
            
            vfile_msg_fn("\r%s", outline);
        }
        bar->prev_n = n;
        if (total == bar->point)
            bar->state = 2;
    }
    
        
    bar->prev = bar->point;
    
    if (total == bar->point && bar->state == 2) {
        if (bar->is_tty)
            vfile_msg_fn("\n");
        else
            vfile_msg_fn(" Done\n");
        bar->state = 3;
    }

    return 0;
}

