# $Id$

SUBDIRS   = vfile shell

CFLAGS    = @CFLAGS@
INCLUDES  = @TRURL_INCLUDE@
LDFLAGS   = @LDFLAGS@
POD2MAN   = @POD2MAN@
POD2HTML  = pod2html

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
	  split.c split.h		\
	  pkgroup.c pkgroup.h		\
	  rpmdb_it.c rpmdb_it.h         \
	  hold.c

SHELL_MOD = shell/libshell.a

if ENABLE_INTERACTIVE_MODE
SHELL_MOD_ = $(SHELL_MOD)
endif

bin_PROGRAMS        = poldek rpmvercmp
bin_SCRIPTS         = mkpackagedir vfjuggle
poldek_SOURCES      = main.c
poldek_LDADD        = vfile/libvfile.a libpoldek.a $(SHELL_MOD_) \
	              @TRURL_LDFLAG@
poldek_DEPENDENCIES = vfile/libvfile.a libpoldek.a $(SHELL_MOD_)


rpmvercmp_SOURCES   = rpmvercmp.c
rpmvercmp_LDADD     = 
rpmvercmp_LIBS      = -lrpm
rpmvercmp_LDFLAGS   = 

noinst_PROGRAMS     = test_match
test_match_SOURCES  = test_match.c
test_match_LDADD        = vfile/libvfile.a libpoldek.a @TRURL_LDFLAG@


EXTRA_DIST = poldek.1 poldek.pod \
	     Makefile.extra *sample* \
	     poldek.spec $(bin_SCRIPTS)

OMIT_DEPENDENCIES = narray.h tfn_types.h nassert nbuf.h nhash.h nlist.h nstr.h \
		    nassert.h trurl.h

man_MANS = poldek.1


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 *.html

poldek.1: poldek.pod
	$(POD2MAN) -s 1 -r "poldek" --center "poldek @VERSION@" $< > $@

poldek.html: poldek.pod
	$(POD2HTML) $< > $@

homepage.html: homepage.sgml
	sgml2html -s 0 $<

website: homepage.html poldek.html

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
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#*\# dupa*



include Makefile.extra
