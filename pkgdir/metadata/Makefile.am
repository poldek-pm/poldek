NULL =

AM_CPPFLAGS = @TRURL_INCLUDE@ -I$(top_srcdir) -I$(top_srcdir)/pkgdir \
	@LIBXML2_INCLUDE@
LIBS =
noinst_LTLIBRARIES = libpkgdir_metadata.la
libpkgdir_metadata_la_SOURCES = 		\
	metadata.c				\
	load.c					\
	load.h					\
	$(NULL)

libpkgdir_metadata_la_CFLAGS =			\
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
