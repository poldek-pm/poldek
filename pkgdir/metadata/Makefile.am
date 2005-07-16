# $Id$

INCLUDES = @TRURL_INCLUDE@ -I$(top_srcdir) -I$(top_srcdir)/pkgdir \
           @LIBXML2_INCLUDE@
LIBS = @LIBXML2_LIB@
noinst_LTLIBRARIES = libpkgdir_metadata.la
libpkgdir_metadata_la_SOURCES = metadata.c load.c load.h

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
