#!/usr/bin/python
# $Id$

import os
import re
import string
from types import *
import poldekmod
from poldekmod import tn_array, poldek_ctx, poldek_ts, pkg, capreq, pkguinf, \
    source, pkgdir, poclidek_ctx, poclidek_rcmd

def lib_init():
    poldekmod.poldeklib_init()

class n_array_proxy:
    def __init__(self, arr, itemClass):
        self._arr = arr
        self._itemClass = itemClass

    def __nonzero__(self):
        if self._arr: return True
        return False

    def __getattr__(self, attr):
        if not self._arr:
            raise AttributeError, 'class has no attribute %s' % attr
        return getattr(self._arr, attr)

    def __len__(self):
        if self._arr: return len(self._arr)
        return 0
        
    def __getitem__(self, i):
        r = self._arr[i]
        if r: r = self._itemClass(r)
        return r

    def __str__(self):
        return '[' + string.join(map(str, map(self._itemClass, self._arr)), ', ') + ']'
    
    def __setitem__(self, i, val):
        raise TypeError, "tn_array is immutable"

def n_array_proxy_func(prefix, func, classnam):
    return eval('lambda self, *args: n_array_proxy(poldekmod.%s%s(self, *args), %s)' %
                (prefix, func, classnam));

def n_array_proxy_method(prefix, func, classnam):
    return eval('lambda self, *args: n_array_proxy(poldekmod.%s%s(self, *args), %s)' %
                (module, prefix, func, classnam));


def _complete_class(aclass, prefix, delprefix = None, nomethods = False,
                    verbose = 0):
    regexp = re.compile('^%s' % prefix)
    regexp_up = re.compile('^%s' % string.upper(prefix))
    if delprefix:
        l = len(delprefix)
    else:
        l = len(prefix)
    for k, elem in poldekmod.__dict__.items():
        #elem = poldekmod.__dict__[k]
        if not nomethods:
            if regexp.match(k) and type(elem) == BuiltinFunctionType:
                name = k[l:]
                if not hasattr(aclass, name):
                    fn = eval('lambda self, *args: poldekmod.%s(self, *args)' % k);
                    setattr(aclass, name, fn)
                    if verbose:
                        print "SET %s %s %s" % (aclass, name, type(fn))
                    #setattr(aclass, name, elem)
                 
                
        if regexp_up.match(k):
            name = k[l:]
            if not hasattr(aclass, name):
                setattr(aclass, name, elem)
                if verbose:
                    print "SET %s %s %s" % (aclass, name, type(elem))

_complete_class(tn_array, 'n_array_')
setattr(tn_array, '__getitem__', tn_array.nth)

_complete_class(capreq, 'capreq_')
setattr(capreq, '__str__', eval('lambda self: poldekmod.capreq_snprintf_s(self)'))
               
_complete_class(poldek_ctx, 'poldek_')
for fn in ['get_avail_packages', 'search_avail_packages']:
    setattr(poldek_ctx, fn, n_array_proxy_func('poldek_', fn, 'pkg'))
    
_complete_class(poldek_ts, 'poldek_ts_')
_complete_class(poldek_ts, 'poldek_op_', delprefix = 'poldek_',
                nomethods = True, verbose = 0)

_complete_class(pkg, 'pkg_', verbose = 0)
setattr(pkg, '__str__', pkg.id)
setattr(pkg, 'provides',
        n_array_proxy_func('pkg.', '_get_provides', 'capreq'))

setattr(pkg, 'requires',
        n_array_proxy_func('pkg.', '_get_requires', 'capreq'))

_complete_class(pkguinf, 'pkguinf_', verbose = 0)

_complete_class(source, 'source_')
_complete_class(pkgdir, 'pkgdir_')


_complete_class(poclidek_rcmd, 'poclidek_rcmd_')
setattr(poclidek_rcmd, 'get_packages',
        n_array_proxy_func('poclidek_rcmd_', 'get_packages', 'pkg'))
