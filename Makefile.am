# $Id$

SUBDIRS   = vfile shell

CFLAGS    = @CFLAGS@
INCLUDES  = @TRURL_INCLUDE@
LDFLAGS   = @LDFLAGS@
POD2MAN   = @POD2MAN@

noinst_LIBRARIES = libpoldek.a
libpoldek_a_SOURCES = \
	  log.c log.h			\
	  minfo.c			\
	  misc.c misc.h			\
	  rpmadds.c rpmadds.h		\
	  depdirs.c depdirs.h   	\
	  pkg.c pkg.h			\
	  pkgu.c pkgu.h         	\
	  pkgfl.c pkgfl.h		\
	  fileindex.c fileindex.h	\
	  capreqidx.c capreqidx.h	\
	  capreq.c capreq.h		\
	  pkgdir.c pkgdir.h		\
	  pkgset-load.c pkgset-load.h	\
	  pkgset.c pkgset.h		\
	  pkgset-req.c pkgset-req.h	\
	  pkgset-order.c    		\
	  pkgset-install.c		\
	  rpm.c rpm.h 		        \
	  rpmhdr.c rpmhdr.h		\
	  pkgdb.c pkgdb.h		\
	  usrset.c usrset.h		\
	  install.c install.h	    	\
	  conf.c conf.h	    		\
	  h2n.h 			\
	  dbpkg.c dbpkg.h		\
	  rpmdb_it.c rpmdb_it.h

SHELL_MOD = shell/shell.a

if ENABLE_INTERACTIVE_MODE
SHELL_MOD_ = $(SHELL_MOD)
endif

bin_PROGRAMS        = poldek rpmvercmp
bin_SCRIPTS         = mkrpmdir
poldek_SOURCES      = main.c
poldek_LDADD        = vfile/libvfile.a libpoldek.a $(SHELL_MOD_) \
	              @TRURL_LDFLAG@ -lutil
poldek_DEPENDENCIES = vfile/libvfile.a libpoldek.a $(SHELL_MOD_)


rpmvercmp_SOURCES   = rpmvercmp.c
rpmvercmp_LDADD     = 
rpmvercmp_LIBS      = -lrpm
rpmvercmp_LDFLAGS   = 

EXTRA_DIST = poldek.1 poldek.pod \
	     Makefile.extra *sample* \
	     poldek.spec

man_MANS = poldek.1


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* *.1

poldek.1: poldek.pod
	$(POD2MAN) -s 1 -r "poldek" --center "poldek @VERSION@" $< > $@

mkrpmdir:

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#*\# dupa*



include Makefile.extra
