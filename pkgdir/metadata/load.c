/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdint.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include <trurl/trurl.h>

#include "config.h"
#include "pkg.h"
#include "pkgu.h"
#include "pkgroup.h"
#include "capreq.h"
#include "pkgdir.h"
#include "log.h"
#include "vfile/vfile.h"
#include "load.h"


#define x_node_eq(node, node_name) (strcmp((char *) node->name, node_name) == 0)
#define x_xmlFree(v) do { if (v) { xmlFree(v); } } while (0)    

static void repomd_ent_free(struct repomd_ent *ent)
{
    if (ent->vf) {
        vfile_close(ent->vf);
        ent->vf = NULL;
    }
    free(ent);
}

static struct repomd_ent *load_repomd_ent(xmlNode *node)
{
    char *location = NULL, *type = NULL;
    char *checksum = NULL, *checksum_type = NULL;
    struct repomd_ent *ent;
    time_t ts = 0;
    int len;

    type = (char *) xmlGetProp(node, (const xmlChar *) "type");
    
    for (node = node->children; node; node = node->next) {
        if (x_node_eq(node, "location")) {
            location = (char *) xmlGetProp(node, (const xmlChar *) "href");
            
        } else if (x_node_eq(node, "checksum")) {
            checksum_type = (char *) xmlGetProp(node, (const xmlChar *) "type");
            checksum = (char *) xmlNodeGetContent(node);
                
        } else if (x_node_eq(node, "timestamp")) {
            char *s = (char *) xmlNodeGetContent(node);
            unsigned int t;
            if (sscanf(s, "%ul", &t) == 1)
                ts = t;
            else
                ts = 0;

            x_xmlFree(s);
        }
    }
    
    if (type == NULL || location == NULL || ts == 0 || checksum == NULL ||
        checksum_type == NULL)
        return NULL;

    len = strlen(location);
    ent = malloc(sizeof(*ent) + len + 1);
    ent->ts = ts;
    n_snprintf(ent->checksum, sizeof(ent->checksum), "%s", checksum);
    n_snprintf(ent->checksum_type, sizeof(ent->checksum_type), "%s",
               checksum_type);
    
    n_snprintf(ent->type, sizeof(ent->type), "%s", type);
    n_snprintf(ent->location, len + 1, "%s", location);

    x_xmlFree(type);
    x_xmlFree(location);
    x_xmlFree(checksum);
    x_xmlFree(checksum_type);
    ent->vf = NULL;
    return ent;
}

static tn_hash *do_load_repomd(xmlNode *node) 
{
    tn_hash *repomd;

    repomd = n_hash_new(16, (tn_fn_free)repomd_ent_free);
    n_hash_ctl(repomd, TN_HASH_NOCPKEY);
    
    for (; node; node = node->next) {
        struct repomd_ent *ent;
        
        if (node->type != XML_ELEMENT_NODE || strcmp((char *) node->name, "data") != 0)
            continue;
        
        if ((ent = load_repomd_ent(node))) {
            n_hash_insert(repomd, ent->type, ent);
            //printf("type = %s, loc = %s, ts = %ld\n", ent->type, ent->location, ent->ts);
        }
    }
    
    if (n_hash_size(repomd) == 0 || !n_hash_exists(repomd, "primary")) {
        logn(LOGERR, "repomd: empty or missing 'primary' entry");
        n_hash_free(repomd);
        repomd = NULL;
    }
    
    return repomd;
}

tn_hash *metadata_load_repomd(const char *path)
{
    xmlDocPtr doc;
    xmlNode   *root = NULL;
    tn_hash   *repomd = NULL;

    doc = xmlReadFile(path, NULL, XML_PARSE_NONET | XML_PARSE_NOERROR |
                      XML_PARSE_NOWARNING | XML_PARSE_NOBLANKS);
    if (doc == NULL) {
        logn(LOGERR, "%s: xml parser error", path);
        return NULL;
    }

    root = xmlDocGetRootElement(doc);
    if (root)
        repomd = do_load_repomd(root->children);

    xmlFreeDoc(doc);
    return repomd;
}

