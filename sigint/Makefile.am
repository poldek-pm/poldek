# $Id$

INCLUDES = @TRURL_INCLUDE@ -I$(top_srcdir)
lib_LIBRARIES      = libsigint.a
libsigint_a_SOURCES  = sigint.h sigint.c

#pkgincludedir = $(includedir)/vftp
#pkginclude_HEADERS = vftp.h ftp.h 

noinst_PROGRAMS    = 

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
