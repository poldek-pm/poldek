# $Id$

SUBDIRS   = vfff
DIST_SUBDIRS = $(SUBDIRS) tests

#LIBS = -ltrurl -lz @LIBS@ @INTLLIBS@

INCLUDES = @TRURL_INCLUDE@ -I$(top_srcdir)
AM_CFLAGS = @AM_CFLAGS@

LIBVERSION = 0:0:0
lib_LTLIBRARIES     = libvfile.la 
libvfile_la_LDFLAGS = -version-info $(LIBVERSION) -export-symbols $(srcdir)/libvfile.sym

libvfile_la_SOURCES = vfile.c fetch.c vfetch.c vfprogress.c misc.c \
		      p_open.c extcompr.c vfreq.c vfreq.h \
		      vflock.c vfffmod.c ne_uri.c \
		      vopen3.c vopen3.h vfile_intern.h

libvfile_la_LIBADD = vfff/libvfff.la

pkgincludedir = $(includedir)/vfile
pkginclude_HEADERS = vfile.h p_open.h vopen3.h

noinst_PROGRAMS = vfget test_vopen3 
TEST_LDADDS    = libvfile.la -lutil

vfget_SOURCES  = vfget.c 
vfget_LDADD    = $(TEST_LDADDS)

test_vopen3_SOURCES  = test_vopen3.c 
test_vopen3_LDADD    = $(TEST_LDADDS)

EXTRA_DIST     = libvfile.sym testcookie.c test_progress.c sample.c ttest.c

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
