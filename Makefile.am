# $Id$

AUTOMAKE_OPTIONS = dist-bzip2
SUBDIRS   = vfile shell po intl

CFLAGS    = @CFLAGS@
INCLUDES  = @TRURL_INCLUDE@
LDFLAGS   = @LDFLAGS@
POD2MAN   = @POD2MAN@
POD2HTML  = pod2html

TRURLIB_VERSION=@TRURLIB_VERSION@

noinst_LIBRARIES = libpoldek.a
libpoldek_a_SOURCES = \
	  log.c log.h			\
	  term.c term.h			\
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
	  pkgdir_tags.c			\
	  pkgdir_load_dir.c             \
	  pkgdir_save.c			\
	  pkgdir_patch.c		\
	  pkgdir_digest.c		\
	  pkgset-load.c pkgset-load.h	\
	  pkgset.c pkgset.h		\
	  pkgset-req.c pkgset-req.h	\
	  pkgset-order.c    		\
	  pkgset-install.c		\
	  dbdep.c dbdep.h		\
	  rpm.c rpm.h 		        \
	  rpmhdr.c rpmhdr.h		\
	  pkgdb.c pkgdb.h		\
	  usrset.c usrset.h		\
	  install.c install.h	    	\
	  conf.c conf.h	    		\
	  h2n.h 			\
	  dbpkg.c dbpkg.h		\
	  dbpkgset.c dbpkgset.h         \
	  split.c split.h		\
	  pkgroup.c pkgroup.h		\
	  rpmdb_it.c rpmdb_it.h         \
	  hold.c			\
	  pkgfetch.c                    \
	  rpminstall.c			\
	  i18n.h

SHELL_MOD = shell/libshell.a

if ENABLE_INTERACTIVE_MODE
SHELL_MOD_ = $(SHELL_MOD)
else
SHELL_MOD_ =
endif

LDADD_              = @INTLLIBS@

bin_PROGRAMS        = poldek rpmvercmp
bin_SCRIPTS         = mkpackagedir vfjuggle
poldek_SOURCES      = main.c
poldek_LDADD        = libpoldek.a vfile/libvfile.a vfile/vftp/libvftp.a \
	              $(SHELL_MOD_) @TRURL_LDFLAG@ $(LDADD_)
poldek_DEPENDENCIES = libpoldek.a vfile/libvfile.a $(SHELL_MOD_)


rpmvercmp_SOURCES   = rpmvercmp.c
rpmvercmp_LDADD     = $(LDADD_)
rpmvercmp_LIBS      = -lrpm
rpmvercmp_LDFLAGS   = 

noinst_PROGRAMS     = test_match
test_match_SOURCES  = test_match.c
test_match_LDADD    = $(poldek_LDADD) 


EXTRA_DIST = poldek.1 poldek.pod \
	     Makefile.extra *sample* \
	     poldek.spec* $(bin_SCRIPTS) \
	     trurlib/

OMIT_DEPENDENCIES = narray.h tfn_types.h nassert nbuf.h nhash.h nlist.h nstr.h \
		    nassert.h trurl.h

man_MANS = poldek.1


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess config.status config.log aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 *.html \
			ABOUT-NLS po/*~ po/*.pot

poldek.1: poldek.pod
	$(POD2MAN) -s 1 -r "poldek" --center "poldek @VERSION@" $< > $@

poldek.html: poldek.pod
	$(POD2HTML) $< > $@

%.html:	%.sgml
	sgml2html -s 0 $<

website: homepage.html screenshoots.html poldek.html

website-whole-up: website dist website-up
	rpm -ts poldek-@VERSION@.tar.gz
	scp -p $$HOME/rpm/SRPMS/poldek-@VERSION@-1.src.rpm poldek-@VERSION@.tar.gz \
	   team.pld.org.pl:public_html/poldek/download
	ssh team.pld.org.pl \
	'(cd public_html/poldek/download && md5sum *.gz *.rpm > md5sums && \
	~/mkindexpage.pl * > index.html && chmod 644 * && ls -l)'

website-up: website 
	scp -p homepage.html team.pld.org.pl:public_html/poldek/index.html
	scp -p poldek.html NEWS team.pld.org.pl:public_html/poldek/

mkpackagedir:

vfjuggle: 

dist-hook: 
	-rm -rf $(distdir)/.deps
	$(MAKE) -C $(distdir)/trurlib distclean

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#*\# dupa*

all-trurlib:
	@echo "Building local trurlib...";                                \
	if [ ! -f ./trurlib/configure ]; then                             \
	     ./trurlib/autogen.sh;                                        \
	fi;                                                               \
	if [ -f ./trurlib/configure -a ! -f ./trurlib/Makefile ]; then    \
		cd ./trurlib && ./configure && cd ..;	                  \
	fi;                                                               \
	$(MAKE) -C trurlib


include Makefile.extra
