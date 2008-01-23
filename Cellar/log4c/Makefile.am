# $Id$

AM_CFLAGS = @AM_CFLAGS@

LIBS = 
noinst_LTLIBRARIES   = liblog4c.la
libsigint_la_SOURCES = log4c.c log4c.h defaultAppender.c

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
