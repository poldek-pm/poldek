# $Id$

#SUBDIRS = dir pdir hdrl rpmdb pndir yum
INCLUDES = @TRURL_INCLUDE@ @TNDB_INCLUDE@ -I$(top_srcdir)
AM_CFLAGS = @AM_CFLAGS@

noinst_LTLIBRARIES = libpkgdb.la

libpkgdb_la_SOURCES  = pkgdb.h pkgdb.c 

#pkgdir_modules = dir/libpkgdir_dir.la      \
#		 hdrl/libpkgdir_hdrl.la    \
#	         rpmdb/libpkgdir_rpmdb.la  \
#		 pdir/libpkgdir_pdir.la    \
#		 pndir/libpkgdir_pndir.la  \
#		 yum/libpkgdir_yum.la


#libpkgdir_la_LIBADD  = $(pkgdir_modules)

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
