# $Id$

VFCURL_SRCS = vfcurl.c
SUBDIRS   = vftp

if ENABLE_VFILE_CURL
VFCURL_SRCS_ = $(VFCURL_SRCS)
endif


INCLUDES = @TRURL_INCLUDE@
lib_LIBRARIES      = libvfile.a 
libvfile_a_SOURCES = vfile.c vfprogress.c fetch.c p_open.c \
		     vfvftp.c $(VFCURL_SRCS_)

pkgincludedir = $(includedir)/vfile
pkginclude_HEADERS = vfile.h p_open.h

noinst_PROGRAMS = sample ttest
sample_SOURCES  = sample.c
sample_LDADD    = libvfile.a vftp/libvftp.a @TRURL_LDFLAG@ -lutil $(LDADD_)

ttest_SOURCES  = ttest.c 
ttest_LDADD    = libvfile.a vftp/libvftp.a @TRURL_LDFLAG@ -lutil $(LDADD_)
EXTRA_DIST     = $(VFCURL_SRCS) 

OMIT_DEPENDENCIES = narray.h tfn_types.h nassert nbuf.h nhash.h nlist.h nstr.h \
		    nassert.h trurl.h

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
