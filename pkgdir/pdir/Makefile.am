NULL =

AM_CPPFLAGS = @TRURL_INCLUDE@ -I$(top_srcdir) -I$(top_srcdir)/pkgdir
AM_CFLAGS = @AM_CFLAGS@

LIBS=
noinst_LTLIBRARIES = libpkgdir_pdir.la
libpkgdir_pdir_la_SOURCES = 		\
	pdir.c				\
	pdir.h				\
	digest.c			\
	save.c				\
	tags.c				\
	pdir_pkg_store.c		\
	pdir_pkg_restore.c		\
	$(NULL)

libpkgdir_pdir_la_CFLAGS =		\
	$(WARNINGFLAGS_C)		\
	$(NULL)

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
