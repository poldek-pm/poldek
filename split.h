/* $Id$ */
#ifndef POLDEK_SPLIT_H
#define POLDEK_SPLIT_H

int packages_set_priorities(tn_array *pkgs, const char *splitconf_path);

int packages_split(tn_array *pkgs, unsigned split_size,
                   unsigned first_free_space, const char *outprefix);

#endif
