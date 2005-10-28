# $Id$

WWW_PAGES  = page-template.html \
	     devel-body.html download-body.html index-body.html related-body.html \
             repo-body.html support-body.html credits-body.html

WWW_BINS   = logo.png logo90.png

WWW_FILES   = $(WWW_PAGES) $(WWW_BINS) ../poldek.html $(top_srcdir)/NEWS
WWW_ROOTDIR = team.pld.org.pl:public_html/poldek/new/

EXTRA_DIST = $(WWW_PAGES) $(WWWBINS)


site:  $(WWW_PAGES)
	$(MAKE) -C .. manual-web
	$(MAKE) -C ../.. NEWS
	$(MAKE) -C .. NEWS.html
	mkdir -p site || exit 1
	./genhomepage --clean --destdir site
	./genhomepage --destdir site || exit 1
	cp ../NEWS.html ../manual.css style.css logo90.png site
	rm -rf site/manual && cp -a ../manual-web site/manual

credits-body.html: ../NEWS.xml
	$(MAKE) -C .. CREDITS-body.html NEWS.html
	cp ../CREDITS-body.html credits-body.html


clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#
	-rm -rf site

MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 

