# $Id$

CFLAGS    = @CFLAGS@
INCLUDES  = -I$(top_srcdir)/trurlib

lib_LIBRARIES      = libvfile.a
libvfile_a_SOURCES = vfile.c fetch.c p_open.c 

pkgincludedir = $(includedir)/vfile
pkginclude_HEADERS = vfile.h p_open.h

#libvfile_a_LDADD  = -Ltrurlib -L$(top_srcdir)/trurlib -lvfile

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#
