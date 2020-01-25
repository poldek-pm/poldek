# vfff - VFile File Fetch

AM_CPPFLAGS = -I$(top_srcdir) @TRURL_INCLUDE@
AM_CFLAGS = @AM_CFLAGS@

LIBS =
noinst_LTLIBRARIES  = libvfff.la
libvfff_la_SOURCES  =   vfff.c vfff.h 	\
			io.c 		\
			http.c base64.c \
			ftp.c

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1
