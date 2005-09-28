# $Id$

WWW_PAGES  = 
WWW_BINS   = logo.png logo90.png

WWW_FILES   = $(WWW_PAGES) $(WWW_BINS) ../poldek.html $(top_srcdir)/NEWS
WWW_ROOTDIR = team.pld.org.pl:public_html/poldek/new/

EXTRA_DIST = $(WWW_PAGES) $(WWW_BINS)

siteonly: 
	mkdir -p site || exit 1; \
	./genhomepage --clean --destdir site; \
	genhomepage --destdir site; \
	cp style.css logo90.png site;

site: siteonly
	$(MAKE) -C .. manual-xhtml && cp -a ../manual-xhtml site/manual	

#website: index.html screenshoots.html 
#	$(MAKE) -C .. poldek.html

#website-up: website
#	scp -p $(WWW_FILES) $(WWW_ROOTDIR)


clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#
	-rm -rf site

MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 

