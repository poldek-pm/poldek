/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

static const char pndir_packages_idx[]    = "packages.dir.tn";
static const char pndir_extension[]       = "ndir";
static const char pndir_desc_suffix[]     = ".dscr";
static const char pndir_difftoc_suffix[]  = ".diff.toc";
static const char pndir_packages_incdir[] = "packages.i";

static const char pndir_poldeksindex[] = "poldeks-pndir";

static const char pndir_tag_hdr[] = "%__h_hdr";
static const char pndir_tag_depdirs[] = "%__h_depdirs";
static const char pndir_tag_langs[] = "%__h_langs";
static const char pndir_tag_ts[] = "%__h_ts";
static const char pndir_tag_ts_orig[] = "%__h_ts_orig";
static const char pndir_tag_opt[] = "%__h_opt";
static const char pndir_tag_removed[] = "%__h_removed";
static const char pndir_tag_pkgroups[] = "%__h_groups";
static const char pndir_tag_endhdr[] = "%__h_end";

#define hdr_eq(key, hdr) strncmp(key, hdr, sizeof(hdr)-1) == 0
