# $Id$

CURL_SRCS = vfcurl.c vfcurl.h

if ENABLE_VFILE_CURL
CURL_SRCS_ = $(CURL_SRCS)
endif


INCLUDES = @TRURL_INCLUDE@
lib_LIBRARIES      = libvfile.a 
libvfile_a_SOURCES = vfile.c fetch.c p_open.c $(CURL_SRCS_)

pkgincludedir = $(includedir)/vfile
pkginclude_HEADERS = vfile.h p_open.h

noinst_PROGRAMS = sample ttest
sample_SOURCES  = sample.c
sample_LDADD    = libvfile.a @TRURL_LDFLAG@ -lutil

ttest_SOURCES  = ttest.c
ttest_LDADD    = libvfile.a @TRURL_LDFLAG@ -lutil
EXTRA_DIST      = $(CURL_SRCS) 

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
