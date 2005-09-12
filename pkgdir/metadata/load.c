/* $Id$ */

#include <stdio.h>
#include <stdint.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include <trurl/trurl.h>

#include "pkg.h"
#include "capreq.h"
#include "log.h"
#include "vfile/vfile.h"
#include "load.h"


#define x_node_eq(node, node_name) (strcmp(node->name, node_name) == 0)

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
    const char *location = NULL, *type = NULL;
    const char *checksum = NULL, *checksum_type = NULL;
    struct repomd_ent *ent;
    time_t ts = 0;
    int len;


    type = xmlGetProp(node, "type");
    for (node = node->children; node; node = node->next) {
        if (x_node_eq(node, "location")) {
            location = xmlGetProp(node, "href");
            
        } else if (x_node_eq(node, "checksum")) {
            checksum_type = xmlGetProp(node, "type");
            checksum = xmlNodeGetContent(node);
                
        } else if (x_node_eq(node, "timestamp")) {
            char *s = xmlNodeGetContent(node);
            unsigned int t;
            if (sscanf(s, "%ul", &t) == 1)
                ts = t;
            else
                ts = 0;
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
        
        if (node->type != XML_ELEMENT_NODE || strcmp(node->name, "data") != 0)
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
static tn_array *load_caps(tn_alloc *na, tn_array *arr, xmlNode *node, unsigned cr_flags)
{
    if (arr == NULL)
        arr = capreq_arr_new(0);
    
    for (node = node->children; node; node = node->next) {
        int32_t relflags = 0, epoch = 0;
        const char *n, *e = NULL, *v = NULL, *r = NULL, *flags = NULL, *pre = NULL;
        struct capreq *cr;
        
        if (!x_node_eq(node, "entry"))
            continue;

        if ((n = xmlGetProp(node, "name")) == NULL)
            continue;

        if ((pre = xmlGetProp(node, "pre")) && *pre == '1')
            cr_flags |= CAPREQ_PREREQ;
        else 
            cr_flags &= ~CAPREQ_PREREQ;
        
        if ((flags = xmlGetProp(node, "flags"))) {
            e = xmlGetProp(node, "epoch");
            v = xmlGetProp(node, "ver");
            r = xmlGetProp(node, "rel");

            if (e && sscanf(e, "%d", &epoch) != 1)
                //logn(LOGERR, "%s: invalid epoch", e);
                continue;

            if (n_str_eq(flags, "EQ"))      relflags = REL_EQ;
            else if (n_str_eq(flags, "GE")) relflags = REL_EQ | REL_GT;
            else if (n_str_eq(flags, "LE")) relflags = REL_EQ | REL_LT;
            else if (n_str_eq(flags, "LT")) relflags = REL_LT;
            else if (n_str_eq(flags, "GT")) relflags = REL_GT;
        }
            
        cr = capreq_new(na, n, epoch, v, r, relflags, cr_flags);
        if (cr)
            n_array_push(arr, cr);
    }
    if (n_array_size(arr))
        n_array_sort(arr);
    else
        n_array_cfree(&arr);

    return arr;
}

static struct pkg *load_package(tn_alloc *na, xmlNode *node)
{
    struct pkg pkg, *rpkg;
    char *arch;

    memset(&pkg, 0, sizeof(pkg));
    
    for (node = node->children; node; node = node->next) {
        if (x_node_eq(node, "name"))
            pkg.name = xmlNodeGetContent(node);
        
        else if (x_node_eq(node, "arch"))
            arch = xmlNodeGetContent(node);

        else if (x_node_eq(node, "location"))
            pkg.fn = xmlGetProp(node, "href");
        
        else if (x_node_eq(node, "version")) {
            char *e;
            
            pkg.ver = xmlGetProp(node, "ver");
            pkg.rel = xmlGetProp(node, "rel");
            e = xmlGetProp(node, "epoch");
            if (e)
                sscanf(e, "%d", &pkg.epoch);
            
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
                    const char *path = xmlNodeGetContent(n);
                    struct capreq *cr = capreq_new(na, path, 0, NULL, NULL, 0, 0);
                    if (pkg.caps == NULL)
                        pkg.caps = capreq_arr_new(0);
                    n_array_push(pkg.caps, cr);
                }
            }
        }
    }
    
    rpkg = pkg_new_ext(na, pkg.name, pkg.epoch, pkg.ver, pkg.rel, arch, NULL, 
                       pkg.fn, 0, 0, 0); //pkgt->size, pkgt->fsize, pkgt->btime);

    if (pkg.caps)
        n_array_sort(pkg.caps); /* files are pushed to it too */
    
    rpkg->caps = pkg.caps;
    rpkg->reqs = pkg.reqs;
    rpkg->cnfls = pkg.cnfls;
    msgn(3, "ld %s", pkg_snprintf_s(rpkg));
    return rpkg;
}
    

tn_array *metadata_load_primary(tn_alloc *na, const char *path)
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
        
        if (node->type != XML_ELEMENT_NODE || strcmp(node->name, "package") != 0)
            continue;

        if ((type = xmlGetProp(node, "type")) && strcmp(type, "rpm") == 0) {
            struct pkg *pkg;
            if ((pkg = load_package(na, node)))
                n_array_push(pkgs, pkg);
        }
    }
    
    xmlFreeDoc(doc);
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

