NULL =

AM_CPPFLAGS = -I$(top_srcdir) @TRURL_INCLUDE@
AM_CFLAGS = @AM_CFLAGS@

noinst_LTLIBRARIES = libpm_rpm.la

libpm_rpm_la_SOURCES =			\
	mod.c				\
	rpmdb_it.c			\
	rpmdb_depdirs.c			\
	install.c			\
	signature.c			\
	misc.c				\
	rpm.c				\
	pm_rpm.h			\
	rpmhdr.c			\
	rpminstall.c			\
	rpm_pkg_ld.c			\
	$(NULL)

EXTRA_DIST      = signature4.c signature5.c
bin_PROGRAMS    = rpmvercmp

rpmvercmp_SOURCES =			\
	rpmvercmp.c			\
	$(NULL)

rpmvercmp_LDADD =			\
	-lrpm				\
	$(NULL)

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1
