/*
  Copyright (C) 2009 Marcin Banasiak <megabajt@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

/* for asprintf() from stdio.h */
#define _GNU_SOURCE 

/* FIXME: nbuf.h should include stdint.h */
#include <stdint.h>

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <trurl/trurl.h>

#include "capreq.h"
#include "i18n.h"
#include "log.h"
#include "ls_queryfmt.h"
#include "pkgu.h"
#include "pkgfl.h"

#define n_strcase_eq(s, p) (strcasecmp(s, p) == 0)

static const char *invalid_format = N_("invalid format:");

enum LsqfParseMode {
    LSQF_PARSE_NORMAL = 0,
    LSQF_PARSE_ARRAY
};

/* Tags have to be in alphabetical order. */
enum {
    LSQF_TAG_UNKNOWN = -1,
    
    LSQF_TAG_ARCH = 0,
    LSQF_TAG_BASENAMES,
    LSQF_TAG_BUILDHOST,
    LSQF_TAG_BUILDTIME,
    LSQF_TAG_CONFLICTFLAGS,
    LSQF_TAG_CONFLICTS,
    LSQF_TAG_CONFLICTVERSION,
    LSQF_TAG_DESCRIPTION,
    LSQF_TAG_DIRNAMES,
    LSQF_TAG_EPOCH,
    LSQF_TAG_FILELINKTOS,
    LSQF_TAG_FILEMODES,
    LSQF_TAG_FILENAMES,
    LSQF_TAG_FILESIZES,
    LSQF_TAG_GROUP,
    LSQF_TAG_LICENSE,
    LSQF_TAG_NAME,
    LSQF_TAG_NVRA,
    LSQF_TAG_OBSOLETEFLAGS,
    LSQF_TAG_OBSOLETES,
    LSQF_TAG_OBSOLETEVERSION,
    LSQF_TAG_PACKAGECOLOR,
    LSQF_TAG_PROVIDEFLAGS,
    LSQF_TAG_PROVIDES,
    LSQF_TAG_PROVIDEVERSION,
    LSQF_TAG_RELEASE,
    LSQF_TAG_REQUIREFLAGS,
    LSQF_TAG_REQUIRES,
    LSQF_TAG_REQUIREVERSION,
    LSQF_TAG_SIZE,
    LSQF_TAG_SOURCERPM,
    LSQF_TAG_SUGGESTSFLAGS,
    LSQF_TAG_SUGGESTS,
    LSQF_TAG_SUGGESTSVERSION,
    LSQF_TAG_SUMMARY,
    LSQF_TAG_URL,
    LSQF_TAG_VENDOR,
    LSQF_TAG_VERSION,
    
    LSQF_N_TAGS
};

enum {
    LSQF_TAG_OUTFMTFN_NONE = 0,
    
    LSQF_TAG_OUTFMTFN_DATE,
    LSQF_TAG_OUTFMTFN_DAY,
    LSQF_TAG_OUTFMTFN_DEPFLAGS
};

struct lsqf_tags {
    int         tagid;
    int         is_array : 1;
    int         need_uinf : 1;
    int         need_flist : 1;
    const char *tagname[4];
};

/*
 * LSQF_TAG_* have to be added in the same sequence as in enumeration
 * (LSQF_TAG_ value is an index in lsqf_tags[] array).
 */
