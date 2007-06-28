#!/usr/bin/python
# $Id$

import os
import re
import string
from types import *
import poldekmod
from poldekmod import tn_array, poldek_ctx, poldek_ts, pkg, capreq, pkguinf, \
    pkgflist_it, source, pkgdir, poclidek_ctx, poclidek_rcmd

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

    def length(self):
        if self._arr: return len(self._arr)
        return 0
        
    def __getitem__(self, i):
        r = self._arr[i]
        if r: r = self._itemClass(r)
        return r

    def __str__(self):
        return '[' + self.join(', ') + ']'

    def __setitem__(self, i, val):
        raise TypeError, "tn_array is immutable"

    def join(self, sep = ', '):
        if self._arr is None: return ''
        return string.join(map(str, map(self._itemClass, self._arr)), sep)

def n_array_proxy_func(prefix, func, classnam):
    return eval('lambda self, *args: n_array_proxy(poldekmod.%s%s(self, *args), %s)' %
                (prefix, func, classnam));

def n_array_proxy_method(prefix, func, classnam):
    return eval('lambda self, *args: n_array_proxy(poldekmod.%s%s(self, *args), %s)' %
                (module, prefix, func, classnam));

def _modsymbols(prefix, strip_prefix = None, with_methods = True, with_constants = True,
                verbose = 0, exclude = None):
    
    exclude_regexp = None
    regexp = re.compile('^%s' % prefix)
    regexp_up = re.compile('^%s' % string.upper(prefix))
    
    if exclude:
        exclude_regexp = re.compile('^%s' % exclude)
        
    if strip_prefix:
        l = len(strip_prefix)
    else:
        l = len(prefix)
        
    symbols = {}
    for k, elem in poldekmod.__dict__.items():
        key = None

        if exclude_regexp and exclude_regexp.match(k):
            if verbose:
                print "Exclude %s:: %s" % (prefix, k)
            continue
        
        if with_methods and regexp.match(k) and type(elem) == BuiltinFunctionType:
            key = k
            
        elif with_constants and regexp_up.match(k) and type(elem) != BuiltinFunctionType:
            key = k

        else:
            if verbose:
                print "skipped %s", k

        if key:        
            symbols[ key[l:] ] = elem

    return symbols


def _complete_class(aclass, prefix, strip_prefix = None, with_methods = True,
                    verbose = 0, exclude = None):
    exclude_regexp = None
    regexp = re.compile('^%s' % prefix)
    regexp_up = re.compile('^%s' % string.upper(prefix))
    toskip = [ '%snew' % prefix , '%sfree' % prefix, '%sswigregister' % prefix,
               '%sctx_swigregister' % prefix]

    if exclude:
        exclude_regexp = re.compile('^%s' % exclude)
        
    if strip_prefix:
        l = len(strip_prefix)
    else:
        l = len(prefix)
    for k, elem in poldekmod.__dict__.iteritems():
        
        if with_methods:
            if exclude_regexp and exclude_regexp.match(k):
                if verbose:
                    print "Exclude %s %s" % (aclass, k)
                continue
            
            if regexp.match(k) and type(elem) == BuiltinFunctionType:
                #print "FF %s %s" % (k, toskip)
                if k in toskip:
                    if verbose: print "XXXSkipped %s" % k
                    continue
                
                name = k[l:]
                if not hasattr(aclass, name):
                    fn = eval('lambda self, *args: poldekmod.%s(self, *args)' % k);
                    setattr(aclass, name, fn)
                    if verbose:
                        print "SET %s %s %s %s" % (k, aclass, name, type(fn))
                    #setattr(aclass, name, elem)
                 
                
        if regexp_up.match(k):
            name = k[l:]
            if not hasattr(aclass, name):
                setattr(aclass, name, elem)
                if verbose:
                    print "SET %s %s %s" % (aclass, name, type(elem))


_complete_class(tn_array, 'n_array_')
setattr(tn_array, '__getitem__', tn_array.nth)


## Package
for name, elem in capreq.__dict__.items():
    if name[0:4] == '_is_':
        setattr(capreq, name[1:], elem)
        
_complete_class(capreq, 'capreq_', verbose = 0)
setattr(capreq, '__str__', eval('lambda self: poldekmod.capreq_snprintf_s(self)'))

setattr(pkgflist_it, '__iter__', eval('lambda self: self'));
def _pkgflist_it_next(self):
    t = self.get_tuple()
    if t is None:
        raise StopIteration
    return t
setattr(pkgflist_it, 'next', _pkgflist_it_next);

_complete_class(pkg, 'pkg_', verbose = 0)
setattr(pkg, '__str__', pkg.id)
setattr(pkg, 'group', property(eval("lambda self: poldekmod.pkg_group(self)")))

for c in ['provides', 'requires', 'conflicts', 'suggests']:
    setattr(pkg, c, property(n_array_proxy_func('pkg.', '_get_%s' % c, 'capreq')))

setattr(pkg, 'files', property(lambda self: poldekmod.pkg_get_flist_it(self)));

tags = _modsymbols('pkguinf_', with_methods = False)
for k, v in tags.iteritems():
    fn = "lambda self: poldekmod.pkguinf_get(self, ord('%s'))" % v[0]
    setattr(pkguinf, k.lower(), property(eval(fn)))

## pkgdir
_complete_class(source, 'source_')
setattr(source, 'enabled', property(lambda self: self.get_enabled(),
                                    lambda self, val: self.set_enabled(val)))
_complete_class(pkgdir, 'pkgdir_')
setattr(pkgdir, 'packages', property(n_array_proxy_func('', 'get_packages', 'pkgdir')))

# poldek 
_complete_class(poldek_ctx, 'poldek_')
setattr(poldek_ctx, 'packages', property(n_array_proxy_func('poldek_', 'get_avail_packages', 'pkg')));
setattr(poldek_ctx, 'search', n_array_proxy_func('poldek_', 'search_avail_packages', 'pkg'))
setattr(poldek_ctx, 'sources', property(n_array_proxy_func('poldek_', 'get_sources', 'source')))
setattr(poldek_ctx, 'pkgdirs', property(n_array_proxy_func('poldek_', 'get_pkgdirs', 'pkgdir')))

_complete_class(poldek_ts, 'poldek_ts_')
_complete_class(poldek_ts, 'poldek_op_', strip_prefix = 'poldek_',
                with_methods = False, verbose = 0)



#_complete_class(poclidek_rcmd, 'poclidek_rcmd_')
setattr(poclidek_rcmd, 'packages',
        property(n_array_proxy_func('poclidek_rcmd_', 'get_packages', 'pkg')))

_complete_class(poclidek_ctx, 'poclidek_', verbose = 0, exclude = 'poclidek_rcmd_')

setattr(poclidek_ctx, 'rcmd', lambda self: poldekmod.poclidek_rcmd_new(self, None));
#print poclidek_ctx.rcmd_new
#setattr(poclidek_rcmd, 'rcmd'


#_complete_class(pkguinf, 'pkguinf_', exclude = 'pkguinf_get', verbose = 1, with_methods = False)
#setattr(pkguinf, 'get', eval('lambda self, tag: poldekmod.pkguinf_get(self, ord(tag[0]))'))
