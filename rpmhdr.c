/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include <rpm/rpmlib.h>
#include <trurl/nassert.h>

#include "rpmhdr.h"

int rpmhdr_ent_get(struct rpmhdr_ent *ent, Header h, int32_t tag)
{
    if (!headerGetEntry(h, tag, &ent->type, &ent->val, &ent->cnt)) {
        memset(ent, 0, sizeof(*ent));
        return 0;
    }
    
    return 1;
}

void rpmhdr_ent_free(struct rpmhdr_ent *ent)
{
    if (ent->type == RPM_STRING_ARRAY_TYPE
        || ent->type == RPM_I18NSTRING_TYPE) {
        n_assert(ent->val);
        free(ent->val);
        memset(ent, 0, sizeof(*ent));
    }
}

int rpmhdr_ent_cp(struct rpmhdr_ent *ent, Header h, int32_t tag, Header toh)
{
    if (!headerGetEntry(h, tag, &ent->type, &ent->val, &ent->cnt)) {
        memset(ent, 0, sizeof(*ent));
        return 0;
    }

    return headerAddEntry(toh, tag, ent->type, ent->val, ent->cnt);
}