static const struct lsqf_tags lsqf_tags[] = {
    { LSQF_TAG_ARCH,            0, 0, 0, { "ARCH", NULL } },
    { LSQF_TAG_BASENAMES,       1, 0, 1, { "BASENAMES", NULL } },
    { LSQF_TAG_BUILDHOST,       0, 1, 0, { "BUILDHOST", NULL } },
    { LSQF_TAG_BUILDTIME,       0, 0, 0, { "BUILDTIME", NULL } },
    { LSQF_TAG_CONFLICTFLAGS,   1, 0, 0, { "CONFLICTFLAGS", NULL } },
    { LSQF_TAG_CONFLICTS,       1, 0, 0, { "C", "CONFLICTNAME", "CONFLICTS", NULL } },
    { LSQF_TAG_CONFLICTVERSION, 1, 0, 0, { "CONFLICTVERSION", NULL } },
    { LSQF_TAG_DESCRIPTION,     0, 1, 0, { "DESCRIPTION", NULL } },
    { LSQF_TAG_DIRNAMES,        1, 0, 1, { "DIRNAMES", NULL } },
    { LSQF_TAG_EPOCH,           0, 0, 0, { "E", "EPOCH", NULL } },
    { LSQF_TAG_FILELINKTOS,     1, 0, 1, { "FILELINKTOS", NULL } },
    { LSQF_TAG_FILEMODES,       1, 0, 1, { "FILEMODES", NULL } },
    { LSQF_TAG_FILENAMES,       1, 0, 1, { "FILENAMES", NULL } },
    { LSQF_TAG_FILESIZES,       1, 0, 1, { "FILESIZES", NULL } },
    { LSQF_TAG_GROUP,           0, 0, 0, { "GROUP", NULL } },
    { LSQF_TAG_LICENSE,         0, 1, 0, { "LICENSE", NULL } },
    { LSQF_TAG_NAME,            0, 0, 0, { "N", "NAME", NULL } },
    { LSQF_TAG_NVRA,            0, 0, 0, { "NVRA", NULL } },
    { LSQF_TAG_OBSOLETEFLAGS,   1, 0, 0, { "OBSOLETEFLAGS", NULL } },
    { LSQF_TAG_OBSOLETES,       1, 0, 0, { "O", "OBSOLETENAME", "OBSOLETES", NULL } },
    { LSQF_TAG_OBSOLETEVERSION, 1, 0, 0, { "OBSOLETEVERSION", NULL } },
    { LSQF_TAG_PACKAGECOLOR,    0, 0, 0, { "PACKAGECOLOR", NULL } },
    { LSQF_TAG_PROVIDEFLAGS,    1, 0, 0, { "PROVIDEFLAGS", NULL } },
    { LSQF_TAG_PROVIDES,        1, 0, 0, { "P", "PROVIDENAME", "PROVIDES", NULL } },
    { LSQF_TAG_PROVIDEVERSION,  1, 0, 0, { "PROVIDEVERSION", NULL } },
    { LSQF_TAG_RELEASE,         0, 0, 0, { "R", "RELEASE", NULL } },
    { LSQF_TAG_REQUIREFLAGS,    1, 0, 0, { "REQUIREFLAGS", NULL } },
    { LSQF_TAG_REQUIRES,        1, 0, 0, { "REQUIRENAME", "REQUIRES", NULL } },
    { LSQF_TAG_REQUIREVERSION,  1, 0, 0, { "REQUIREVERSION", NULL } },
    { LSQF_TAG_SIZE,            0, 0, 0, { "SIZE", NULL } },
    { LSQF_TAG_SOURCERPM,       0, 0, 0, { "SOURCERPM", NULL } },
    { LSQF_TAG_SUGGESTSFLAGS,   1, 0, 0, { "SUGGESTSFLAGS", NULL } },
    { LSQF_TAG_SUGGESTS,        1, 0, 0, { "SUGGESTS", "SUGGESTSNAME", NULL } },
    { LSQF_TAG_SUGGESTSVERSION, 1, 0, 0, { "SUGGESTSVERSION", NULL } },
    { LSQF_TAG_SUMMARY,         0, 1, 0, { "SUMMARY", NULL } },
    { LSQF_TAG_URL,             0, 1, 0, { "URL", NULL } },
    { LSQF_TAG_VENDOR,          0, 1, 0, { "VENDOR", NULL } },
    { LSQF_TAG_VERSION,         0, 0, 0, { "V", "VERSION", NULL } },
    { LSQF_N_TAGS,              0, 0, 0, { NULL } }
};

struct lsqf_pkgdata {
    const struct pkg  *pkg;
    struct pkgflist   *flist;
    struct pkguinf    *uinf;
};

static struct lsqf_pkgdata *lsqf_pkgdata_new(const struct pkg *pkg)
{
    struct lsqf_pkgdata *pkgdata = NULL;
    
    pkgdata = n_malloc(sizeof(struct lsqf_pkgdata));
    
    if (pkgdata) {
	pkgdata->pkg = pkg;
	pkgdata->flist = NULL;
	pkgdata->uinf = NULL;
    }
    
    return pkgdata;
}

static struct pkgflist *lsqf_pkgdata_flist(struct lsqf_pkgdata *pkgdata)
{
    if (pkgdata->flist == NULL)
	pkgdata->flist = pkg_get_flist(pkgdata->pkg);

    return pkgdata->flist;
}

static struct pkguinf *lsqf_pkgdata_uinf(struct lsqf_pkgdata *pkgdata)
{
    if (pkgdata->uinf == NULL)
	pkgdata->uinf = pkg_uinf(pkgdata->pkg);

    return pkgdata->uinf;
}

