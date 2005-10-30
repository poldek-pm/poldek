# $Id$

# generated configs
GENCONFIGS = fetch.conf source.conf poldek.conf

CONFIGS   = $(GENCONFIGS) aliases.conf \
            pld-source.conf rh-source.conf \
	    fedora-source.conf centos-source.conf

EXTRA_DIST = $(CONFIGS) \
	     sample-source-remoteconf.conf \
             sample-cdrom-source.conf \
             poldek-pri-sample.conf

fetch.conf: ../doc/poldek.conf.xml ../doc/*.xsl
	        ../doc/conf-xml2.sh conf ../doc/poldek.conf.xml fetch

source.conf: ../doc/poldek.conf.xml ../doc/*.xsl
	         ../doc/conf-xml2.sh conf ../doc/poldek.conf.xml source

poldek.conf: ../doc/poldek.conf.xml ../doc/*.xsl
	         ../doc/conf-xml2.sh conf ../doc/poldek.conf.xml poldek

install-data-local: $(GENCONFIGS)
	     $(mkdir_p) $(DESTDIR)$(sysconfdir)/$(PACKAGE)
	     $(INSTALL_DATA) $(CONFIGS) $(DESTDIR)$(sysconfdir)/$(PACKAGE)

