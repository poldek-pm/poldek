# $Id$

SUBDIRS = rpm pset
INCLUDES = @TRURL_INCLUDE@ @TNDB_INCLUDE@ -I$(top_srcdir)
AM_CFLAGS = @AM_CFLAGS@

noinst_LTLIBRARIES = libpm.la
libpm_la_SOURCES  = \
		pm.c pm.h \
		mod.h mod.c pkgdb.c

pm_modules = rpm/libpm_rpm.la pset/libpm_pset.la

libpm_la_LIBADD  = $(pm_modules)

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
