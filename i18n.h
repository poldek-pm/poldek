/* $Id$ */
#ifndef  POLDEK_i18h_H
#define  POLDEK_i18h_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

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
