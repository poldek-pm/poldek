# $Id$

AUTOMAKE_OPTIONS = 
ACLOCAL_AMFLAGS = -I m4

if WITH_INCLUDED_TRURLIB
TRURL_DIR=trurlib
else
TRURL_DIR=
endif

SUBDIRS   = po $(TRURL_DIR) vfile shell conf
DIST_SUBDIRS = po trurlib vfile shell conf 

INCLUDES  = @TRURL_INCLUDE@
POD2MAN   = @POD2MAN@
POD2HTML  = pod2html

TRURLIB_VERSION=@TRURLIB_VERSION@

noinst_LIBRARIES = libpoldek.a
libpoldek_a_SOURCES = \
	  log.c log.h			\
	  poldek_term.c poldek_term.h	\
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
	  source.c source.h             \
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
	  rpm.c rpm.h rpmdb_depdirs.c rpm_signature.c  \
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
	  pkgscore.c			\
	  pkgfetch.c                    \
	  rpminstall.c			\
	  i18n.h 			\
	  ask.c                         \
	  uninstall.c                   \
	  sigint.c sigint.h

SHELL_MOD = shell/libshell.a

poldek_DEPENDENCIES_ = libpoldek.a            \
	               vfile/libvfile.a       \
		       vfile/vftp/libvftp.a   \
		       vfile/vhttp/libvhttp.a

if ENABLE_INTERACTIVE_MODE
SHELL_MOD_ = $(SHELL_MOD)
poldek_DEPENDENCIES = $(poldek_DEPENDENCIES_) $(SHELL_MOD)
else
SHELL_MOD_ =
poldek_DEPENDENCIES = $(poldek_DEPENDENCIES_) $(SHELL_MOD_)
endif

LDADD_              = @INTLLIBS@

bin_PROGRAMS        = poldek rpmvercmp
dist_bin_SCRIPTS    = mkpackagedir vfjuggle vfuncompr
poldek_SOURCES      = main.c
poldek_LDADD        = libpoldek.a vfile/libvfile.a \
		      vfile/vftp/libvftp.a vfile/vhttp/libvhttp.a \
	              $(SHELL_MOD_) $(LDADD_)

rpmvercmp_SOURCES   = rpmvercmp.c
rpmvercmp_LDADD     = $(LDADD_)
rpmvercmp_LIBS      = -lrpm
rpmvercmp_LDFLAGS   = 

noinst_PROGRAMS     = test_match
test_match_SOURCES  = test_match.c
test_match_LDADD    = $(poldek_LDADD) 


EXTRA_DIST = poldek.1 poldek.pod \
	     Makefile.extra poldekrc logo.png \
	     poldek.spec poldek-suse.spec \
	     package-set.sample  pkgsplit.conf.sample  vfjugglerc.sample \
	     m4/codeset.m4 m4/gettext.m4 m4/glibc21.m4 m4/iconv.m4 \
	     m4/intdiv0.m4 m4/inttypes_h.m4 m4/inttypes.m4 m4/inttypes-pri.m4 \
	     m4/isc-posix.m4 m4/lcmessage.m4 m4/lib-ld.m4 m4/lib-link.m4 \
	     m4/lib-prefix.m4 m4/progtest.m4 m4/stdint_h.m4 m4/uintmax_t.m4 \
	     m4/ulonglong.m4
	     

man_MANS = poldek.1


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess config.status config.log aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 *.html \
			ABOUT-NLS po/*~ po/*.pot conf/*~ m4/*~

poldek.1: poldek.pod
	$(POD2MAN) -s 1 -r "poldek" --center "poldek @VERSION@" $< > $@

poldek.html: poldek.pod
	$(POD2HTML) $< > $@

homepage.html:	homepage.sgml
	sgml2html -s 0 $< && mv $@ $@- && ./mkhtmltoc.pl $@- > $@ && rm -f $@-

%.html:	%.sgml
	sgml2html -s 0 $<

website: homepage.html screenshoots.html poldek.html

website-whole-up: website dist website-up
	rpm -ts poldek-@VERSION@.tar.gz
	scp -p $$HOME/rpm/SRPMS/poldek-@VERSION@-1.src.rpm poldek-@VERSION@.tar.* \
	   team.pld.org.pl:public_html/poldek/download
	ssh team.pld.org.pl \
	'(cd public_html/poldek/download && md5sum *.gz *.rpm > md5sums && \
	~/mkindexpage.pl * > index.html && chmod 644 * && ls -l)'

homepage-up: website
	scp -p homepage.html team.pld.org.pl:public_html/poldek/index.html

website-up: website homepage-up
	scp -p screenshoots.html poldek.html NEWS team.pld.org.pl:public_html/poldek/

mkpackagedir:

vfjuggle: 

dist-hook: 
	-rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#*\# dupa* conf/*~

include Makefile.extra
