# $Id$

INCLUDES = @TRURL_INCLUDE@ @TNDB_INCLUDE@ -I$(top_srcdir) -I$(top_srcdir)/pkgdir
AM_CFLAGS = @AM_CFLAGS@

lib_LIBRARIES      = libpkgdir_pndir.a
libpkgdir_pndir_a_SOURCES = pndir.c pndir.h digest.c update.c save.c tags.c

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