static void lsqf_pkgdata_free(struct lsqf_pkgdata *pkgdata)
{
    if (pkgdata) {
	if (pkgdata->flist)
	    pkgflist_free(pkgdata->flist);

	if (pkgdata->uinf)
	    pkguinf_free(pkgdata->uinf);
    
	n_free(pkgdata);
    }
}

static int get_tagid_by_name(char *tag)
{
    if (tag) {
	unsigned int i, j;
	
	for (i = 0; i < LSQF_N_TAGS; i++) {    
	    for (j = 0; lsqf_tags[i].tagname[j]; j++) {
		if (n_strcase_eq(tag, lsqf_tags[i].tagname[j])) {
		    return lsqf_tags[i].tagid;
		}
	    }
	}
    }

    return LSQF_TAG_UNKNOWN;
}

static int get_outfmtfnid_by_name(char *outfmtfn)
{
    if (outfmtfn && *outfmtfn) {
	if (n_str_eq(outfmtfn, "date"))
	    return LSQF_TAG_OUTFMTFN_DATE;
	if (n_str_eq(outfmtfn, "day"))
	    return LSQF_TAG_OUTFMTFN_DAY;
	if (n_str_eq(outfmtfn, "depflags"))
	    return LSQF_TAG_OUTFMTFN_DEPFLAGS;
    }
    
    return LSQF_TAG_OUTFMTFN_NONE;
}

/* TODO: move to capreq.c */
static int capreq_snprintf_evr(char *str, size_t size, const struct capreq *cr)
{
    int n = 0;
    
    n_assert(size > 0);
    
    if (capreq_has_epoch(cr))
	n += n_snprintf(&str[n], size - n, "%d:", capreq_epoch(cr));

    if (capreq_has_ver(cr)) 
	n += n_snprintf(&str[n], size - n, "%s", capreq_ver(cr));

    if (capreq_has_rel(cr)) {
        n_assert(capreq_has_ver(cr));

        n += n_snprintf(&str[n], size - n, "-%s", capreq_rel(cr));
    }
    
    return n;
}

static char *format_date(int outfmtfnid, uint32_t time)
{
    char *buf = NULL, datestr[32];;
    
    if (outfmtfnid == LSQF_TAG_OUTFMTFN_DATE) {
	strftime(datestr, sizeof(datestr), "%c", gmtime((time_t *)&time));
	asprintf(&buf, "%s", datestr);
    } else if (outfmtfnid == LSQF_TAG_OUTFMTFN_DAY) {
	strftime(datestr, sizeof(datestr), "%a %b %d %Y", gmtime((time_t *)&time));
	asprintf(&buf, "%s", datestr);
    } else {
	asprintf(&buf, "%u", time);
    }
    
    return buf;
}

static char *format_flags(int outfmtfnid, struct capreq *cr)
{
    char *buf = NULL;

    if (outfmtfnid == LSQF_TAG_OUTFMTFN_DEPFLAGS) {
	char relstr[3], *p;

	p = relstr;
	*p = '\0';
	
	if (cr->cr_relflags & REL_LT)
	    *p++ = '<';
	else if (cr->cr_relflags & REL_GT)
	    *p++ = '>';
	
	if (cr->cr_relflags & REL_EQ)
	    *p++ = '=';
	
	*p = '\0';
	
	asprintf(&buf, " %s ", relstr);
    } else {
	asprintf(&buf, "%u", cr->cr_relflags);
    }
    
    return buf;
}

