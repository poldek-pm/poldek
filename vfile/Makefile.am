# $Id$

VFCURL_SRCS = vfcurl.c
SUBDIRS   = vfff

LIBS = -ltrurl -lz

if ENABLE_VFILE_CURL
VFCURL_SRCS_ = $(VFCURL_SRCS)
endif

LDADD_  = @INTLLIBS@

INCLUDES = @TRURL_INCLUDE@ -I$(top_srcdir)
AM_CFLAGS = @AM_CFLAGS@

lib_LTLIBRARIES     = libvfile.la 
libvfile_la_SOURCES = vfile.c fetch.c vfetch.c vfprogress.c misc.c \
		      p_open.c extcompr.c vfreq.c vfreq.h \
			  vflock.c \
		      vfffmod.c $(VFCURL_SRCS_)

libvfile_la_LIBADD = vfff/libvfff.la

pkgincludedir = $(includedir)/vfile
pkginclude_HEADERS = vfile.h p_open.h

sigint_la = $(top_srcdir)/sigint/libsigint.la

noinst_PROGRAMS = vfget 
#sample_SOURCES  = sample.c
#sample_LDADD    = libvfile.la @TRURL_LDFLAG@ $(LDADD_) $(sigint_la)

vfget_SOURCES  = vfget.c 
vfget_LDADD    = libvfile.la $(sigint_la) -lutil


#test_vopen3_SOURCES  = test_vopen3.c 
#test_vopen3_LDADD    = libvfile.la $(sigint_la) -lutil

EXTRA_DIST     = $(VFCURL_SRCS) \
		 testcookie.c test_progress.c test_p_open.c



dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
