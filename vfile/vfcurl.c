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

#include "vfile.h"

static int progress (void *clientp, size_t dltotal, size_t dlnow,
                     size_t ultotal, size_t ulnow);


struct progress_bar {
    size_t  total;
    size_t  prev;
    size_t  point;
    int     width;
    int     anybfetched;
};


static CURL *curlh = NULL;


int vfile_curl_init(void) 
{
    if ((curlh = curl_easy_init()) == NULL)
        return 0;
    
#if 0                           /* curl bug */
    if (curl_easy_setopt(curlh, CURLOPT_MAXCONNECTS, 16) != 0) {
        vfile_err_fn("curl_easy_setopt failed");
        return 0;
    };
#endif

    if (curl_easy_setopt(curlh, CURLOPT_MUTE, *vfile_verbose > 1 ? 1:0) != 0) {
        vfile_err_fn("curl_easy_setopt failed");
        return 0;
    };

    if (curl_easy_setopt(curlh, CURLOPT_VERBOSE, *vfile_verbose > 2 ? 1:0) != 0) {
        vfile_err_fn("curl_easy_setopt failed");
        return 0;
    };

    if (curl_easy_setopt(curlh, CURLOPT_PROGRESSFUNCTION, progress) != 0) {
        vfile_err_fn("curl_easy_setopt failed");
        return 0;
    };

    return 1;
}


int vfile_curl_fetch(const char *dest, const char *url)
{
    struct progress_bar bar = {0, 0, 0, 75, 0};
    struct stat st;
    FILE *stream;
    int rc = 1;
    char err[CURL_ERROR_SIZE * 2];

    *err = '\0';

    if (curl_easy_setopt(curlh, CURLOPT_ERRORBUFFER, err) != 0) {
        vfile_err_fn("curl_easy_setopt failed");
        return 0;
    }
    
    if (curl_easy_setopt(curlh, CURLOPT_PROGRESSDATA, &bar) != 0) {
        vfile_err_fn("curl_easy_setopt failed");
        return 0;
    }
    
    if (curl_easy_setopt(curlh, CURLOPT_URL, url) != 0) {
        vfile_err_fn("curl_easy_setopt failed");
        return 0;
    };

    if ((stream = fopen(dest, "a+")) == NULL) {
        vfile_err_fn("fopen %s: %m\n", dest);
        return 0;
    }

    if (curl_easy_setopt(curlh, CURLOPT_FILE, stream) != 0) {
        vfile_err_fn("curl_easy_setopt failed\n");
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

    if (rc == CURLE_OK) {
        rc = 1;
        
    } else {
        vfile_err_fn("%s: curl failed: %s\n", url, err);
        rc = 0;
    }

    fclose(stream);
    return rc;
}



/* progress() taken from curl: */

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
    char line[256];
    char outline[256];
    char format[40];
    float frac;
    float percent;
    int barwidth;
    size_t total;
    int num;
    int i;
    
        
    bar = (struct progress_bar *)clientp;
    total = dltotal + ultotal;

    bar->point = dlnow + ulnow; /* we've come this far */

    if (total == 0) {
        int prevblock = bar->prev / 1024;
        int thisblock = bar->point / 1024;
        while ( thisblock > prevblock ) {
            vfile_msg_fn(".");
            bar->anybfetched = 1;
            prevblock++;
        }
        
    } else {
        frac = (float) bar->point / (float) total;
        percent = frac * 100.0f;
        barwidth = bar->width - 7;
        num = (int) (((float)barwidth) * frac);
        i = 0;
        for ( i = 0; i < num; i++ ) {
            line[i] = '.';
        }
        line[i] = '\0';
        snprintf(format, sizeof(format), "%%-%ds %%5.1f%%%%", barwidth );
        snprintf(outline, sizeof(outline), format, line, percent );
        vfile_msg_fn("\r%s", outline);
        
    }
    bar->prev = bar->point;
    
    if (total == bar->point && bar->anybfetched == 1)
        vfile_msg_fn("\n");

    return 0;
}