static char *get_str_by_tagid(const struct lsqf_ent *ent, struct lsqf_pkgdata *pkgdata, unsigned int num)
{
    const struct pkg *pkg = pkgdata->pkg;
    struct capreq *c = NULL;
    char *buf = NULL, evr[32];
    unsigned int i;

    if (lsqf_tags[ent->tag.id].need_uinf) {
	struct pkguinf *pkgu = lsqf_pkgdata_uinf(pkgdata);
	const char *str = NULL;
    
	switch (ent->tag.id) {
	    case LSQF_TAG_BUILDHOST:
		str = pkguinf_get(pkgu, PKGUINF_BUILDHOST);
		break;
	    
	    case LSQF_TAG_DESCRIPTION:
		str = pkguinf_get(pkgu, PKGUINF_DESCRIPTION);
		break;
	    
	    case LSQF_TAG_LICENSE:
		str = pkguinf_get(pkgu, PKGUINF_LICENSE);
		break;
	    
	    case LSQF_TAG_SUMMARY:
		str = pkguinf_get(pkgu, PKGUINF_SUMMARY);
		break;
		
	    case LSQF_TAG_URL:
		str = pkguinf_get(pkgu, PKGUINF_URL);
		break;
	
	    case LSQF_TAG_VENDOR:
		str = pkguinf_get(pkgu, PKGUINF_VENDOR);
		break;
	    
	    default:
		n_assert(0);
	}
	
	if (str)
	    buf = n_strdup(str);
	else
	    buf = n_strdup("(none)");
    
    } else if (lsqf_tags[ent->tag.id].need_flist) {
	struct pkgflist *flist = lsqf_pkgdata_flist(pkgdata);
	struct pkgfl_ent *flent;
	
	if (flist) {
	    switch (ent->tag.id) {
		case LSQF_TAG_BASENAMES:
    		case LSQF_TAG_FILELINKTOS:
    		case LSQF_TAG_FILEMODES:
    		case LSQF_TAG_FILENAMES:
		case LSQF_TAG_FILESIZES:
		    for (i = 0; i < n_tuple_size(flist->fl); i++) {
			flent = n_tuple_nth(flist->fl, i);
			
			if (flent->items <= num)
			    num -= flent->items;
			else
			    break;
		    }
		    
		    if (ent->tag.id == LSQF_TAG_BASENAMES)
			buf = n_strdup(flent->files[num]->basename);
		    else if (ent->tag.id == LSQF_TAG_FILEMODES)
			asprintf(&buf, "%u", flent->files[num]->mode);
		    else if (ent->tag.id == LSQF_TAG_FILENAMES) {
			if (*flent->dirname == '/')
			    asprintf(&buf, "%s%s", flent->dirname, flent->files[num]->basename);
			else
			    asprintf(&buf, "/%s%s%s", flent->dirname,
						      *flent->files[num]->basename ? "/" : "",
						      flent->files[num]->basename);
		    } else if (ent->tag.id == LSQF_TAG_FILESIZES)
			asprintf(&buf, "%u", flent->files[num]->size);
		    else
			if (S_ISLNK(flent->files[num]->mode))
			    buf = n_strdup(flent->files[num]->basename + strlen(flent->files[num]->basename) + 1);
		
		    break;
		
		case LSQF_TAG_DIRNAMES:
		    flent = n_tuple_nth(flist->fl, num);
		    
		    asprintf(&buf, "%s%s", *flent->dirname == '/' ? "" : "/",
					   flent->dirname);
		    break;

		default:
		    n_assert(0);
	    }
	}
    } else {
	switch (ent->tag.id) {
	    case LSQF_TAG_ARCH:
		buf = n_strdup(pkg_arch(pkg));
		break;

	    case LSQF_TAG_BUILDTIME:
		buf = format_date(ent->tag.outfmtfnid, pkg->btime);
		break;
	    
	    case LSQF_TAG_CONFLICTFLAGS:
	    case LSQF_TAG_CONFLICTS:
	    case LSQF_TAG_CONFLICTVERSION:
	    case LSQF_TAG_OBSOLETEFLAGS:
	    case LSQF_TAG_OBSOLETES:
	    case LSQF_TAG_OBSOLETEVERSION:
	    {
		unsigned int n = 0;
				
		for (i = 0; i < n_array_size(pkg->cnfls); i++) {
		    struct capreq *cr = n_array_nth(pkg->cnfls, i);
		    
		    if (ent->tag.id == LSQF_TAG_CONFLICTS || ent->tag.id == LSQF_TAG_CONFLICTFLAGS) {
			if (!capreq_is_obsl(cr)) {
			    if (n == num) {
				c = cr;
				break;
			    }
			
			    n++;
			}
		    } else {
			if (capreq_is_obsl(cr)) {
			    if (n == num) {
				c = cr;
				break;
			    }
			    
			    n++;
			}
		    }
		}
	    
		if (c) {
		    if (ent->tag.id == LSQF_TAG_CONFLICTS || ent->tag.id == LSQF_TAG_OBSOLETES)
			buf = n_strdup(capreq_name(c));
		    else if (ent->tag.id == LSQF_TAG_CONFLICTFLAGS || ent->tag.id == LSQF_TAG_OBSOLETEFLAGS)
			buf = format_flags(ent->tag.outfmtfnid, c);
		    else if (ent->tag.id == LSQF_TAG_CONFLICTVERSION || ent->tag.id == LSQF_TAG_OBSOLETEVERSION)
			if (capreq_snprintf_evr(evr, sizeof(evr), c) > 0)
			    buf = n_strdup(evr);
		
		}
		break;
	    }

	    case LSQF_TAG_EPOCH:
		asprintf(&buf, "%d", pkg->epoch);
		break;
	
	    case LSQF_TAG_GROUP:
		buf = n_strdup(pkg_group(pkg));
		break;
	
	    case LSQF_TAG_NAME:
		buf = n_strdup(pkg->name);
		break;
	
	    case LSQF_TAG_NVRA:
		buf = n_strdup(pkg_id(pkg));
		break;
	
	    case LSQF_TAG_PACKAGECOLOR:
		asprintf(&buf, "%d", pkg->color);
		break;

	    case LSQF_TAG_PROVIDEFLAGS:
		buf = format_flags(ent->tag.outfmtfnid, n_array_nth(pkg->caps, num));
		break;

	    case LSQF_TAG_PROVIDES:
		c = n_array_nth(pkg->caps, num);
		buf = n_strdup(capreq_name(c));
		break;
	
	    case LSQF_TAG_PROVIDEVERSION:
		c = n_array_nth(pkg->caps, num);
		
		if (capreq_snprintf_evr(evr, sizeof(evr), c) > 0)
		    buf = n_strdup(evr);
		
		break;

	    case LSQF_TAG_RELEASE:
		buf = n_strdup(pkg->rel);
		break;

	    case LSQF_TAG_REQUIREFLAGS:
		buf = format_flags(ent->tag.outfmtfnid, n_array_nth(pkg->reqs, num));
		break;

	    case LSQF_TAG_REQUIRES:
		c = n_array_nth(pkg->reqs, num);
	    
		if (capreq_is_rpmlib(c))
		    asprintf(&buf, "rpmlib(%s)", capreq_name(c));
		else
		    buf = n_strdup(capreq_name(c));
	
		break;

	    case LSQF_TAG_REQUIREVERSION:
		c = n_array_nth(pkg->reqs, num);
		
		if (capreq_snprintf_evr(evr, sizeof(evr), c) > 0)
		    buf = n_strdup(evr);
		
		break;

	    case LSQF_TAG_VERSION:
		buf = n_strdup(pkg->ver);
		break;
	
	    case LSQF_TAG_SIZE:
		asprintf(&buf, "%u", pkg->size);
		break;

	    case LSQF_TAG_SOURCERPM:
		buf = n_strdup(pkg_srcfilename_s(pkg));
		break;

	    case LSQF_TAG_SUGGESTSFLAGS:
		buf = format_flags(ent->tag.outfmtfnid, n_array_nth(pkg->sugs, num));
		break;

	    case LSQF_TAG_SUGGESTS:
		c = n_array_nth(pkg->sugs, num);
		buf = n_strdup(capreq_name(c));
		break;

	    case LSQF_TAG_SUGGESTSVERSION:
		c = n_array_nth(pkg->sugs, num);
		
		if (capreq_snprintf_evr(evr, sizeof(evr), c) > 0)
		    buf = n_strdup(evr);
		
		break;

	    default:
		n_assert(0);
	}
    }
    
    return buf;
}

