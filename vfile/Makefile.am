# $Id$

VFCURL_SRCS = vfcurl.c
SUBDIRS   = vftp vhttp

LIBS = -ltrurl -lz

if ENABLE_VFILE_CURL
VFCURL_SRCS_ = $(VFCURL_SRCS)
endif

LDADD_  = @INTLLIBS@

INCLUDES = @TRURL_INCLUDE@ -I$(top_srcdir)
AM_CFLAGS = @AM_CFLAGS@

lib_LTLIBRARIES     = libvfile.la 
libvfile_la_SOURCES = vfile.c vfprogress.c fetch.c misc.c \
		      p_open.c uncompr.c vfreq.c vfreq.h \
		      $(VFCURL_SRCS_) vfvftp.c vfvhttp.c 

libvfile_la_LIBADD = vftp/libvftp.la vhttp/libvhttp.la 

pkgincludedir = $(includedir)/vfile
pkginclude_HEADERS = vfile.h p_open.h

sigint_la = $(top_srcdir)/sigint/libsigint.la

#noinst_PROGRAMS = sample ttest
#sample_SOURCES  = sample.c
#sample_LDADD    = libvfile.la @TRURL_LDFLAG@ $(LDADD_) $(sigint_la)

#ttest_SOURCES  = ttest.c 
#ttest_LDADD    = libvfile.la @TRURL_LDFLAG@ $(LDADD_) $(sigint_la)
EXTRA_DIST     = $(VFCURL_SRCS) \
		 testcookie.c test_progress.c

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
