# $Id$

CFLAGS    = @CFLAGS@
INCLUDES  = -I$(top_srcdir)/trurlib

lib_LIBRARIES      = libvfile.a
libvfile_a_SOURCES = vfile.c fetch.c p_open.c 

pkgincludedir = $(includedir)/vfile
pkginclude_HEADERS = vfile.h p_open.h

noinst_PROGRAMS = sample
sample_SOURCES  = sample.c
sample_LDADD    = libvfile.a

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#