static char get_escaped_char(char zn)
{
    switch (zn) {
	case 'a': return '\a';
	case 'b': return '\b';
	case 'f': return '\f';
	case 'n': return '\n';
	case 'r': return '\r';
	case 't': return '\t';
	case 'v': return '\v';
	default:  return zn;
    }
}

static struct lsqf_ent *lsqf_ent_new(int type)
{
    struct lsqf_ent *ent = NULL;

    ent = n_malloc(sizeof(struct lsqf_ent));
    
    if (ent) {
	ent->type = type;
    
	switch (type) {
	    case LSQF_ENT_TYPE_TAG:
		ent->tag.id = 0;
		ent->tag.iterate = 0;
		ent->tag.countArray = 0;
		ent->tag.pad = 0;
		break;
	
	    case LSQF_ENT_TYPE_STRING:
		ent->string = NULL;
		break;

	    case LSQF_ENT_TYPE_ARRAY:
		ent->array = lsqf_ent_array_new();
		break;

	    default:
		n_assert(0);
	}
    }
    
    return ent;
}

static void lsqf_ent_free(struct lsqf_ent *ent)
{
    if (ent) {
	switch (ent->type) {
	    case LSQF_ENT_TYPE_TAG:
		break;

	    case LSQF_ENT_TYPE_STRING:
		if (ent->string)
		    n_free(ent->string);
		
		break;
		
	    case LSQF_ENT_TYPE_ARRAY:
		lsqf_ent_array_free(ent->array);
		break;
	}
	
	n_free(ent);
    }
}

