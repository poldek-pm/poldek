# vfff - VFile File Fetch

NULL =

AM_CPPFLAGS = @TRURL_INCLUDE@ -I$(top_srcdir)
AM_CFLAGS = @AM_CFLAGS@

LIBS = 
noinst_LTLIBRARIES  = libvfff.la 
libvfff_la_SOURCES  = \
						vfff.c vfff.h \
						http.c base64.c \
						ftp.c

libvfff_la_CFLAGS =			\
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