//<rpm:entry name="glibc-devel" flags="LT" epoch="0" ver="2.2.3", rel="foo" pri="1" />
static tn_array *load_caps(tn_alloc *na, tn_array *arr, xmlNode *node,
                           unsigned cr_flags)
{
    if (arr == NULL)
        arr = capreq_arr_new(0);
    
    for (node = node->children; node; node = node->next) {
        int32_t relflags = 0, epoch = 0;
        char *n, *e = NULL, *v = NULL, *r = NULL, *flags = NULL, *pre = NULL;
        struct capreq *cr;
        
        if (!x_node_eq(node, "entry"))
            continue;

        if ((n = (char *) xmlGetProp(node, (const xmlChar *) "name")) == NULL)
            continue;

        if ((pre = (char *) xmlGetProp(node, (const xmlChar *) "pre")) && *pre == '1')
            cr_flags |= CAPREQ_PREREQ;
        else 
            cr_flags &= ~CAPREQ_PREREQ;
        
        if ((flags = (char *) xmlGetProp(node, (const xmlChar *) "flags"))) {
            e = (char *) xmlGetProp(node, (const xmlChar *) "epoch");
            v = (char *) xmlGetProp(node, (const xmlChar *) "ver");
            r = (char *) xmlGetProp(node, (const xmlChar *) "rel");

            if (e && sscanf(e, "%d", &epoch) != 1)
                //logn(LOGERR, "%s: invalid epoch", e);
                goto l_continue;
            
            if (n_str_eq(flags, "EQ"))      relflags = REL_EQ;
            else if (n_str_eq(flags, "GE")) relflags = REL_EQ | REL_GT;
            else if (n_str_eq(flags, "LE")) relflags = REL_EQ | REL_LT;
            else if (n_str_eq(flags, "LT")) relflags = REL_LT;
            else if (n_str_eq(flags, "GT")) relflags = REL_GT;
        }
            
        cr = capreq_new(na, n, epoch, v, r, relflags, cr_flags);
        if (cr)
            n_array_push(arr, cr);
        
    l_continue:
        x_xmlFree(n);
        x_xmlFree(e);
        x_xmlFree(v);
        x_xmlFree(r);
        x_xmlFree(flags);
        x_xmlFree(pre);
    }
    
    if (n_array_size(arr))
        n_array_sort(arr);
    else
        n_array_cfree(&arr);

    return arr;
}

