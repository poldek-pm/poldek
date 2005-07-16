# $Id$

SUBDIRS = dir pdir hdrl rpmdb pndir yum @METADATA_DIR@
INCLUDES = @TRURL_INCLUDE@ @TNDB_INCLUDE@ -I$(top_srcdir)
AM_CFLAGS = @AM_CFLAGS@

LIBS=
noinst_LTLIBRARIES = libpkgdir.la

libpkgdir_la_SOURCES  = \
			source.c source.h \
			pkgdir.c pkgdir.h pkgdir_intern.h \
			pkgdir_patch.c    \
			pkgdir_clean.c    \
			mod.c             \
			pkg_store.c pkg_restore.c pkg_store.h 

pkgdir_modules = dir/libpkgdir_dir.la  \
		 hdrl/libpkgdir_hdrl.la    \
		 rpmdb/libpkgdir_rpmdb.la  \
		 pdir/libpkgdir_pdir.la    \
		 pndir/libpkgdir_pndir.la  \
		 yum/libpkgdir_yum.la @METADATA_LIB@


libpkgdir_la_LIBADD  = $(pkgdir_modules)

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