struct lsqf_ent_array *lsqf_ent_array_new(void)
{
    struct lsqf_ent_array *array = NULL;
    
    array = n_malloc(sizeof(struct lsqf_ent_array));
    
    if (array) {
	array->ents = NULL;
	array->items = 0;
    }
    
    return array;
}

void lsqf_ent_array_free(struct lsqf_ent_array *array)
{
    unsigned int i;

    if (array) {
	for (i = 0; i < array->items; i++) {
	    struct lsqf_ent *ent = array->ents[i];
	
	    lsqf_ent_free(ent);
	}
    
	if (array->ents)
	    n_free(array->ents);

	n_free(array);
    }
}

static void lsqf_ent_array_add_ent(struct lsqf_ent_array *array, struct lsqf_ent *ent)
{
    array->ents = n_realloc(array->ents, (array->items + 1) * sizeof(struct lsqf_ent *));
    
    if (array->ents) {
	array->ents[array->items] = ent;
    
	array->items++;
    }
}

static void lsqf_ent_array_add_ent_string(struct lsqf_ent_array *array, tn_buf *nbuf)
{
    struct lsqf_ent *ent = NULL;

    if (n_buf_size(nbuf) > 0) {
	n_buf_putc(nbuf, '\0');
	
	if ((ent = lsqf_ent_new(LSQF_ENT_TYPE_STRING))) {
	    ent->string = n_strdup(n_buf_ptr(nbuf));

	    lsqf_ent_array_add_ent(array, ent);
		    
	    n_buf_clean(nbuf);
	}
    }
}

/**
 * do_parse:
 *
 * Returns: 1 on error.
 **/
static int do_parse(struct lsqf_ent_array *array, char *fmt, char **endfmt, enum LsqfParseMode mode)
{
    struct lsqf_ent *ent;
    tn_buf *nbuf = NULL;
    int done = 0, error = 0;
    char *end;

    if (fmt == NULL)
	return 1;

    nbuf = n_buf_new(8);

    while (*fmt && !done && !error) {
	switch (*fmt) {
	    case '%':
	    {
		char *p, *outfmtfn;
		int tagid, pad, countArray = 0, iterate = 0;
		
		fmt++;
		
		/* catch %% */
		if (*fmt == '%') {
		    n_buf_putc(nbuf, '%');
		    break;
		}
		
		pad = strtoul(fmt, &p, 10);
		
		fmt = p;
		
		if (*fmt != '{') {
		    logn(LOGERR, _("%s missing { after %%"), invalid_format);
		    error = 1;
		    break;
		}
		
		fmt++;

		if (*fmt == '#') {
		    countArray = 1;
		    fmt++;
		} else if (*fmt == '=') {
		    iterate = 1;
		    fmt++;
		}
		
		if ((p = strchr(fmt, '}')) == NULL) {
		    logn(LOGERR, _("%s missing } after %%{"), invalid_format);		    
		    error = 1;
		    break;
		}
		
		*p = '\0';
		
		if (*fmt == '\0') {
		    logn(LOGERR, _("%s empty tag name"), invalid_format);
		    error = 1;
		    break;
		}
		
		/* check if another output format is requested */
		if ((outfmtfn = strchr(fmt, ':')) != NULL) {
		    *outfmtfn = '\0';
		    outfmtfn++;
		}
		
		if ((tagid = get_tagid_by_name(fmt)) == LSQF_TAG_UNKNOWN) {
		    logn(LOGERR, _("%s unknown tag: \'%s\'"), invalid_format, fmt);
		    error = 1;
		    break;
		}
		
		/* create new ent with a string that is currently stored in nbuf */
		lsqf_ent_array_add_ent_string(array, nbuf);
		
		ent = lsqf_ent_new(LSQF_ENT_TYPE_TAG);
		ent->tag.id = tagid;
		ent->tag.pad = pad;
		ent->tag.countArray = countArray;
		ent->tag.iterate = iterate;
		ent->tag.outfmtfnid = get_outfmtfnid_by_name(outfmtfn);

		lsqf_ent_array_add_ent(array, ent);
		
		fmt = p;
		
		break;
	    }
	    case '[':
		fmt++;
		
		lsqf_ent_array_add_ent_string(array, nbuf);
		
		ent = lsqf_ent_new(LSQF_ENT_TYPE_ARRAY);		
		lsqf_ent_array_add_ent(array, ent);
		
		if (do_parse(ent->array, fmt, &end, LSQF_PARSE_ARRAY)) {
		    error = 1;
		    break;
		}
		
		if (*end == '\0') {
		    logn(LOGERR, _("%s missing ] at end of array"), invalid_format);
		    error = 1;
		    break;
		}
		
		fmt = end;
		
		break;

	    case ']':
		if (mode != LSQF_PARSE_ARRAY) {
		    logn(LOGERR, _("%s unexpected ]"), invalid_format);
		    error = 1;
		    break;
		}

		/* found end of array -> stop parsing */
		done = 1;
		
		/* save address of the last character we parsed */
		*endfmt = fmt;
		
		break;

	    case '}':
		logn(LOGERR, _("%s unexpected }"), invalid_format);
		error = 1;
		break;
	
	    default:
		if (fmt[0] == '\\' && fmt[1] != '\0') {
		    fmt++;

		    n_buf_putc(nbuf, get_escaped_char(*fmt));
		
		} else {
		    n_buf_putc(nbuf, *fmt);
		}	
	}
		
	fmt++;
    }

    if (error) {
	n_buf_free(nbuf);
	return 1;
    }
    
    lsqf_ent_array_add_ent_string(array, nbuf);

    if (!done && endfmt)
	*endfmt = fmt;

    n_buf_free(nbuf);

    return 0;
}

