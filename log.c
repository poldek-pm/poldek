/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>

#include <trurl/nstr.h>

#include "log.h"

int verbose;

static char       l_prefix[64];
static FILE       *l_stream = NULL;
static unsigned   l_mask = (unsigned)~0;
static int (*l_vprintf)(const char *format, va_list args);


/*
 */
void log(int pri, const char *fmt, ...)
{
    va_list args;

    if (verbose < 0 && (l_stream == stdout || l_stream == stderr))
        return;
        
    va_start(args, fmt);
    vlog(pri, 0, fmt, args);
    va_end(args);
}

/*
 */
void vlog(int pri, int indent_size, const char *fmt, va_list args)
{
    if (l_stream == NULL) {
        char path[256];
        sprintf("%s%d", path, "/var/tmp/log", getpid());
        log_openlog(path, 0, "");
        if (l_stream == NULL)
            return;
    }
    

    if (*fmt == '_') 
        fmt++;
    
    else if (*fmt == '$') {
        fputs("\n", l_stream);
        fmt++;
        
    } else if (*fmt != '\n') {
        if (pri & LOGDEBUG) 
            fputs("* ", l_stream);
        else 
            fputs(l_prefix, l_stream);
    }

    if (indent_size > 0) {
        if (indent_size < 10) {
            int i;
            for (i=0; i<indent_size; i++) 
                fputc(' ', l_stream);
        } else {
            char buf[indent_size + 1];
            memset(buf, ' ', indent_size);
            buf[indent_size] = '\0';
            fputs(buf, l_stream);
        }
    }
    
    vfprintf(l_stream, fmt, args);
    fflush(l_stream);
}


void log_msg(const char *fmt, ...) 
{
    va_list args;
    
    va_start(args, fmt);
    vlog(LOGDEBUG, 0, fmt, args);
    va_end(args);
}


void log_msg_i(int indent_size, const char *fmt, ...) 
{
    va_list args;
    
    va_start(args, fmt);
    vlog(LOGDEBUG, indent_size, fmt, args);
    va_end(args);
}


/*
  open_log
 */
int log_openlog(const char *pathname, unsigned mask, char *prefix)

{
    int is_not_stdstream = l_stream != stdout && l_stream != stderr;
    
    if(l_stream != NULL && is_not_stdstream) 
        fclose(l_stream);

    l_stream = fopen(pathname, "w");

    if (mask) 
        l_mask = mask;

    n_strncpy(l_prefix, prefix, sizeof(l_prefix) - 4);
    strcat(&l_prefix[strlen(l_prefix)], ": ");
    return l_stream != NULL;
}

         
int log_sopenlog(FILE *stream, unsigned mask, char *prefix)
{
    int is_not_stdstream = l_stream != stdout && l_stream != stderr;
    
    
    if(l_stream != NULL && is_not_stdstream) 
        fclose(l_stream);
    
    l_stream = stream;

    if (mask) 
        l_mask = mask;

    n_strncpy(l_prefix, prefix, sizeof(l_prefix) - 4);
    strcat(&l_prefix[strlen(l_prefix)], ": ");
    return l_stream != NULL;
}


/*
  
 */
void log_closelog(void)
{
    if (l_stream != NULL && l_stream != stdout && l_stream != stderr) 
	fclose(l_stream);
    
    l_stream = NULL;
}


FILE *log_stream(void) 
{
    return l_stream ? l_stream : stderr;
}


void log_set_vprintf(int (*vprintffn)(const char *format, va_list args))
{
    l_vprintf = vprintffn;
}