static struct pkg *load_package(tn_alloc *na, struct pkgroup_idx *pkgroups,
                                xmlNode *node)
{
    struct pkg pkg, *rpkg = NULL;
    char *arch = NULL;
    
    struct uinfo {
        char *summary;
        char *description;
        char *url;
        char *license;
        char *vendor;
        char *buildhost;
    } inf;
    
    memset(&inf, 0, sizeof(inf));
    memset(&pkg, 0, sizeof(pkg));
    
    for (node = node->children; node; node = node->next) {
        if (x_node_eq(node, "name")) {
            pkg.name = (char *) xmlNodeGetContent(node);
        
        } else if (x_node_eq(node, "arch")) {
            arch = (char *) xmlNodeGetContent(node);
            
            if (n_str_eq(arch, "src")) 
                goto l_skip_end;
            
        } else if (x_node_eq(node, "location")) {
            pkg.fn = (char *) xmlGetProp(node, (const xmlChar *) "href");
        
        } else if (x_node_eq(node, "version")) { 
            char *e;
            
            pkg.ver = (char *) xmlGetProp(node, (const xmlChar *) "ver");
            pkg.rel = (char *) xmlGetProp(node, (const xmlChar *) "rel");
            e = (char *) xmlGetProp(node, (const xmlChar *) "epoch");
            if (e)
                sscanf(e, "%d", &pkg.epoch);
            x_xmlFree(e);

        } else if (x_node_eq(node, "size")) {
            char *v;
            
            if ((v = (char *) xmlGetProp(node, (const xmlChar *) "package"))) {
                sscanf(v, "%d", &pkg.fsize);
                xmlFree(v);
            }
            
            if ((v = (char *) xmlGetProp(node, (const xmlChar *) "installed"))) {
                sscanf(v, "%d", &pkg.size);
                xmlFree(v);
            }
            
        } else if (x_node_eq(node, "time")) {
            char *v;
            
            if ((v = (char *) xmlGetProp(node, (const xmlChar *) "build"))) {
                sscanf(v, "%d", &pkg.btime);
                xmlFree(v);
            }
            
        } else if (x_node_eq(node, "format")) {
            xmlNode *n = NULL;
            for (n = node->children; n; n = n->next) {
                if (x_node_eq(n, "provides"))
                    pkg.caps = load_caps(na, pkg.caps, n, 0);
                
                else if (x_node_eq(n, "requires"))
                    pkg.reqs = load_caps(na, NULL, n, 0);

                else if (x_node_eq(n, "conflicts"))
                    pkg.cnfls = load_caps(na, pkg.cnfls, n, 0);

                else if (x_node_eq(n, "obsoletes"))
                    pkg.cnfls = load_caps(na, pkg.cnfls, n, CAPREQ_OBCNFL);
                
                else if (x_node_eq(n, "file")) {
                    char *path = (char *) xmlNodeGetContent(n);
                    struct capreq *cr = capreq_new(na, path, 0, NULL,
                                                   NULL, 0, 0);
                    if (pkg.caps == NULL)
                        pkg.caps = capreq_arr_new(0);
                    n_array_push(pkg.caps, cr);
                    x_xmlFree(path);
                    
                } else if (x_node_eq(n, "license")) {
                    inf.license = (char *) xmlNodeGetContent(n);
                    
                } else if (x_node_eq(n, "vendor")) {
                    inf.vendor = (char *) xmlNodeGetContent(n);
                    
                } else if (x_node_eq(n, "buildhost")) {
                    inf.buildhost = (char *) xmlNodeGetContent(n);
                    
                } else if (x_node_eq(n, "group")) {
                    char *g;
                    if ((g = (char *) xmlNodeGetContent(n))) {
                        pkg.groupid = pkgroup_idx_add(pkgroups, g);
                        xmlFree(g);
                    }
                }
            }
            
        } else if (x_node_eq(node, "summary")) {
            inf.summary = (char *) xmlNodeGetContent(node);
            
        } else if (x_node_eq(node, "description")) {
            inf.description = (char *) xmlNodeGetContent(node);
            
        } else if (x_node_eq(node, "url")) {
            inf.url = (char *) xmlNodeGetContent(node);
        }
    }
    
    rpkg = pkg_new_ext(na, pkg.name, pkg.epoch, pkg.ver, pkg.rel, arch, NULL, 
                       pkg.fn, NULL, pkg.size, pkg.fsize, pkg.btime);

l_skip_end:
    
    x_xmlFree(pkg.name);
    x_xmlFree(arch);
    x_xmlFree(pkg.fn);
    x_xmlFree(pkg.ver);
    x_xmlFree(pkg.rel);
    
    if (rpkg == NULL) {
        if (pkg.caps) n_array_free(pkg.caps);
        if (pkg.reqs) n_array_free(pkg.reqs);
        if (pkg.cnfls) n_array_free(pkg.cnfls);
        
    } else {
        if (pkg.caps)
            n_array_sort(pkg.caps); /* files are pushed to it too */

        rpkg->caps = pkg.caps;
        rpkg->reqs = pkg.reqs;
        rpkg->cnfls = pkg.cnfls;
        rpkg->groupid = pkg.groupid;

        if (inf.summary && inf.description) {
            struct pkguinf *pkgu = pkguinf_new(na);
            
            if (inf.url && *inf.url)
                pkguinf_set(pkgu, PKGUINF_URL, inf.url, NULL);
            
            if (inf.license && *inf.license)
                pkguinf_set(pkgu, PKGUINF_LICENSE, inf.license, NULL);
            
            if (inf.vendor && *inf.vendor)
                pkguinf_set(pkgu, PKGUINF_VENDOR, inf.vendor, NULL);
            
            if (inf.buildhost && *inf.buildhost)
                pkguinf_set(pkgu, PKGUINF_BUILDHOST, inf.buildhost, NULL);
            
            pkguinf_set(pkgu, PKGUINF_SUMMARY, inf.summary, "C");
            pkguinf_set(pkgu, PKGUINF_DESCRIPTION, inf.description, "C");

            rpkg->pkg_pkguinf = pkgu;
            pkg_set_ldpkguinf(rpkg);
        }
        
        msgn(3, "ld %s", pkg_snprintf_s(rpkg));
    } 

    x_xmlFree(inf.summary);
    x_xmlFree(inf.description);
    x_xmlFree(inf.url);
    x_xmlFree(inf.license);
    x_xmlFree(inf.vendor);
    x_xmlFree(inf.buildhost);
    
    return rpkg;
}
    

tn_array *metadata_load_primary(struct pkgdir *pkgdir, const char *path)
{
    xmlDocPtr doc; 
    xmlNode  *root = NULL, *node;
    tn_array *pkgs;

    pkgs = n_array_new(1024, NULL, NULL);
    doc = xmlReadFile(path, NULL, XML_PARSE_NONET);

    if (doc == NULL) {
        logn(LOGERR, "%s: parser error", path);
        return NULL;
    }

    if ((root = xmlDocGetRootElement(doc)) == NULL) {
        xmlFreeDoc(doc);
        return NULL;
    }
    
    for (node = root->children; node; node = node->next) {
        char *type;
        
        if (node->type != XML_ELEMENT_NODE || strcmp((char *) node->name, "package") != 0)
            continue;

        if ((type = (char *) xmlGetProp(node, (const xmlChar *) "type")) && strcmp(type, "rpm") == 0) {
            struct pkg *pkg;
            if ((pkg = load_package(pkgdir->na, pkgdir->pkgroups, node)))
                n_array_push(pkgs, pkg);
            MEMINF("%s", pkg ? pkg_snprintf_s(pkg) : "null");
        }
        
        x_xmlFree(type);
    }
    
    xmlFreeDoc(doc);
    MEMINF("XMLFREE_END");
    return pkgs;
}

void metadata_loadmod_init(void) 
{
    LIBXML_TEST_VERSION
}

void metadata_loadmod_destroy(void) 
{
    xmlCleanupParser();
}