/**
 * lsqf_parse:
 *
 * Returns: On success, pointer to structure which is a base to display requested information or NULL when parsing failed.
 **/
struct lsqf_ent_array *lsqf_parse(char *fmt)
{
    struct lsqf_ent_array *array = NULL;

    if ((array = lsqf_ent_array_new())) {
	if (do_parse(array, fmt, NULL, LSQF_PARSE_NORMAL)) {
	    lsqf_ent_array_free(array);
	    array = NULL;
	}
    }
    
    return array;
}

static int get_tag_array_size(const struct lsqf_ent *ent, struct lsqf_pkgdata *pkgdata)
{
    const struct pkg *pkg = pkgdata->pkg;
    unsigned int i;
    int size = 1;

    n_assert(ent->type == LSQF_ENT_TYPE_TAG);

    if (lsqf_tags[ent->tag.id].is_array) {
	size = 0;
	
	if (lsqf_tags[ent->tag.id].need_flist) {
	    struct pkgflist *flist = lsqf_pkgdata_flist(pkgdata);
	    
	    if (flist) {
		switch (ent->tag.id) {
		    case LSQF_TAG_BASENAMES:
		    case LSQF_TAG_FILELINKTOS:
		    case LSQF_TAG_FILEMODES:
		    case LSQF_TAG_FILENAMES:
		    case LSQF_TAG_FILESIZES:
			for (i = 0; i < n_tuple_size(flist->fl); i++) {
			    struct pkgfl_ent *flent = n_tuple_nth(flist->fl, i);
		    
			    size += flent->items;
			}
		
			break;

	    	    case LSQF_TAG_DIRNAMES:
		        size = n_tuple_size(flist->fl);
			break;
		
		    default:
			n_assert(0);
		}
	    }
	} else {
	    switch (ent->tag.id) {
		case LSQF_TAG_CONFLICTFLAGS:
		case LSQF_TAG_CONFLICTS:
		case LSQF_TAG_CONFLICTVERSION:
		case LSQF_TAG_OBSOLETEFLAGS:
		case LSQF_TAG_OBSOLETES:
		case LSQF_TAG_OBSOLETEVERSION:
		    if (pkg->cnfls) {
			int nobsl = 0, ncnfls = 0;
		
			for (i = 0; i < n_array_size(pkg->cnfls); i++) {
			    struct capreq *cr = n_array_nth(pkg->cnfls, i);
			
			    if (capreq_is_obsl(cr))
				nobsl++;
			    else
				ncnfls++;
			}
		    
			if (ent->tag.id == LSQF_TAG_CONFLICTFLAGS || ent->tag.id == LSQF_TAG_CONFLICTS
			 || ent->tag.id == LSQF_TAG_CONFLICTVERSION)
			    size = ncnfls;
			else
			    size = nobsl;
		    }

		    break;
	
		case LSQF_TAG_PROVIDEFLAGS:
		case LSQF_TAG_PROVIDES:
		case LSQF_TAG_PROVIDEVERSION:
		    if (pkg->caps)
			size = n_array_size(pkg->caps);
		    break;
	
		case LSQF_TAG_REQUIREFLAGS:
		case LSQF_TAG_REQUIRES:
		case LSQF_TAG_REQUIREVERSION:
		    if (pkg->reqs)
			size = n_array_size(pkg->reqs);
		    break;

		case LSQF_TAG_SUGGESTSFLAGS:
		case LSQF_TAG_SUGGESTS:
		case LSQF_TAG_SUGGESTSVERSION:
		    if (pkg->sugs)
			size = n_array_size(pkg->sugs);
		    break;

		default:
		    n_assert(0);
	    }
	}
    }
    
    return size;
}

