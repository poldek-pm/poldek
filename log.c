/* 
  Copyright (C) 2000 - 2002 Pawel A. Gajda (mis@k2.net.pl)
 
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
#include <time.h>

#include <trurl/nstr.h>
#include "term.h"
#include "log.h"

int verbose;
int verbose_file;

static char       l_prefix[64];
static FILE      *l_stream = NULL, *l_fstream = NULL;


static void vlog_tty(int pri, const char *fmt, va_list args);


void log(int pri, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vlog(pri, 0, fmt, args);
    va_end(args);
}


void vlog(int pri, int indent, const char *fmt, va_list args)
{
    char buf[1024];
    int n = 0, flags, is_cont = 0;
    
    if (*fmt == '_') {
        fmt++;
        is_cont = 1;
        
    } else if (*fmt == '\n') {
        buf[n++] = '\n';
        fmt++;
    }

    if (indent > 0) {
        memset(&buf[n], ' ', indent);
        n += indent;
    }

    buf[n] = '\0';

    /* revert LOG[TTY|FLAGS]  */
    flags = LOGTTY | LOGFILE;
    if (pri & LOGTTY)
        flags &= ~LOGFILE;
    
    else if (pri & LOGFILE)
        flags &= ~LOGTTY;
    
    
    if (flags & LOGTTY) {
        fprintf(l_stream, "%s", buf);
        if (*fmt)
            vlog_tty(pri, fmt, args);
        fflush(l_stream);
    }

    if (flags & LOGFILE && l_fstream) {
        
        if (*fmt == '\0') {
            fprintf(l_fstream, "%s", buf);
            
        } else {
            if (!is_cont) {
                char timbuf[64];
                time_t t;
            
                t = time(NULL);
                strftime(timbuf, sizeof(timbuf), "%Y/%m/%d %H:%M:%S ",
                         localtime(&t));

                fprintf(l_fstream, "%s", timbuf);
            }
            fprintf(l_fstream, "%s", buf);
            vfprintf(l_fstream, fmt, args);
            fflush(l_fstream);
        }
    }
}


void log_msg(const char *fmt, ...) 
{
    va_list args;
    if (verbose > -1) {
        va_start(args, fmt);
        vlog(LOGINFO, 0, fmt, args);
        va_end(args);
    }
}

void log_err(const char *fmt, ...) 
{
    va_list args;
    if (verbose > -1) {
        va_start(args, fmt);
        vlog(LOGERR, 0, fmt, args);
        va_end(args);
    }
}



void log_msg_i(int indent, const char *fmt, ...) 
{
    va_list args;
    
    va_start(args, fmt);
    vlog(LOGINFO, indent, fmt, args);
    va_end(args);
}


int log_init(const char *pathname, FILE *tty, char *prefix)
{
    int is_not_stdstream = l_stream != stdout && l_stream != stderr;

    
    if (l_stream != NULL && is_not_stdstream) 
        fclose(l_stream);

    if (l_fstream)
        fclose(l_fstream);
    
    l_stream = tty;
    l_fstream = fopen(pathname, "a");

    l_prefix[0] = '\0';
    if (prefix) {
        n_strncpy(l_prefix, prefix, sizeof(l_prefix) - 4);
        strcat(&l_prefix[strlen(l_prefix)], ": ");
    }
    
    return l_fstream != NULL;
}


void log_closelog(void)
{
    if (l_stream != NULL && l_stream != stdout && l_stream != stderr) 
	fclose(l_stream);
    
    l_stream = NULL;

    if (l_fstream) {
        fclose(l_fstream);
        l_fstream = NULL;
    }
}


FILE *log_stream(void) 
{
    return l_stream ? l_stream : stdout;
}


static
void vlog_tty(int pri, const char *fmt, va_list args)
{
    char buf[2048];
    int n = 0;
    
    if (pri & LOGERR)
        n = snprintf_c(PRCOLOR_RED | PRAT_BOLD, buf, sizeof(buf), "error: ");
    
    else if (pri & LOGWARN)
        n = snprintf_c(PRCOLOR_RED | PRAT_BOLD, buf, sizeof(buf), "warn: ");

    if (n > 0)
        fprintf(l_stream, "%s", buf);
    vfprintf(l_stream, fmt, args);
}

