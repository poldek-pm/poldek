# $Id$

SUBDIRS = dir pdir hdrl rpmdb pndir
INCLUDES = @TRURL_INCLUDE@ -I$(top_srcdir)
AM_CFLAGS = @AM_CFLAGS@

lib_LIBRARIES        = libpkgdir.a

libpkgdir_a_SOURCES  = \
			source.c source.h \
			pkgdir.c pkgdir.h \
			pkgdir_patch.c    \
			pkgdir_clean.c    \
			mod.c             \
			pkg_store.c pkg_restore.c  

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
