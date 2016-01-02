/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef  POLDEK_i18h_H
#define  POLDEK_i18h_H

#if HAVE_LOCALE_H
# include <locale.h>
#endif

#if !HAVE_SETLOCALE
# define setlocale(category, locale)
#endif

#if ENABLE_NLS
# include <libintl.h>
# define _(foo)  gettext(foo)
# if !HAVE_NGETTEXT
#  define ngettext(foo, foo_plural, n) (foo_plural)
# endif
# if defined(N_)                /* popt.h defines N_... */
#   undef N_
# endif
# define N_(foo) (foo)
# define F_(foo) gettext(foo)
#else
# undef bindtextdomain
# define bindtextdomain(domain, directory)
# undef textdomain
# define textdomain(domain)
# define _(foo)  (foo)
# define ngettext(foo, foo_plural, n) (foo_plural)
# define N_(foo) (foo)
# define F_(foo) (foo)
#endif


#endif
