# $Id$

PROJ_DIR=$(shell pwd)
VERSION=$(shell cat VERSION)
INSTALL_ROOT = /usr/local

ifdef DESTDIR
	INSTALL_ROOT = $(DESTDIR)
endif

DEFINES  = -DVFILE_RPMIO_SUPPORT
INCLUDES = -Itrurlib
CPPFLAGS = $(INCLUDES) $(DEFINES)
override CFLAGS += -g -Wall -W $(CPPFLAGS)
LIBS     = -lrpm -lpopt -lz -lbz2 -ldb

ifeq ($(MAKECMDGOALS),poldek)
LIBS     += -Wl,-Bstatic -ltrurl -Wl,-Bdynamic 
else
LIBS     += -ltrurl
endif

LDFLAGS  = -Ltrurlib
CC 	 = gcc
SHELL 	 = /bin/sh
RANLIB   = ranlib
AR	 = ar
STRIP	 = strip

LIBOBJS = log.o             \
	  minfo.o           \
	  misc.o	    \
	  rpmadds.o         \
	  depdirs.o         \
	  pkg.o             \
	  pkgfl.o           \
	  fileindex.o       \
	  capreqidx.o	    \
	  capreq.o          \
	  pkgset.o          \
	  pkgset-req.o      \
	  pkgset-order.o    \
	  pkgset-load.o     \
	  pkgset-install.o  \
	  pkgset-rpmidx.o   \
	  pkgset-txtidx.o   \
	  rpm.o             \
	  rpmhdr.o          \
	  pkgdb.o           \
	  usrset.o          \
	  vfile.o  	    \
	  install.o	    \
	  fetch.o   	    \
	  conf.o	    

OBJS = main.o test_rpm.o 

LIBNAME     =  poldek
STATIC_LIB  =  lib$(LIBNAME).a
TEST_PROGS  =  test_rpm


TARGETS = poldek poldek.1 poldek.static poldek.semistatic

ifdef gprof
	CFLAGS  += -pg
	LDFLAGS += -Wl,-Bstatic -lc_p -Wl,-Bdynamic
endif

ifdef ccmalloc	
	LIBS += -lccmalloc -ldl 
endif 

ifdef efence
	LIBS += -lefence
endif

backupdir ?= "/z"

%.o:	%.cc
	$(CC) -c $(CFLAGS) -o $@ $<

%.o:	%.c
	$(CC) -c $(CFLAGS)  -o $@ $<

all: 	poldek poldek.1

$(STATIC_LIB): $(LIBOBJS)
	$(AR) cr $@ $?
	$(RANLIB) $@


poldek: $(STATIC_LIB) main.o
	$(CC)  $(LDFLAGS) -o $@ main.o $(STATIC_LIB) $(LIBS)

poldek.semistatic: poldek
	$(CC) $(LDFLAGS) -o $@ main.o $(STATIC_LIB) -Wl,-Bstatic $(LIBS) -Wl,-Bdynamic

poldek.static: poldek
	$(CC) -static $(LDFLAGS) -o $@ main.o $(STATIC_LIB) $(LIBS)

poldek.1: poldek.pod
	pod2man -s 1 -r "poldek" --center "poldek $(VERSION)" $< > $@

test_%:  test_%.o $(STATIC_LIB)
	$(CC) $(CFLAGS) $< -o $@ $(LFLAGS) $(STATIC_LIB) $(LDFLAGS) $(LIBS)

install: 
	install -d $(INSTALL_ROOT)/bin/
	install -m 755 poldek $(INSTALL_ROOT)/bin/
	install -d $(INSTALL_ROOT)/share/man/man1
	install -m 644 poldek.1 $(INSTALL_ROOT)/share/man/man1/


install-statics: 
	install -d $(INSTALL_ROOT)/bin/
	install -m 755 poldek.*static* $(INSTALL_ROOT)/bin/

dep:
	gcc -MM $(CPPFLAGS) *.c >.depend

etags: 
	etags *.c *.h

clean:
	-rm -f core *.o *.bak *~ *% #*
	-rm -f $(TARGETS) $(TEST_PROGS) $(STATIC_LIB) TAGS gmon.out

distclean: clean
	-rm -f .depend *.log Packages*

backup:
	@cd $(PROJ_DIR); \
	ARCHDIR=`basename  $(PROJ_DIR)`-ARCH; \
	ARCHNAME=`basename  $(PROJ_DIR)`-`date +%Y.%m.%d-%H.%M`; \
	cd ..; \
	mkdir $$ARCHDIR || true; \
	cd $$ARCHDIR && mkdir $$ARCHNAME || exit 1; \
	cd .. ;\
	cp -a $(PROJ_DIR) $$ARCHDIR/$$ARCHNAME ;\
	cd $$ARCHDIR ;\
	tar cvpzf $$ARCHNAME.tgz $$ARCHNAME && rm -rf $$ARCHNAME;   \
	md5sum $$ARCHNAME.tgz > $$ARCHNAME.md5;                     \
	if [ $(cparch)x = "1x" ]; then                              \
	        mkdir $(backupdir)/copy || true;                    \
		cp -v $$HOME/$$ARCHDIR/$$ARCHNAME.tgz $(backupdir); \
		cp -v $$HOME/$$ARCHDIR/$$ARCHNAME.tgz $(backupdir)/copy; \
		cd $(backupdir) || exit 1;                         \
		md5sum --check $$HOME/$$ARCHDIR/$$ARCHNAME.md5;    \
		cd copy || exit 1;                                 \
		md5sum --check $$HOME/$$ARCHDIR/$$ARCHNAME.md5;    \
	fi

arch : distclean  backup 

misarch: distclean 
	$(MAKE) -C . backup cparch=1 backupdir=/z/

#
# Make dist archives of $(PROJ_DIR). Archives are stored in TMPDIR (see below)
# as $(PROJ_DIR)-`cat VERSION`.tar.[gz, bz2]
# 
dist:   distclean
	@cd $(PROJ_DIR)                       ;\
	TMPDIR=/tmp                           ;\
	REV=`cat VERSION`                     ;\
	CURDIR=`basename  $(PROJ_DIR)`        ;\
	DISTDIR=$$TMPDIR/$$CURDIR-$$REV/      ;\
	rm -rf $$DISTDIR                      ;\
	mkdir $$DISTDIR                       ;\
	cp -a * $$DISTDIR                     ;\
	for f in `cat .cvsignore`; do          \
             rm -rf $$DISTDIR/$$f             ;\
        done                                  ;\
	rm -rf $$DISTDIR/CVS                  ;\
	cd $$TMPDIR                           ;\
	arch_name=`basename $$DISTDIR`        ;\
	tar cvpf $$arch_name.tar $$arch_name  ;\
	gzip -9f $$arch_name.tar && rm -rf $$DISTDIR

rpm:    dist
	@rpm -ta /tmp/poldek-$(VERSION).tar.gz 

ifeq (.depend,$(wildcard .depend))
include .depend
endif
