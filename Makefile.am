# $Id$

AUTOMAKE_OPTIONS = 
ACLOCAL_AMFLAGS = -I m4

TRURL_DIR = @TRURL_DIR@
TNDB_DIR = @TNDB_DIR@

CLI=cli
SUBDIRS   = po doc $(TRURL_DIR) $(TNDB_DIR) sigint vfile conf pkgdir \
            pm install3 . $(CLI)
DIST_SUBDIRS = $(SUBDIRS) python tests

INCLUDES  = @TRURL_INCLUDE@ @TNDB_INCLUDE@
AM_CFLAGS = @AM_CFLAGS@

POD2MAN   = @POD2MAN@

libVERINFO         = 2:0:0
lib_LTLIBRARIES    = libpoldek.la

libpoldek_la_LDFLAGS = -version-info $(libVERINFO)
libpoldek_la_SOURCES = \
	  compiler.h	\	
	  log.c log.h	   \
	  poldek_term.c poldek_term.h	\
	  minfo.c			    \
	  misc.c misc.h			\
	  pkgmisc.c pkgmisc.h			\
	  depdirs.c depdirs.h   \
	  pkg.c pkgiter.c pkg.h			\
	  pkgcmp.c pkgcmp.h		\
	  pkgu.c pkgu.h        	\
	  pkgfl.c pkgfl.h		\
	  fileindex.c fileindex.h	\
	  capreqidx.c capreqidx.h	\
	  capreq.c capreq.h 		\
	  pkgset-load.c    	        \
	  pkgset.c pkgset.h 		\
	  pkgset-req.c pkgset-req.h	\
	  pkgset-order.c    	\
	  arg_packages.c arg_packages.h		\
	  conf.c conf.h	conf_intern.h conf_sections.c \
	  split.c split.h		\
	  pkgroup.c pkgroup.h	\
	  pkgscore.c		\
	  pkgfetch.c            \
	  pkgmark.c             \
	  i18n.h 		\
	  ask.c                 \
	  install-dist.c        \
	  upgrade-dist.c        \
	  uninstall.c           \
	  lib_init.c            \
	  lib_pkgset.c          \
	  poldek_ts.c poldek_ts.h \
	  poldek_intern.h \
	  pkg_ver_cmp.h 

pkgincludedir = $(includedir)/poldek

# one line to easy get them by external script 
libHEADERS = poldek.h poldek_ts.h pkg.h pkgcmp.h capreq.h pkgu.h pkgmisc.h arg_packages.h poldek_util.h poldek_term.h log.h pm/pm.h pkgdir/pkgdir.h pkgdir/source.h sigint/sigint.h conf.h pkgfl.h

nobase_pkginclude_HEADERS = $(libHEADERS)


libpoldek_la_LIBADD = \
			sigint/libsigint.la  \
			pkgdir/libpkgdir.la  \
			install3/libinstall3.la \
			pm/libpm.la    \
			vfile/libvfile.la

LDADD_              = @INTLLIBS@

pkglib_SCRIPTS      = zlib-in-rpm.sh vfjuggle vfcompr vfsmb poldekuser-setup.sh

EXTRA_DIST = \
	     $(pkglib_SCRIPTS) \
	     Makefile.extra \
	     poldek.spec \
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

NEWS: 
	$(MAKE) -C doc NEWS
	cp doc/NEWS .

conf_sections.c: $(top_srcdir)/doc/poldek.conf.xml $(top_srcdir)/doc/*.xsl
	         	 doc/conf-xml2.sh c doc/poldek.conf.xml > $@

vfjuggle: 

dist-hook: 
	-rm -rf $(distdir)/.deps
	-rm -rf `find $(distdir)/doc/ -name CVS`

#install-exec-local:
#	$(install_sh) -m 0755 -d $(DESTDIR)$(libexecdir)/$(PACKAGE)
#	$(install_sh) -m 0755 $(vfscripts) $(DESTDIR)$(libexecdir)/$(PACKAGE)

# cleanning up *.la files; who is stupid me or libtool? 
install-exec-hook:
	for p in poldek vfile tndb trurl; do                        \
		laf="$(DESTDIR)$(libdir)/lib$$p.la";                    \
		test -f $$laf && perl -pi -e 's|\s\-L[/\w\-\.]+||g;' $$laf; \
	done; \
	$(LN_S) $(PACKAGE) $(DESTDIR)$(bindir)/i$(PACKAGE) 

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#*\# dupa* conf/*~

poldek.pc: 
        @ ( echo 'prefix=$(prefix)'; \
            echo 'exec_prefix=$${exec_prefix}'; \
            echo 'libdir=$${exec_prefix}/lib'; \
            echo 'includedir=$${includedir}'; \
            echo ''; \
            echo 'Name: poldek'; \
            echo 'Description: poldek libraries'; \
            echo 'Version: '$(VERSION); \
            echo 'Requires: '; \
            echo 'Libs: -L$${libdir} -lpoldek -lvfile -ltrurl -ltndb'; \
            echo 'Cflags: -I$${includedir}' ) > poldek.pc


include Makefile.extra
