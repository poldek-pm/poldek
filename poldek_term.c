/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <curses.h>
#include <term.h>

#include <trurl/nassert.h>
#include <trurl/n_snprintf.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "poldek_term.h"

static int term_width  = TERM_DEFAULT_WIDTH;
static int term_height = TERM_DEFAULT_HEIGHT;
static volatile sig_atomic_t winch_reached = 0;
static void (*orig_sigwinch_handler)(int) = NULL;
static int sigwinch_attached = 0;

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


int poldek_term_vprintf_c(int color, const char *fmt, va_list args)
{
    int n = 0, isbold = 0;

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


int poldek_term_printf_c(int color, const char *fmt, ...)
{
    va_list args;
    int n = 0;

    va_start(args, fmt);
    n = poldek_term_vprintf_c(color, fmt, args);
    va_end(args);
    return n;
}


int poldek_term_vsnprintf_c(int color, char *str, size_t size, const char *fmt,
                            va_list args)
{
    int n = 0, isbold = 0;

    if (color & PRAT_BOLD) {
        n += n_snprintf(&str[n], size - n, "%s", at_bold);
        color &= ~PRAT_BOLD;
        isbold = 1;
    }

    if (*pr_colors[color].seq)
        n += n_snprintf(&str[n], size - n, "%s", pr_colors[color].seq);

    n += n_vsnprintf(&str[n], size - n, fmt, args);

    if (*pr_colors[color].seq)
        n += n_snprintf(&str[n], size - n, "%s%s", color_default,
                      isbold ? at_no_attr:"");

    return n;
}


int poldek_term_snprintf_c(int color, char *str, size_t size,
                           const char *fmt, ...)
{
    va_list args;
    int n;

    va_start(args, fmt);
    n = poldek_term_vsnprintf_c(color, str, size, fmt, args);
    va_end(args);
    return n;
}



int poldek_term_puts_c(int color, const char *s)
{
    int isbold = 0;

    if (color & PRAT_BOLD) {
        printf("%s", at_bold);
        color &= ~PRAT_BOLD;
    }

    return printf("%s%s%s%s", pr_colors[color].seq, s, color_default,
                  isbold ? at_no_attr:"");
}

static void sig_winch(int signo)
{
    n_assert(signo == SIGWINCH);
    winch_reached = 1;
    signal(SIGWINCH, sig_winch);
}

int poldek_term_init(int force_color)
{
    int i, rc;
    char *term, *s;
    int istty = isatty(fileno(stdout));

    if (!istty && !force_color)
        return 0;

    if (sigwinch_attached)
        return 0;

    term = getenv("TERM");
    if (term == NULL || *term == '\0') {
        logn(LOGERR, _("$TERM undefined"));
        return 0;
    }

    if (setupterm(term, fileno(stdout), &rc) != OK && rc <= 0) {
        logn(LOGERR, _("%s: unknown terminal"), term);
        return 0;
    }

    for (i=0; i<7; i++)
        get_color(pr_colors[i].seq, sizeof(pr_colors[i].seq), pr_colors[i].no);

    s = tigetstr("op");
    if (s != NULL && s != (char*)-1)
        snprintf(color_default, sizeof(color_default), "%s", s);

    s = tigetstr("bold");
    if (s != NULL && s != (char*)-1)
        n_snprintf(at_bold, sizeof(at_bold), "%s", s);

    /*s = tigetstr("dim");
      if (s != NULL && s != (char*)-1)
      snprintf(&at_bold[n], sizeof(at_bold) - n, "%s", s);*/

    s = tigetstr("sgr0");
    if (s != NULL && s != (char*)-1)
        snprintf(at_no_attr, sizeof(at_no_attr), "%s", s);

    if (istty && !sigwinch_attached) {
        winch_reached = 1;
        poldek_term_get_width();
        orig_sigwinch_handler = signal(SIGWINCH, sig_winch);
        sigwinch_attached = 1;
    }

    return 1;
}


void poldek_term_destroy(void)
{
    signal(SIGWINCH, orig_sigwinch_handler);
}


static void update_term_width(void)
{
    struct winsize ws;

    if (winch_reached) {
        char tmp[256];
        if (ioctl(1, TIOCGWINSZ, &ws) == 0) {
            term_width  = ws.ws_col;
            term_height = ws.ws_row;

        } else {
            term_width  = TERM_DEFAULT_WIDTH;
            term_height = TERM_DEFAULT_HEIGHT;
        }

        snprintf(tmp, sizeof(tmp), "rmargin=%d", term_width - 1);
        setenv("ARGP_HELP_FMT", tmp, 1);
        winch_reached = 0;
    }
}

int poldek_term_get_width(void)
{
    update_term_width();
    return term_width;
}

int poldek_term_get_height(void)
{
    update_term_width();
    return term_height;
}


int poldek_term_ask(int fd, const char *validchrs, const char *msg)
{
    struct termios t, tmp;
    unsigned char c;

    if (!isatty(fd))
        return 0;

    tcgetattr(fd, &t);
    memcpy(&tmp, &t, sizeof(tmp));

    t.c_lflag &= ~(ICANON | ISIG | IEXTEN | ECHO);
    t.c_lflag &= ~(ECHOE | ECHOK | ECHONL|ECHOCTL|ECHOPRT|ECHOKE);
    t.c_iflag &= ~(IXON | IXANY | BRKINT);
    t.c_cc[VTIME] = 0;
    t.c_cc[VMIN] = 1;
    tcsetattr(0, TCSAFLUSH, &t);

    while (1) {
        if ((read(fd, &c, sizeof(c)) == 1) && strchr(validchrs, c))
            break;

	// map pgup/pgdown to backspace/tab
	if (c == 0x1b && (read(fd, &c, sizeof(c)) == 1) &&
	    c == 0x5b && (read(fd, &c, sizeof(c)) == 1)) {
	    if (c == 0x35 && (read(fd, &c, sizeof(c)) == 1) &&
		c == 0x7e && strchr(validchrs, 0x7f)) {
		    c = 0x7f;
		    break;
	    }
	    if (c == 0x36 && (read(fd, &c, sizeof(c)) == 1) &&
		c == 0x7e && strchr(validchrs, '\t')) {
		    c = '\t';
		    break;
	    }
	}


        // terminal lost - so prevent loop
        if (!isatty(fd))
            return 0;

        if (msg)
            printf("%s\n", msg);
    }

    tcsetattr(fd, TCSAFLUSH, &tmp);
    return c;
}


#if 0
int main(int argc, char *argv[])
{
    term_init(0);
    printc(color_red, "RED\n");
    printc(color_green, "GREEN\n");
    printc(color_yellow, "YELLOW\n");
    printc(color_default, "NORMAL\n");
    printf("NORMAL\n");

    return 0;
}
#endif
