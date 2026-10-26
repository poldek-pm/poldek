## 0.46 (TBD)
* New `signkey` per-source config option: URL of the GPG key that packages of
  the source are signed with; at install time the key is checked in rpm's
  keyring, fetched and imported only if missing and after confirmation in
  interactive mode, and packages from the source are verified against that
  source's key only, when signature verification is enabled (`signed = yes`)
  for the source. Configuring `signkey` automatically enables signature
  verification unless `signed = no` is explicitly set (rpm.org backend)
* New `--nosignature` install option to skip package signature verification,
  equivalent to `rpm --nosignature` (rpm.org backend)
* Security hardening:
  - enable SSL/TLS certificate verification for HTTPS connections
  - block HTTPS to HTTP downgrade on redirects
  - validate environment variable paths (HOME, TMPDIR, XDG_CACHE_HOME)
  - zero HTTP auth credentials before freeing memory
  - call `initgroups()` when dropping privileges
* Bug fixes:
  - fix path traversal detection (trailing `/..`)
  - use `memmove()` for overlapping memory regions
  - fix metadata handling with libxml2 (`XML_PARSE_UNZIP` flag)
* Improved zstd to gzip conversion for metadata repos
* multilib: do not treat uncolored packages (e.g. `-devel`) from different arch families as interchangeable - poldek-nocolor-cmp.patch (https://github.com/pld-linux/poldek/commit/d0226c1cb0de9b8f759d6890562e8ff7bea23ae1)
* multilib: `install` with a bare package name resolves to the native arch only, instead of pulling in a foreign-arch dependency chain; globs (`install foo*`) and explicit names (`install foo-1.0.0-1.i686`) still match all arches, and a package available only for a foreign arch must be named explicitly - poldek-multilib-bare-name-install.patch (https://github.com/pld-linux/poldek/commit/dcf566fbc923ff4bc085c2d9981dfeda98862bdf)
* Show config file origin when removing duplicated sources - poldek-dup-sources.patch (https://github.com/pld-linux/poldek/commit/d1adbc7551110e290d82895d4a88bb8b93e975c0)
* depsolver: break ties between equivalent providers by preferring highest EVR - poldek-scoring-evr.patch (https://github.com/pld-linux/poldek/commit/3c9d98c7c3b231827a10714d48ccf0f894a9a93f)
* Repo (source) `ignore` config option is merged with the global one instead of overwriting it - poldek-global-ignore-merges.patch (https://github.com/pld-linux/poldek/commit/1d3b08124897ca8e21a70944d5eb3291cc16a635)
* Fix: unreadable FTP PASV response is reported as an error and a non-conforming reply is printed verbatim - poldek-ftp-pasv-crash.patch (https://github.com/pld-linux/poldek/commit/59e3fec55abec6a69632766852c42ac36aec064b)
* Fix: digit-less FTP PASV reply is reported as a parse error - poldek-ftp-pasv-parse.patch (https://github.com/pld-linux/poldek/commit/59e3fec55abec6a69632766852c42ac36aec064b)
* Fix: a crash in vf_stat on a failed protocol-changing redirect - poldek-vfstat-redirect-crash.patch (https://github.com/pld-linux/poldek/commit/59e3fec55abec6a69632766852c42ac36aec064b)
* Fix: dead and desynchronized FTP connections are no longer reused from the connection pool - poldek-vcn-pool-reuse.patch (https://github.com/pld-linux/poldek/commit/59e3fec55abec6a69632766852c42ac36aec064b)
* Fix: an explicit port in a repository URL is kept across redirects and when a proxy is used - poldek-url-keep-port.patch (https://github.com/pld-linux/poldek/commit/59e3fec55abec6a69632766852c42ac36aec064b)
* Fixed SIGSEGV with --noconf (@arekm #32)
* Fixed `--help` output so the "Report bugs to ..." line is shown again (@arekm #33)
* Filter rpm install notices (e.g. `<pkg>.rpm: digests signatures OK`) from the output again (@arekm #33)
* Fixed 'Q' in the suggested-packages question to abort the transaction (lp#1532694) (@arekm #34)
* Fixed a crash on a versioned conflict with an installed package absent from all repositories (@arekm #35)
* Fixed `poldek --help` looping or crashing on a narrow terminal (@arekm #36)
* Use $COLUMNS/$LINES as a fallback when the terminal size is unknown (@arekm #36)
* Reconnect silently when a pooled keep-alive connection goes stale (@arekm #37)
* Avoid SIGPIPE from TLS connections closed without close_notify (@arekm #37)
* Drop the HTTP keep-alive liveness HEAD probe (@arekm #37)
* Boolean dependencies: with/without now consider installed packages (@arekm #38)
* Fix evaluation of OR alternatives and unsatisfiable conjuncts inside with/without (@arekm #38)
* Suggested packages: the 'r' key repeats the current round of suggested-package questions instead of skipping them (@arekm #39)
* Refreshing a package index no longer keeps a broken partially-built index or prints stale package descriptions afterwards (@arekm #40)
* Required-packages listing (`desc -R`) no longer changes its result depending on the order packages were queried in (@arekm #42)
* `desc -R` / `desc -B` while in `/installed` now searches only installed packages (including file owners of required paths), not the available ones (@arekm #43)
* `desc -B` shows only packages that actually need the version being described, instead of every package sharing its name (@arekm #44)
* Boolean (`and`/`or`/`with`/`without`) dependencies are re-evaluated when installing/upgrading, including against installed packages and unlisted paths, so upgrades no longer break such requirements silently (@arekm #45, #47)
* `uninstall --greedy` removes only the packages that really satisfy the dependency, following versioned and file requirements as well as boolean ones, and follows what a removed package actually needs (@arekm #41, #46)
* Upgrading or downgrading a package (and `upgrade-dist`) keeps installed packages that depend on boolean conditions or newly added/removed capabilities from breaking or being dropped unnecessarily (@arekm #47)
* Reading the installed database cache under `--root` now shows summaries and file lists, and no longer leaks memory when a package record is gone (@arekm #48)
* `ls --qf` and `desc -L` no longer crash on packages whose metadata is unavailable (e.g. indexes built with `--mo=nodesc`) (@arekm #49)
* Repository paths, URLs and package file names containing `%` are no longer treated as printf conversions (@arekm #50)
* Fix: a pndir index kept in a directory whose name contains `%` is written and read correctly (@arekm #50)
* Build fixes for newer toolchains, where C23 string functions made poldek emit `discarded-qualifiers` warnings and fail with `-Werror` (@arekm #51)
* Fix: password masking of a repository URL read past the login and the host part, exposing credentials of a malformed URL (@arekm #52)
* Fix: a repository path close to `PATH_MAX` no longer breaks its pndir index (@arekm #52)
* Fix: `die` messages with arguments were printed garbled on exit (@arekm #52)
* The dir index is no longer rebuilt for every package on 32-bit systems with a 64-bit `time_t` (@arekm #52)
* Fix: sizes of 2 GiB and more were printed as negative numbers (@arekm #53)
* Fix: poldek aborted on rpm v6 packages and on packages with files of 4 GiB or more (@arekm #55)
* Fix: disk space and download totals wrapped around on 32-bit systems (@arekm #55)
* Fix: a package with an unusable header, a truncated file tag or an out-of-range `DIRINDEXES` entry no longer aborts poldek or reads past its arrays when loading a dir index (@arekm #56)
* `--verify=file-conflicts` now reports every differing file of a pair of packages, files in directories that were not looked up, and the last copy of a name; it honors the packages given on the command line and prints top-level paths without a doubled slash (@arekm #57)
* `poldek -l` no longer crashes on a source with `compr` set (@arekm #60)
* An old-style `source = name,options path` line keeps its `pri`, `gpg`/`pgp` and the value of `compr`; before, `compr` was turned into `yes` and poldek looked for `packages.ndir.yes.md` (@arekm #61)
* A source declared `gpg` or `pgp` on such a line now has its packages' signatures verified before installation, as asked for: an unsigned package from it stops the installation, or makes poldek ask whether to proceed (@arekm #61)
* The cache of installed packages is no longer used when the database changed while poldek was running, and a list that mixes packages from before and after such a change is no longer saved (@arekm #63)
  - the database is now recognized by its inode, modification time in nanoseconds, size and, where rpm provides it, a digest of its contents
  - the first run after an upgrade rebuilds that cache; later runs are unaffected
* Public headers now match what the libraries really provide: two functions declared there were missing, and declarations of functions and flags that no library defines are gone (@arekm #58, #59, #62)
* A broken cached repo index are fetched again instead of leaving the repository without packages (@arekm #65)
* An HTTP download cut short is reported as an error, and its incomplete file is no longer left in the cache (@arekm #66)
* Files downloaded over HTTP or FTP are stored with the server's modification time regardless of the local time zone (@arekm #67)
* `desc -L` and `ls -S` no longer show the installed package's own changelog entries as new, so only fixes made after its build day are listed (@arekm #68)

## 0.45 (2025/12/16)
* Fixed issue where poldek doesn't try to install other packages from cmdline if one is going to be skipped (#27)
* Fixed compatibility with gettext >= 0.24 by @jpalus in https://github.com/poldek-pm/poldek/pull/26
* Fixed skipping requirements which are both pre and preun by @jpalus in https://github.com/poldek-pm/poldek/pull/25
* Added support for direct use of metadata `.repo` files (source type auto-detected from URL). e.g.:
  ```shell
      $ poldek -s https://example.com/example.repo
  ```
* Handle metadata (aka dnf) compressed with ztsd
* Faster index loading in threads (lock-free memory alocation, atomic cache, etc)
* Improved deps indexing speed (timsort) and memory usage (deduplication mem allocator)
* Fixed handling of trailing slash in paths
* Fixed required packages caching when unmet deps are needed (install dist case)
* Fixed several memleaks and buffer/ stack overflows
* Fixed `--verify` crashes
* Fix: `-q` always quiet regardless `-v`/`-q` order
* Restored `desc -B`
* Restored the ability to `--verify all` packages
* Fixed checking http connection with `HEAD /` when proxied
* Fixed SIGSEV on reinstall

## 0.44.0 (2025/02/27)
* Interactive mode (*shell*) commands can be invoked directly from command line, like:
  ```shell
      $ poldek llu
      $ poldek install foo
      # etc
  ```
  Some commands get aliased with to mimic apt/dnf/etc:
  - `install` => `add`
  - `uninstall` => `remove`
  - `desc`      => `info`

  Means `$ poldek install` and `$ poldek add` do the same.
  Two new commands has been added `up` (works like `--up` switch) and `clean` (`--clean`).

  See `poldek help` and `poldek <command> --help` for details.
* Simplified available *repo* list display
* Tilde (`~`) at the end of package name is treated as a asterisk wildcard (`*`), e.g:
  ```shell
   $ poldek install foo-~
  ```
* Boolean dependency support (at least tested cases - see tests/sh/10-booldeps)
* New option --color forces coloured output (#21)
* Coloured and compact output of `llu` (`ls -lu`); old 4-column view is available with `ls -llu`
* Fixed depsolver crash (issues #15, #24)
* Fixed depsolver conflict resolution (tests/sh/07-depsolver:testUpgradeMultipleByConflict)
* Reduced startup time:
    - lazy dep indexing
    - repos loading in threads (can be switched off by `-Ouse_threads=n`)
    - zlib-ng is used by default for (de)compressing
    - `install` arguments validation before repo load

* Merged most PLD patches: ‎
  - poldek-info.patch (https://github.com/pld-linux/poldek/commit/5e4ef52a61a7ebbf59a4f8c2d8254376514f1f49)
  - poldek-pc.patch (https://github.com/pld-linux/poldek/commit/b99b1fbaafcd141db7744fc9f85bbfdce0a7137c)
  - rpm4-no-dir-deps.patch (https://github.com/pld-linux/poldek/commit/3e585669810a6f61ce6797b7a4b52eaf15c2f02f)
  - rpm-4.18.patch (https://github.com/pld-linux/poldek/commit/887913553bb27d6dc7121c16335007ced4dccde2)
  - gcc14.patch (https://github.com/pld-linux/poldek/commit/862de2ef73b3753c63daaa965018aa79544c2600)
  - poldek-https-redirect.patch (https://github.com/pld-linux/poldek/commit/4c18fee28a08543b4ce144403efe0c7cbf1109df)
  - egrep-is-obsolete.patch (https://github.com/pld-linux/poldek/commit/99a84776b7eba2eef27cfcf1ff91f942f215382f)
  - more-trace.patch (https://github.com/pld-linux/poldek/commit/2d94f8f7056fbe16b90a171b66282cae45b9070b)
  - no-bdb-for-rpm-org.patch (https://github.com/pld-linux/poldek/commit/d37cdfea68f9fb65e2fbcdf2521e19e30aa60d76)
  - poldek-rsa_sig_rpmorg.patch (https://github.com/pld-linux/poldek/commit/d88c72b5ec4b6ad64ed02b79b658fb183829b641)
  - verify-signature.patch (https://github.com/pld-linux/poldek/commit/c685f627d1dae75f527f4a43245ea8acdbab7292)
  - skip-buildid-obsoletes.patch (https://github.com/pld-linux/poldek/commit/aba59b953581b330457d7e6f0ea3e1ab648f80a7)
  - rpm4-cpuinfo-deps.patch (https://github.com/pld-linux/poldek/commit/7f01416d06a257ce15e0ea468e836d4fdcd891ca)
  - sqlite-rpmdb.patch (https://github.com/pld-linux/poldek/commit/cafb63cb964647aba1f6c2de3a4bd5968439ddc5)
  - rpm4-uname-deps.patch (https://github.com/pld-linux/poldek/commit/c670d25c1a1541aad4f848759358ed0d11ddacaf)
  - db-index-format.patch (https://github.com/pld-linux/poldek/commit/c670d25c1a1541aad4f848759358ed0d11ddacaf)
  - rpm-4.15.patch (https://github.com/pld-linux/poldek/commit/b47be59439fe1031a342efc93d30171acac79fa3)

* rpmvercmp for rpm.org by @jpalus in https://github.com/poldek-pm/poldek/pull/16
* fix: cli/ls: sort entries just before listing (after filtering) by @jpalus in https://github.com/poldek-pm/poldek/pull/22

## previous versions

See [NEWS.old](doc/NEWS.old)
