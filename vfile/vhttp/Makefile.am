# $Id$

INCLUDES = @TRURL_INCLUDE@
lib_LIBRARIES      = libvhttp.a 
libvhttp_a_SOURCES  = vhttp.c http.c

pkgincludedir = $(includedir)/vhttp
pkginclude_HEADERS = vhttp.h http.h 

noinst_PROGRAMS    = test_vhttp
test_vhttp_SOURCES  = test_vhttp.c
test_vhttp_LDADD    = libvhttp.a @TRURL_LDFLAG@

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
