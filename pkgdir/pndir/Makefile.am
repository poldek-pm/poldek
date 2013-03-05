NULL =

AM_CPPFLAGS = @TRURL_INCLUDE@ @TNDB_INCLUDE@ -I$(top_srcdir) -I$(top_srcdir)/pkgdir
AM_CFLAGS = @AM_CFLAGS@
AM_LDFLAGS = @LDFLAGS@ @TNDB_LDFLAG@
LIBS = -ltrurl -ltndb

noinst_LTLIBRARIES = libpkgdir_pndir.la
libpkgdir_pndir_la_SOURCES =			\
	pndir.c					\
	pndir.h					\
	digest.c				\
	update.c				\
	save.c					\
	tags.c					\
	description.c				\
	$(NULL)

libpkgdir_pndir_la_CFLAGS =			\
	$(WARNINGFLAGS_C)			\
	$(NULL)

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
