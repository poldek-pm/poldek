# $Id$

DISTSRCS = mdk pld rh
SAMPLES  = sample-cdrom sample
CONF=../poldekrc

SOURCES = $(addsuffix .src, $(DISTSRCS))
DESTS   = $(addprefix poldekrc.sample-, $(DISTSRCS))

all: $(DESTS) poldekrc.sample poldekrc.sample-cdrom


poldekrc.sample-%:  %.src $(CONF)
	@echo "# Sample poldek <http://poldek.pld.org.pl> configuration" > $@; \
	cat $< $(CONF) >> $@

poldekrc.sample-cdrom: sample-cdrom.src $(CONF)
	@echo "# Sample poldek <http://poldek.pld.org.pl> configuration" > $@; \
	cat $< $(CONF) >> $@

poldekrc.sample: sample.src $(CONF)
	@echo "# Sample poldek <http://poldek.pld.org.pl> configuration" > $@; \
	cat $< $(CONF) >> $@




clean: 
	-rm -f *~ poldekrc.sample*

# satisfy automake
distdir:

install:

maintainer-clean:
