# $Id$

VFCURL_SRCS = vfcurl.c
SUBDIRS   = vftp vhttp

if ENABLE_VFILE_CURL
VFCURL_SRCS_ = $(VFCURL_SRCS)
endif

LDADD_              = @INTLLIBS@

INCLUDES = @TRURL_INCLUDE@ -I$(top_srcdir)
AM_CFLAGS = @AM_CFLAGS@

lib_LIBRARIES      = libvfile.a 
libvfile_a_SOURCES = vfile.c vfprogress.c fetch.c misc.c \
		     p_open.c uncompr.c \
		     $(VFCURL_SRCS_) vfvftp.c vfvhttp.c 

pkgincludedir = $(includedir)/vfile
pkginclude_HEADERS = vfile.h p_open.h

vlibs = vftp/libvftp.a vhttp/libvhttp.a

noinst_PROGRAMS = sample ttest
sample_SOURCES  = sample.c
sample_LDADD    = libvfile.a $(vlibs) @TRURL_LDFLAG@ $(LDADD_) $(top_srcdir)/sigint/libsigint.a

ttest_SOURCES  = ttest.c 
ttest_LDADD    = libvfile.a $(vlibs) @TRURL_LDFLAG@ $(LDADD_) $(top_srcdir)/sigint/libsigint.a
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
