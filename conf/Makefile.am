# $Id$

CONFIGS   = fetch.conf poldek.alias \
			pld-source.conf rh-source.conf fedora-source.conf

EXTRA_DIST = $(CONFIGS) poldek.conf.in \
             mdk-source.conf  poldek.conf  sample-cdrom-source.conf \
             poldek.alias poldek-pri-sample.conf 

install-data-local:
	     $(mkdir_p) $(DESTDIR)$(sysconfdir)/$(PACKAGE)
	     $(INSTALL_DATA) poldek.conf $(CONFIGS) $(DESTDIR)$(sysconfdir)/$(PACKAGE)

