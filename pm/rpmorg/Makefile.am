NULL =

AM_CPPFLAGS = @TRURL_INCLUDE@ -I$(top_srcdir)
AM_CFLAGS = @AM_CFLAGS@

noinst_LTLIBRARIES = libpm_rpm.la

libpm_rpm_la_SOURCES =			\
	mod.c				\
	rpmdb_it.c			\
	install.c			\
	signature.c			\
	misc.c				\
	rpm.c				\
	pm_rpm.h			\
	rpmhdr.c			\
	rpminstall.c			\
	rpm_pkg_ld.c			\
	$(NULL)

EXTRA_DIST      =
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
