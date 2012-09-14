# $Id$

WWW_PAGES  = page-template.html \
	     devel-body.html download-body.html index-body.html related-body.html \
             repo-body.html support-body.html credits-body.html

WWW_BINS   = logo.png logo90.png

WWW_FILES   = $(WWW_PAGES) $(WWW_BINS) ../poldek.html $(top_srcdir)/NEWS
WWW_ROOTDIR = team.pld.org.pl:public_html/poldek/new/

EXTRA_DIST = $(WWW_PAGES) $(WWWBINS)

site-only:
	./genhomepage --clean --destdir site
	./genhomepage --destdir site || exit 1

site:  $(WWW_PAGES) ../manual.xml ../NEWS.xml ../NEWSto
	$(MAKE) -C .. manual-web
	$(MAKE) -C ../.. NEWS
	$(MAKE) -C .. NEWS.html
	mkdir -p site || exit 1
	./genhomepage --clean --destdir site
	./genhomepage --destdir site || exit 1
	cp ../NEWS.html ../manual.css style.css logo90.png site
	rm -rf site/Manual && cp -a ../manual-web site/Manual

credits-body.html: ../NEWS.xml ../NEWSto
	$(MAKE) -C .. CREDITS-body.html NEWS.html
	cp ../CREDITS-body.html credits-body.html


clean-local:
	-rm -rf site

CLEANFILES = credits-body.html

MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 

