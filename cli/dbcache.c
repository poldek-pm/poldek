static int load_installed_packages(struct shell_s *sh_s, int reload) 
{
    struct pkgdir *pkgdir;
    
    if ((pkgdir = load_installed_pkgdir(reload)) == NULL)
        return 0;
    
    if (sh_s->dbpkgdir)
        pkgdir_free(sh_s->dbpkgdir);
    
    sh_s->dbpkgdir = pkgdir;
    sh_s->ts_instpkgs = pkgdir->ts;
    pkgs_to_pkgs(&sh_s->instpkgs, pkgdir->pkgs);

    return 1;
}


static time_t mtime(const char *pathname) 
{
    struct stat st;
    //printf("stat %s %d\n", pathname, stat(pathname, &st));
    if (stat(pathname, &st) != 0)
        return 0;

    return st.st_mtime;
}

char *mkdbcache_path(char *path, size_t size, const char *cachedir,
                     const char *dbfull_path)
{
    int len;
    char tmp[PATH_MAX], *p;
    
    n_assert(cachedir);
    if (*dbfull_path == '/')
        dbfull_path++;

    len = n_snprintf(tmp, sizeof(tmp), "%s", dbfull_path);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
        len--;
    }
    n_assert(len);
    p = tmp;

    while (*p) {
        if (*p == '/')
            *p = '.';
        p++;
    }

    snprintf(path, size, "%s/packages.dbcache.%s.gz", cachedir, tmp);
    return path;
}


static char *mkrpmdb_path(char *path, size_t size, const char *root,
                          const char *dbpath)
{
    *path = '\0';
    n_snprintf(path, size, "%s%s",
               *(root + 1) == '\0' ? "" : root,
               dbpath != NULL ? dbpath : "");
    return *path ? path : NULL;
}

#define RPMDBCACHE_PDIRTYPE "pndir"

static struct pkgdir *load_installed_pkgdir(int reload) 
{
    char            rpmdb_path[PATH_MAX], dbcache_path[PATH_MAX];
    const char      *lc_lang;
    struct pkgdir   *dir = NULL;
    int             ldflags = 0;

    
    if (mkrpmdb_path(rpmdb_path, sizeof(rpmdb_path), shell_s.inst->rootdir,
                     rpm_get_dbpath()) == NULL)
        return NULL;

    
    if (mkdbcache_path(dbcache_path, sizeof(dbcache_path),
                       shell_s.inst->cachedir, rpmdb_path) == NULL)
        return NULL;

    lc_lang = lc_messages_lang();
    if (lc_lang == NULL) 
        lc_lang = "C";
    
    if (!reload) {              /* use cache */
        time_t mtime_rpmdb, mtime_dbcache;
        mtime_dbcache = mtime(dbcache_path);
        mtime_rpmdb = rpm_dbmtime(rpmdb_path);
        if (mtime_rpmdb && mtime_dbcache && mtime_rpmdb < mtime_dbcache)
            dir = pkgdir_open_ext(dbcache_path, NULL, RPMDBCACHE_PDIRTYPE,
                                  "rpmdb", 0, lc_lang);
    }
    
    if (dir == NULL)
        dir = pkgdir_open_ext(rpmdb_path, NULL, "rpmdb", "rpmdb", 0, lc_lang);
    
    
    if (dir != NULL) {
        if (pkgdir_load(dir, NULL, PKGDIR_LD_NOUNIQ | ldflags)) {
            int n = n_array_size(dir->pkgs);
            msgn(2, ngettext("%d package loaded",
                             "%d packages loaded", n), n);
            
        } else {
            pkgdir_free(dir);
            dir = NULL;
        }
    }
    
    
    if (dir == NULL)
        logn(LOGERR, _("Load installed packages failed"));
    
    return dir;
}


static void save_installed_pkgdir(struct pkgdir *pkgdir) 
{
    time_t       mtime_rpmdb, mtime_dbcache;
    char         rpmdb_path[PATH_MAX], dbcache_path[PATH_MAX];
    const char   *path;


    if (mkrpmdb_path(rpmdb_path, sizeof(rpmdb_path), shell_s.inst->rootdir,
                     rpm_get_dbpath()) == NULL)
        return;

    mtime_rpmdb = rpm_dbmtime(rpmdb_path);
    if (mtime_rpmdb > pkgdir->ts) /* changed outside poldek */
        return;

    
    if (pkgdir_is_type(pkgdir, RPMDBCACHE_PDIRTYPE))
        path = pkgdir->idxpath;
    else 
        path = mkdbcache_path(dbcache_path, sizeof(dbcache_path),
                              shell_s.inst->cachedir, pkgdir->idxpath);

    if (path == NULL)
        return;
    
    if (mtime_rpmdb == pkgdir->ts) { /* not touched, check if cache exists  */
        mtime_dbcache = mtime(path);
        if (mtime_dbcache && mtime_dbcache >= pkgdir->ts)
            return;
    }
    
    //printf("path = %s, %d, %d, %d\n", path, 
    //       mtime_rpmdb, pkgdir->ts, mtime_dbcache);
    pkgdir_save(pkgdir, RPMDBCACHE_PDIRTYPE, path,
                PKGDIR_CREAT_NOPATCH | PKGDIR_CREAT_NOUNIQ |
                PKGDIR_CREAT_MINi18n);
}

static 
int cmd_reload(struct cmdarg *cmdarg,
               int argc, const char **argv, struct argp *argp)
{
    argp = argp;
    cmdarg = cmdarg;
    
    if (argv_is_help(argc, argv)) {
        printf(_("Just type \"reload\"\n"));
        return 1;
    }

    if (shell_s.instpkgs) {
        load_installed_packages(&shell_s, 1);
        
    } else {
        shell_s.instpkgs = n_array_new(1024, (tn_fn_free)pkg_free,
                                       (tn_fn_cmp)pkg_nvr_strcmp);
        
        load_installed_packages(&shell_s, 0);
    }

    return 1;
}
