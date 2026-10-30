## 0.46 (TBD)
### Features
* New `signkey` per-source config option and `--nosignature` install option for per-source GPG signature verification; `signkey` automatically enables signature verification unless `signed = no` is set (rpm.org backend)
* Public headers now match what the libraries really provide: two functions declared there were missing, and declarations of functions and flags that no library defines are gone (@arekm [#58](https://github.com/poldek-pm/poldek/pull/58), [#59](https://github.com/poldek-pm/poldek/pull/59), [#62](https://github.com/poldek-pm/poldek/pull/62))
* Boolean (`and`/`or`/`with`/`without`) dependencies are re-evaluated when installing/upgrading, including against installed packages and unlisted paths, so upgrades no longer break such requirements silently (@arekm [#45](https://github.com/poldek-pm/poldek/pull/45), [#47](https://github.com/poldek-pm/poldek/pull/47))
* Repo (source) `ignore` config option is merged with the global one instead of overwriting it - poldek-global-ignore-merges.patch (https://github.com/pld-linux/poldek/commit/1d3b08124897ca8e21a70944d5eb3291cc16a635)
* depsolver: break ties between equivalent providers by preferring highest EVR - poldek-scoring-evr.patch (https://github.com/pld-linux/poldek/commit/3c9d98c7c3b231827a10714d48ccf0f894a9a93f)
* Show config file origin when removing duplicated sources - poldek-dup-sources.patch (https://github.com/pld-linux/poldek/commit/d1adbc7551110e290d82895d4a88bb8b93e975c0)
* multilib: `install` with a bare package name resolves to the native arch only, instead of pulling in a foreign-arch dependency chain; globs (`install foo*`) and explicit names (`install foo-1.0.0-1.i686`) still match all arches, and a package available only for a foreign arch must be named explicitly - poldek-multilib-bare-name-install.patch (https://github.com/pld-linux/poldek/commit/dcf566fbc923ff4bc085c2d9981dfeda98862bdf)
* multilib: do not treat uncolored packages (e.g. `-devel`) from different arch families as interchangeable - poldek-nocolor-cmp.patch (https://github.com/pld-linux/poldek/commit/d0226c1cb0de9b8f759d6890562e8ff7bea23ae1)
* Improved zstd to gzip conversion for metadata repos
* `search --perlre` uses PCRE2 instead of the unmaintained PCRE 8.x, which e.g. RHEL 10 does not ship (@arekm [#106](https://github.com/poldek-pm/poldek/pull/106))
* rpm.org 6.0 or later is now required; the rpm5 backend and the module for rpm.org before 4.14 are gone (@arekm [#100](https://github.com/poldek-pm/poldek/pull/100))
* Security hardening:
  - enable SSL/TLS certificate verification for HTTPS connections (use `--no-ssl-verify` to disable it; insecure)
  - block HTTPS to HTTP downgrade on redirects
  - validate environment variable paths (HOME, TMPDIR, XDG_CACHE_HOME)
  - zero HTTP auth credentials before freeing memory
  - call `initgroups()` when dropping privileges

### Bug fixes
* depsolver fixes (@arekm [#35](https://github.com/poldek-pm/poldek/pull/35), [#38](https://github.com/poldek-pm/poldek/pull/38), [#41](https://github.com/poldek-pm/poldek/pull/41), [#46](https://github.com/poldek-pm/poldek/pull/46), [#47](https://github.com/poldek-pm/poldek/pull/47), [#90](https://github.com/poldek-pm/poldek/pull/90), [#95](https://github.com/poldek-pm/poldek/pull/95), [#96](https://github.com/poldek-pm/poldek/pull/96), [#97](https://github.com/poldek-pm/poldek/pull/97))
* Fedora / rpm 6 fixes (@arekm [#98](https://github.com/poldek-pm/poldek/pull/98), [#99](https://github.com/poldek-pm/poldek/pull/99), [#101](https://github.com/poldek-pm/poldek/pull/101), [#102](https://github.com/poldek-pm/poldek/pull/102), [#103](https://github.com/poldek-pm/poldek/pull/103))
* Fix: `search --perlre` with a pattern that does not compile printed the error but exited with success; it now fails (@arekm [#105](https://github.com/poldek-pm/poldek/pull/105))
* Fix: the `--enable-strict` build failed with gcc 16 (@arekm [#105](https://github.com/poldek-pm/poldek/pull/105))
* Fix: `poldek -e` listed a package twice when two arguments named it (@arekm [#93](https://github.com/poldek-pm/poldek/pull/93))
* Fix: shell `uninstall name` with several installed versions failed with "ambiguous name"; it now removes all of them (@arekm [#92](https://github.com/poldek-pm/poldek/pull/92))
* Fix: multilib mode ignored masks without an arch, so `install a-1-1` and `ls a-1-1` found nothing and `--pset` defaults were taken from a foreign arch (@arekm [#91](https://github.com/poldek-pm/poldek/pull/91))
* Fix: metadata repositories with SHA-1 checksums did not load (@arekm [#89](https://github.com/poldek-pm/poldek/pull/89))
* Fix: `--upa` of a changed pndir repository said it was up to date after fetching the whole index (@arekm [#88](https://github.com/poldek-pm/poldek/pull/88))
* Fix: `--up`/`--upa` of a metadata repository reported success and exited with 0 when its new index could not be fetched (@arekm [#87](https://github.com/poldek-pm/poldek/pull/87))
* Fix: `--destination` copied a package for a foreign arch; an unreadable rpm configuration left `--mkidx` writing world-writable index files (@arekm [#86](https://github.com/poldek-pm/poldek/pull/86))
* Fix: after "Index patches size too big", HTTP 404s counted as downloads and poldek printed nothing even with `-vv` (@arekm [#81](https://github.com/poldek-pm/poldek/pull/81))
* Fix: `--install-dist` with `--destination` is now refused instead of trying to install through `pset` (@arekm [#85](https://github.com/poldek-pm/poldek/pull/85))
* Fix: output from hooks, `cp` and `rpm` is logged line by line (@arekm [#84](https://github.com/poldek-pm/poldek/pull/84))
* Fix: `--fetch` no longer leaves packages that fail the digest check in the destination directory (@arekm [#83](https://github.com/poldek-pm/poldek/pull/83))
* Fix: after `--up` fetches a whole index, `ls` no longer lists packages the repository no longer has (@arekm [#82](https://github.com/poldek-pm/poldek/pull/82))
* Fix: removing a package could keep one that requires its directory when `auto_directory_dependencies` is enabled (@arekm [#80](https://github.com/poldek-pm/poldek/pull/80))
* Fix: `--up` could crash while sizing the index patches (@arekm [#79](https://github.com/poldek-pm/poldek/pull/79))
* Fix: a directory read error was taken for its end; it is now reported instead of silently using partial results (@arekm [#78](https://github.com/poldek-pm/poldek/pull/78))
* `desc -L` and `ls -S` no longer show the installed package's own changelog entries as new, so only fixes made after its build day are listed (@arekm [#68](https://github.com/poldek-pm/poldek/pull/68))
* Files downloaded over HTTP or FTP are stored with the server's modification time regardless of the local time zone (@arekm [#67](https://github.com/poldek-pm/poldek/pull/67))
* An HTTP download cut short is reported as an error, and its incomplete file is no longer left in the cache (@arekm [#66](https://github.com/poldek-pm/poldek/pull/66))
* A broken cached repo index is fetched again instead of leaving the repository without packages (@arekm [#65](https://github.com/poldek-pm/poldek/pull/65))
* The cache of installed packages is no longer used when the database changed while poldek was running, and a list that mixes packages from before and after such a change is no longer saved (@arekm [#63](https://github.com/poldek-pm/poldek/pull/63))
  - the first run after an upgrade rebuilds that cache; later runs are unaffected
* An old-style `source = name,options path` line keeps its `pri`, `gpg`/`pgp` and `compr` settings (@arekm [#61](https://github.com/poldek-pm/poldek/pull/61))
* A source declared `gpg` or `pgp` on such a line now has its packages' signatures verified before installation: an unsigned package from it stops the installation, or makes poldek ask whether to proceed (@arekm [#61](https://github.com/poldek-pm/poldek/pull/61))
* `poldek -l` no longer crashes on a source with `compr` set (@arekm [#60](https://github.com/poldek-pm/poldek/pull/60))
* `--verify=file-conflicts` reports every differing file of a pair of packages, including the last copy of a name (@arekm [#57](https://github.com/poldek-pm/poldek/pull/57))
* A package with an unusable header or a truncated file list no longer aborts poldek when loading a dir index (@arekm [#56](https://github.com/poldek-pm/poldek/pull/56))
* poldek no longer aborts on rpm v6 packages or on packages with files of 4 GiB or more; disk space and download totals no longer wrap around on 32-bit systems (@arekm [#55](https://github.com/poldek-pm/poldek/pull/55))
* Sizes of 2 GiB and more are no longer printed as negative numbers (@arekm [#53](https://github.com/poldek-pm/poldek/pull/53))
* The dir index is no longer rebuilt for every package on 32-bit systems; URL password masking and very long repository paths no longer break; error messages with arguments are no longer garbled on exit (@arekm [#52](https://github.com/poldek-pm/poldek/pull/52))
* Build fixes for newer toolchains that fail with `-Werror` (@arekm [#51](https://github.com/poldek-pm/poldek/pull/51))
* Paths, URLs and package file names containing `%` are handled correctly (@arekm [#50](https://github.com/poldek-pm/poldek/pull/50))
* `ls --qf` and `desc -L` no longer crash on packages whose metadata is unavailable (e.g. indexes built with `--mo=nodesc`) (@arekm [#49](https://github.com/poldek-pm/poldek/pull/49))
* Reading the installed database cache under `--root` now shows summaries and file lists, and no longer leaks memory (@arekm [#48](https://github.com/poldek-pm/poldek/pull/48))
* `desc -B` shows only packages that actually need the version being described, instead of every package sharing its name (@arekm [#44](https://github.com/poldek-pm/poldek/pull/44))
* `desc -R` / `desc -B` while in `/installed` now searches only installed packages, not the available ones (@arekm [#43](https://github.com/poldek-pm/poldek/pull/43))
* Required-packages listing (`desc -R`) no longer changes its result depending on the order packages were queried in (@arekm [#42](https://github.com/poldek-pm/poldek/pull/42))
* Refreshing a package index no longer keeps a broken partially-built index or prints stale package descriptions afterwards (@arekm [#40](https://github.com/poldek-pm/poldek/pull/40))
* Suggested packages: the 'r' key repeats the current round of suggested-package questions instead of skipping them (@arekm [#39](https://github.com/poldek-pm/poldek/pull/39))
* Reconnect silently when a keep-alive connection goes stale; do not die when a TLS connection is closed without close_notify; drop the HTTP keep-alive liveness probe (@arekm [#37](https://github.com/poldek-pm/poldek/pull/37))
* `poldek --help` no longer loops or crashes on a narrow terminal; `$COLUMNS`/`$LINES` are used as a fallback when the terminal size is unknown (@arekm [#36](https://github.com/poldek-pm/poldek/pull/36))
* Fixed 'Q' in the suggested-packages question to abort the transaction (lp#1532694) (@arekm [#34](https://github.com/poldek-pm/poldek/pull/34))
* Fixed `--help` output so the "Report bugs to ..." line is shown again; rpm install notices are filtered from the output again (@arekm [#33](https://github.com/poldek-pm/poldek/pull/33))
* Fixed SIGSEGV with --noconf (@arekm [#32](https://github.com/poldek-pm/poldek/pull/32))
* Fix: an explicit port in a repository URL is kept across redirects and when a proxy is used; dead FTP connections are no longer reused; unreadable FTP responses are reported as errors - poldek-url-keep-port.patch, poldek-vcn-pool-reuse.patch, poldek-vfstat-redirect-crash.patch, poldek-ftp-pasv-crash.patch, poldek-ftp-pasv-parse.patch (https://github.com/pld-linux/poldek/commit/59e3fec55abec6a69632766852c42ac36aec064b)
* Fix path traversal detection (trailing `/..`); fix metadata handling with libxml2

## 0.45 (2025/12/16)
* Fixed issue where poldek doesn't try to install other packages from cmdline if one is going to be skipped ([#27](https://github.com/poldek-pm/poldek/pull/27))
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
* New option --color forces coloured output ([#21](https://github.com/poldek-pm/poldek/pull/21))
* Coloured and compact output of `llu` (`ls -lu`); old 4-column view is available with `ls -llu`
* Fixed depsolver crash (issues [#15](https://github.com/poldek-pm/poldek/pull/15), [#24](https://github.com/poldek-pm/poldek/pull/24))
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
