# $Id$

INCLUDES = @TRURL_INCLUDE@ -I$(top_srcdir)
AM_CFLAGS = @AM_CFLAGS@

lib_LIBRARIES      = libpkg_rpm.a

libpkg_rpm_a_SOURCES  = rpmdb_it.c rpmdb_it.h   \
			rpmdb_depdirs.c         \
			install.c  signature.c  \
			misc.c                  \
			rpm.c rpm.h             \
			rpmhdr.c rpmhdr.h       \
			rpminstall.c            \
			rpm_pkg_ld.c rpm_pkg_ld.h   	

bin_PROGRAMS    = rpmvercmp

rpmvercmp_SOURCES   = rpmvercmp.c
rpmvercmp_LDADD     = $(LDADD_)
rpmvercmp_LIBS      = -lrpm
rpmvercmp_LDFLAGS   = -lrpm


dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
