#!/usr/bin/python

import os
import re
import string
from types import *
import poldekmod
from poldekmod import *

class n_array_proxy:
    def __init__(self, arr, itemClass):
        self.__arr = arr
        self.__itemClass = itemClass

    def __getattr__(self, attr):
        return getattr(self.__arr, attr)

    def __len__(self):
        return len(self.__arr)
        
    def __getitem__(self, i):
        r = self.__arr[i]
        if r: r = self.__itemClass(r)
        return r

def _m_get_avail_packages(self):
    l = poldek_get_avail_packages(self)
    if l:
        l = n_array_proxy(l, pkg)
    return l

def n_array_proxy_func(function):
    return eval("""def _m_%s(self, *args):
       l = %s(self, *args)
       if l:
           return n_array_proxy(l, pkg)
       return l
                """ % (function, function))
                

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
                    #setattr(aclass, name, elem)
                 
                
        if regexp_up.match(k):
            name = k[l:]
            if not hasattr(aclass, name):
                setattr(aclass, name, elem)
                if verbose:
                    print "SET %s %s" % (name, type(elem))

_complete_class(tn_array, 'n_array_')
setattr(tn_array, '__getitem__', tn_array.nth)

                
_complete_class(poldek_ctx, 'poldek_')
#setattr(poldek_ctx, 'get_avail_packages', _m_get_avail_packages)
setattr(poldek_ctx, 'get_avail_packages',
        n_array_proxy_func('poldek_get_avail_packages'))
_complete_class(poldek_ts, 'poldek_ts_')
_complete_class(poldek_ts, 'poldek_op_', delprefix = 'poldek_',
                nomethods = True, verbose = 0)

_complete_class(pkg, 'pkg_')
setattr(pkg, '__str__', pkg.id)
_complete_class(source, 'source_')
_complete_class(pkgdir, 'pkgdir')

















