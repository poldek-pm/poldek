# $Id$

AUTOMAKE_OPTIONS = 
ACLOCAL_AMFLAGS = -I m4

TRURL_DIR = @TRURL_DIR@
TNDB_DIR = @TNDB_DIR@

CLI=cli
SUBDIRS   = po $(TRURL_DIR) $(TNDB_DIR) sigint vfile conf pkgdir \
            pm . $(CLI)
DIST_SUBDIRS = $(SUBDIRS)

INCLUDES  = @TRURL_INCLUDE@ @TNDB_INCLUDE@
AM_CFLAGS = @AM_CFLAGS@

POD2MAN   = @POD2MAN@

lib_LTLIBRARIES = libpoldek.la
libpoldek_la_SOURCES =  \
	  log.c log.h	   \
	  poldek_term.c poldek_term.h	\
	  minfo.c			    \
	  misc.c misc.h			\
	  pkgmisc.c pkgmisc.h			\
	  depdirs.c depdirs.h   \
	  pkg.c 			\
	  pkgu.c pkgu.h        	\
	  pkgfl.c pkgfl.h		\
	  fileindex.c fileindex.h	\
	  capreqidx.c capreqidx.h	\
	  capreq.c capreq.h 		\
	  pkgset-load.c    	        \
	  pkgset.c		\
	  pkgset-req.c pkgset-req.h	\
	  pkgset-order.c    	\
	  pkgset-install.c		\
	  dbdep.c dbdep.h		\
	  arg_packages.c arg_packages.h		\
	  conf.c 	    	\
	  dbpkgset.c dbpkgset.h \
	  split.c split.h		\
	  pkgroup.c pkgroup.h	\
	  pkgscore.c		\
	  pkgfetch.c            \
	  pkgmark.c             \
	  i18n.h 		\
	  ask.c                 \
	  install-dist.c        \
	  uninstall.c           \
	  lib_init.c            \
	  lib_pkgset.c          \
	  poldek_ts.c poldek_ts.h	  

pkgincludedir = $(includedir)/poldek
pkginclude_HEADERS = poldek.h conf.h pkg.h pkgset.h \
		     pkgdir/pkgdir.h pkgdir/source.h


libpoldek_la_LIBADD = \
			sigint/libsigint.la  \
			pkgdir/libpkgdir.la  \
			pm/libpm.la    \
			vfile/libvfile.la

SHELL_MOD = shell/libshell.la


poldek_DEPENDENCIES_ = libpoldek.la vfile/libvfile.la    

if ENABLE_INTERACTIVE_MODE
SHELL_MOD_ = $(SHELL_MOD)
#poldek_DEPENDENCIES = $(poldek_DEPENDENCIES_) $(SHELL_MOD)
else
SHELL_MOD_ =
#poldek_DEPENDENCIES = $(poldek_DEPENDENCIES_) $(SHELL_MOD_)
endif

LDADD_              = @INTLLIBS@

bin_PROGRAMS        =
#poldek_SOURCES      = main.c
#poldek_LDADD        = $(poldek_DEPENDENCIES) $(SHELL_MOD_) $(LDADD_)

noinst_PROGRAMS     = test_match
test_match_SOURCES  = test_match.c
test_match_LDADD    = libpoldek.la 

vfscripts           = vfjuggle vfcompr vfsmb

EXTRA_DIST = \
	     $(vfscripts) \
	     Makefile.extra \
	     poldek.spec poldek-suse.spec \
	     package-set.sample vfjugglerc.sample \
	     m4/codeset.m4 m4/gettext.m4 m4/glibc21.m4 m4/iconv.m4 \
	     m4/intdiv0.m4 m4/inttypes_h.m4 m4/inttypes.m4 m4/inttypes-pri.m4 \
	     m4/isc-posix.m4 m4/lcmessage.m4 m4/lib-ld.m4 m4/lib-link.m4 \
	     m4/lib-prefix.m4 m4/progtest.m4 m4/stdint_h.m4 m4/uintmax_t.m4 \
	     m4/ulonglong.m4

MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess config.status config.log aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 *.html \
			ABOUT-NLS po/*~ po/*.pot conf/*~ m4/*~


website-whole-up: dist 
	$(MAKE) -C doc/homepage website-up
	rpm -ts poldek-@VERSION@.tar.gz
	scp -p $$HOME/rpm/SRPMS/poldek-@VERSION@-1.src.rpm poldek-@VERSION@.tar.* \
	   team.pld.org.pl:public_html/poldek/download
	ssh team.pld.org.pl \
	'(cd public_html/poldek/download && md5sum *.gz *.rpm > md5sums && \
	~/mkindexpage.pl * > index.html && chmod 644 * && ls -l)'

vfjuggle: 

dist-hook: 
	-rm -rf $(distdir)/.deps

install-exec-local:
	$(install_sh) -m 0755 -d $(DESTDIR)$(libexecdir)/$(PACKAGE)
	$(install_sh) -m 0755 $(vfscripts) $(DESTDIR)$(libexecdir)/$(PACKAGE)

# libtool hack
install-exec-hook:
	for p in poldek vfile tndb trurl; do                        \
		laf="$(DESTDIR)$(libdir)/lib$$p.la";                    \
		test -f $$laf && perl -pi -e 's|\s\-L[/\w]+||g;' $$laf; \
	done

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#*\# dupa* conf/*~

include Makefile.extra