/**
 * Returns 1 when arrays size differ.
 **/
static int check_size(const struct lsqf_ent_array *array, struct lsqf_pkgdata *pkgdata, unsigned int *s)
{
    unsigned int i;
    int size = 1, prev_size = -1;

    for (i = 0; i < array->items; i++) {
	struct lsqf_ent *ent = array->ents[i];
	
	if (ent->type == LSQF_ENT_TYPE_TAG) {
	    if (lsqf_tags[ent->tag.id].is_array) {
		size = get_tag_array_size(ent, pkgdata);
	    } else {
		/* check whether we want to print this tag with every iteration */
		if (ent->tag.iterate)
		    continue;

		size = 1;
	    }

	    if (prev_size < 0)
		prev_size = size;

	    if (prev_size != size)
		return 1;
	}
    }
    
    *s = size;
    
    return 0;
}

static void add_tagstr_to_nbuf(tn_buf *nbuf, const struct lsqf_ent *ent, struct lsqf_pkgdata *pkgdata, unsigned int num)
{
    char *str = NULL, fmt[16];

    if (ent->tag.countArray) {
	n_snprintf(fmt, sizeof(fmt), "%%%dd", ent->tag.pad);
	n_buf_printf(nbuf, fmt, get_tag_array_size(ent, pkgdata));

    } else if ((str = get_str_by_tagid(ent, pkgdata, num))) {
	n_snprintf(fmt, sizeof(fmt), "%%%ds", ent->tag.pad);
	n_buf_printf(nbuf, fmt, str);

	n_free(str);
    }
}

/**
 * tags_size - number of items in tags. It's mostly used by tag-arrays (for example REQUIRES)
 */
static int ent_array_to_string(const struct lsqf_ent_array *array,
				 struct lsqf_pkgdata *pkgdata,
				 tn_buf *nbuf, int tags_size)
{
    unsigned int i, j, size = 0;
    
    for (j = 0; j < tags_size; j++) {
	for (i = 0; i < array->items; i++) {
	    struct lsqf_ent *ent = array->ents[i];
	    int ret = 1;
	
	    switch (ent->type) {
		case LSQF_ENT_TYPE_TAG:
		    add_tagstr_to_nbuf(nbuf, ent, pkgdata, j);
		    break;

		case LSQF_ENT_TYPE_STRING:
		    n_buf_puts_z(nbuf, ent->string);
		    break;

		case LSQF_ENT_TYPE_ARRAY:
		    if (check_size(ent->array, pkgdata, &size)) {
			logn(LOGERR, _("%s array iterator used with different sized arrays"), invalid_format);
			ret = 0;
		    } else {
			ret = ent_array_to_string(ent->array, pkgdata, nbuf, size);
		    }
		    
		    break;

		default:
		    n_assert(0);
	    }
	    
	    /* break on error */
	    if (ret == 0)
		return 0;
	}
    }
    
    return 1;
}

char *lsqf_to_string(const struct lsqf_ent_array *array, const struct pkg *pkg)
{
    struct lsqf_pkgdata *pkgdata = NULL;
    tn_buf		*nbuf = NULL;
    char		*buf = NULL;
    
    pkgdata = lsqf_pkgdata_new(pkg);
    nbuf = n_buf_new(64);

    /* In the first array there can't be more than one item per tag,
     * so force tags_size = 1 */
    if (ent_array_to_string(array, pkgdata, nbuf, 1)) {
	buf = n_strdup(n_buf_ptr(nbuf));
    }

    n_buf_free(nbuf);
    lsqf_pkgdata_free(pkgdata);
    
    return buf;
}

/**
 * lsqf_show_querytags:
 *
 * Print all supported tags.
 */
void lsqf_show_querytags(struct cmdctx *cmdctx)
{
    int i, j;
    
    for (i = 0; i < LSQF_N_TAGS; i++) {
	for (j = 0; lsqf_tags[i].tagname[j]; j++) {
	    cmdctx_printf(cmdctx, "%s\n", lsqf_tags[i].tagname[j]);
	}
    }
}
