# $Id$

INCLUDES = @TRURL_INCLUDE@ @TNDB_INCLUDE@ -I$(top_srcdir)
AM_CFLAGS = @AM_CFLAGS@

LIBS=
noinst_LTLIBRARIES = libinstall.la
libinstall_la_SOURCES = install.h install.c  \
                        ictx.c ictx.h mark.c misc.c \
                        conflicts.c icaps.c  \
	  	        obsoletes.c requirements.c \
                        process.c

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
