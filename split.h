/* $Id$ */
#ifndef POLDEK_SPLIT_H
#define POLDEK_SPLIT_H

int packages_split(tn_array *pkgs, unsigned split_size, unsigned first_free_space,
                   const char *splitconf_path, const char *outprefix);

#endif
