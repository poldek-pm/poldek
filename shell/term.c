/* 
  Copyright (C) 2001 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <stdio.h>
#include <stdlib.h>

#include <ncurses/curses.h>
#include <ncurses/term.h>

#include "log.h"
#include "term.h"

/*
#define PRCOLOR_BLACK    0
#define PRCOLOR_RED      1
#define PRCOLOR_GREEN    2
#define PRCOLOR_YELLOW   3
#define PRCOLOR_BLUE     4
#define PRCOLOR_MAGNETA  5
#define PRCOLOR_CYAN     6
*/

struct prcolor {
    int  no;                    /* curses def */
    char seq[64];
};

struct prcolor pr_colors[] = {
    { COLOR_BLACK,   {'\0'} },
    { COLOR_RED,     {'\0'} },
    { COLOR_GREEN,   {'\0'} },
    { COLOR_YELLOW,  {'\0'} },
    { COLOR_BLUE,    {'\0'} },
    { COLOR_MAGENTA, {'\0'} },
    { COLOR_CYAN,    {'\0'} },
    { COLOR_WHITE,   {'\0'} },
    { 0, {'\0'} }, 
};

static char color_default[64] = {'\0'};
static char at_bold[64] = {'\0'};
static char at_no_attr[64] = {'\0'};

static char *get_color(char *buf, int size, int color) 
{
    char *s;
    
    if ((s = tigetstr("setaf")) == NULL || s == (char*)-1)
        return NULL;
    
    if ((s = tparm(s, color, 0)) != NULL)
        snprintf(buf, size, "%s", s);
    else
        buf = NULL;
    
    return buf;
}


void color_vlog(int pri, int indent, const char *fmt, va_list args)
{
    if (pri & LOGERR) 
        printf("%s%sERR: %s%s", at_bold, pr_colors[COLOR_RED].seq, color_default, at_no_attr);

    if (pri & LOGWARN)
        printf("%s%sWARN: %s%s", at_bold, pr_colors[COLOR_RED].seq, color_default, at_no_attr);
    
    do_vlog(pri, indent, fmt, args);
}

    
int vprintf_c(int color, const char *fmt, va_list args)
{
    int n, isbold = 0;

    if (color & PRAT_BOLD) {
        n += printf("%s", at_bold);
        color &= ~PRAT_BOLD;
        isbold = 1;
    }

    if (*pr_colors[color].seq)
        printf("%s", pr_colors[color].seq);

    n = vprintf(fmt, args);
    
    if (*pr_colors[color].seq)
        printf("%s%s", color_default, isbold ? at_no_attr:"");

    printf("%s", at_no_attr);

    return n;
}
    

int printf_c(int color, const char *fmt, ...)
{
    va_list args;
    int n = 0;
    
    va_start(args, fmt);
    n = vprintf_c(color, fmt, args);
    va_end(args);
    return n;
}

int snprintf_c(int color, char *buf, int size, const char *fmt, ...)
{
    va_list args;
    int n = 0, isbold = 0;
    
    va_start(args, fmt);

    if (color & PRAT_BOLD) {
        n += snprintf(buf, size, "%s", at_bold);
        color &= ~PRAT_BOLD;
        isbold = 1;
    }

    if (*pr_colors[color].seq)
        n += snprintf(buf, size, "%s", pr_colors[color].seq);

    n += vsnprintf(&buf[n], size - n, fmt, args);
    
    if (*pr_colors[color].seq) 
        n += snprintf(&buf[n], size - n, "%s%s", color_default, isbold ? at_no_attr:"");
        
    va_end(args);
    return n;
}

    

int puts_c(int color, const char *s) 
{
    int isbold = 0;
    
    if (color & PRAT_BOLD) {
        printf("%s", at_bold);
        color &= ~PRAT_BOLD;
    }
    
    return printf("%s%s%s%s", pr_colors[color].seq, s, color_default,
                  isbold ? at_no_attr:"");
}


void term_init(void) 
{
    int i, rc, n;
    char *term, *s;
    
    term = getenv("TERM");
    
    if (term == NULL || *term == '\0') {
	log(LOGERR, "No value for $TERM\n");
        return;
    }
    
    if (setupterm(term, fileno(stdout), &rc) != OK && rc <= 0) {
	log(LOGERR, "Unknown terminal \"%s\"", term);
        return;
    }

    for (i=0; i<7; i++) 
        get_color(pr_colors[i].seq, sizeof(pr_colors[i].seq), pr_colors[i].no);
        
    s = tigetstr("op");
    if (s != NULL && s != (char*)-1) 
        snprintf(color_default, sizeof(color_default), "%s", s);

    s = tigetstr("bold");
    if (s != NULL && s != (char*)-1) 
        n = snprintf(at_bold, sizeof(at_bold), "%s", s);

    /*s = tigetstr("dim");
      if (s != NULL && s != (char*)-1)
      snprintf(&at_bold[n], sizeof(at_bold) - n, "%s", s);*/

    s = tigetstr("sgr0");
    if (s != NULL && s != (char*)-1)
        snprintf(at_no_attr, sizeof(at_no_attr), "%s", s);
    
    

    log_sopenlog(stdout, 0, NULL);
    log_set_vlogfn(color_vlog);
}
    
#if 0
int main(int argc, char *argv[])
{
    term_init();
    printc(color_red, "RED\n");
    printc(color_green, "GREEN\n");
    printc(color_yellow, "YELLOW\n");
    printc(color_default, "NORMAL\n");
    printf("NORMAL\n");
    
    return 0;
}
#endif
