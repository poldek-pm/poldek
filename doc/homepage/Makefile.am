# $Id$

WWW_PAGES  = screenshoots.html index.html
WWW_BINS   = logo.png

WWW_FILES   = $(WWW_PAGES) $(WWW_BINS) ../poldek.html $(top_srcdir)/NEWS
WWW_ROOTDIR = team.pld.org.pl:public_html/poldek

EXTRA_DIST = index.sgml.in screenshoots.sgml mkhtmltoc.pl \
			 $(WWW_PAGES) $(WWW_BINS)

index.html:	index.sgml
	sgml2html -s 0 $< && mv $@ $@- && ./mkhtmltoc.pl $@- > $@ && rm -f $@-

%.html:	%.sgml
	sgml2html -s 0 $<

index-up: index.html
	scp -p index.html $(WWW_ROOTDIR)

website: index.html screenshoots.html 
	$(MAKE) -C .. poldek.html

website-up: website
	scp -p $(WWW_FILES) $(WWW_ROOTDIR)


clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#

MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 

