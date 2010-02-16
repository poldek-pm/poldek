#ifndef POCLIDEK_LS_QUERYFMT_H
#define POCLIDEK_LS_QUERYFMT_H

#include "cmd.h"
#include "pkg.h"

struct lsqf_ent;

struct lsqf_ent_array {
    struct lsqf_ent **ents;
    unsigned int      items;
};

struct lsqf_ent {
    enum {
	LSQF_ENT_TYPE_TAG = 1,
	LSQF_ENT_TYPE_STRING,
	LSQF_ENT_TYPE_ARRAY
    } type;
    
    union {
	struct {
	    int id;

	    int iterate;
	    int countArray;
	    int pad;
	    int outfmtfnid;
	} tag;
	
	char *string;
	
	struct lsqf_ent_array *array;
    };
};



struct lsqf_ent_array *lsqf_parse(char *fmt);
char                  *lsqf_to_string(const struct lsqf_ent_array *array, const struct pkg *pkg);

struct lsqf_ent_array *lsqf_ent_array_new(void);
void                   lsqf_ent_array_free(struct lsqf_ent_array *array);

void                   lsqf_show_querytags(struct cmdctx *cmdctx);

#endif /* POCLIDEK_LS_QUERYFMT_H */
