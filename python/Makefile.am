SWIG = swig -python -shadow
py_ver     = $(shell python -c "import sys; print sys.version[:3]")
py_prefix  = $(shell python -c "import sys; print sys.prefix")
py_incdir  = $(py_prefix)/include/python$(py_ver)
#py_sitedir = $(py_libdir)/site-packages
#$py_dyndir      %{py_libdir}/lib-dynload

EXTRA_DIST = poldek.i
CLEANFILES = wrap_* poldek.py

INCLUDES = @TRURL_INCLUDE@ @TNDB_INCLUDE@ -I$(top_srcdir) -I$(py_incdir) -I.

#pythondir = $(prefix)$(libdir)/python
python_PYTHON = poldek.py

lib_LTLIBRARIES = _poldek.la

_poldek_la_SOURCES = wrap_poldek.c local_stdint.h
_poldek_la_LDFLAGS = -avoid-version -module
_poldek_la_LIBADD = ../libpoldek.la ../vfile/libvfile.la @TRURL_LDFLAG@ 

AM_CFLAGS=
CFLAGS=

poldek.i: local_stdint.h

install-exec-hook:
	$(mkinstalldirs) "$(DESTDIR)$(py_sitedir)"

%.py: wrap_%.c

local_stdint.h: 
	$(CPP) local_stdint.c > local_stdint.h

wrap_%.c: %.i $(top_srcdir)/%.h
	$(SWIG) $(SWIG_PYTHON_OPT) $(INCLUDES) -o $@ $<


MAINTAINERCLEANFILES =	*~ *.py[co]
