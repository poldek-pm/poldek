# $Id$

INCLUDES = @TRURL_INCLUDE@ -I$(top_srcdir) -I$(top_srcdir)/pkgdir

noinst_LTLIBRARIES = libpkgdir_hdrl.la
libpkgdir_hdrl_la_SOURCES = hdrl.c

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
