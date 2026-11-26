## 0.46 (TBD)
### Changes
* New `repo` command: `repo add URL`, `repo ls` and `repo del URL` manage repositories kept in `repos.d` ([#145](https://github.com/poldek-pm/poldek/pull/145))
* A source name defined twice is an error that names both places ([#145](https://github.com/poldek-pm/poldek/pull/145))
* `repo add` imports dnf `.repo` files; `-s foo.repo` is no longer read as an index ([#145](https://github.com/poldek-pm/poldek/pull/145))
* A configuration is no longer read from a URL (`%include` of a remote file, `--conf URL`, `--upconf`); use `repo add` ([#150](https://github.com/poldek-pm/poldek/pull/150))
* `%include` of a directory includes its `*.conf` files, as `%includedir` does ([#152](https://github.com/poldek-pm/poldek/pull/152))
* `--clean` deletes the whole cache; `-s`/`-n` no longer narrow it ([#169](https://github.com/poldek-pm/poldek/pull/169))
* Hidden legacy options are gone, e.g. `-f` (use `--skip-installed`), `--rpmcmd` (`--pmcmd`), `--uninstall` (`--erase`), `--nodesc` (`--mo=nodesc`) ([#169](https://github.com/poldek-pm/poldek/pull/169))
* `cdrom://` sources and the `vfjuggle` fetcher are gone ([#170](https://github.com/poldek-pm/poldek/pull/170))
* apt-rpm `sources.list` is no longer loaded; `load apt sources list` is obsolete ([#149](https://github.com/poldek-pm/poldek/pull/149))
* The `auto zlib in rpm` option is obsolete and does nothing ([#169](https://github.com/poldek-pm/poldek/pull/169))
* A deprecated or obsolete configuration option is reported with what to use instead ([#169](https://github.com/poldek-pm/poldek/pull/169))
* The pre-0.20 `ftp get`, `http get` and other `* get` options are ignored; use `[fetcher]` sections ([#169](https://github.com/poldek-pm/poldek/pull/169))
* Every question names its keys: `q` aborts, `n` skips a set, `r` asks again (@arekm [#132](https://github.com/poldek-pm/poldek/pull/132))
* Without a terminal a removal that needs a confirmation fails instead of doing nothing; a default answer is shown (@arekm [#133](https://github.com/poldek-pm/poldek/pull/133))
* `up NAME` works as `-n NAME --up` ([#156](https://github.com/poldek-pm/poldek/pull/156))
* `--shcmd` runs its line as the shell does: `cd`, `pwd`, `reload` and `!cmd` work, `up`, `clean` and `repo` are refused; a command of the shell only, or not of the shell, says so ([#203](https://github.com/poldek-pm/poldek/pull/203))
* `&&` and `||` between commands, with spaces around them: `cd /installed && ls foo*` ([#204](https://github.com/poldek-pm/poldek/pull/204))
* `$releasever_major` and `${VAR}` are taken from the environment in a `.repo` file; `${a.b}` and `${x:=w}` stay as written, and an escaped `$` in `${name:-word}` no longer starts a nested expansion ([#187](https://github.com/poldek-pm/poldek/pull/187), [#195](https://github.com/poldek-pm/poldek/pull/195))
* A `.repo` line longer than 4 KB refuses the file, and a read error no longer imports the part before it ([#195](https://github.com/poldek-pm/poldek/pull/195))
* New predefined macro `%{_arch}` in configuration files, e.g. `path = http://.../PLD/%{_arch}/RPMS/` ([#123](https://github.com/poldek-pm/poldek/pull/123))
* New `hooks dir` option: `pre-install.d`/`post-install.d` hooks run once per install, upgrade or removal (@arekm [#72](https://github.com/poldek-pm/poldek/pull/72))
* New per-source `signkey` option and `--nosignature`: GPG signature verification per source
* Boolean dependencies are checked on install and upgrade, also against installed packages (@arekm [#45](https://github.com/poldek-pm/poldek/pull/45), [#47](https://github.com/poldek-pm/poldek/pull/47))
* Repo (source) `ignore` config option is merged with the global one instead of overwriting it - poldek-global-ignore-merges.patch (https://github.com/pld-linux/poldek/commit/1d3b08124897ca8e21a70944d5eb3291cc16a635)
* depsolver: break ties between equivalent providers by preferring highest EVR - poldek-scoring-evr.patch (https://github.com/pld-linux/poldek/commit/3c9d98c7c3b231827a10714d48ccf0f894a9a93f)
* Show config file origin when removing duplicated sources - poldek-dup-sources.patch (https://github.com/pld-linux/poldek/commit/d1adbc7551110e290d82895d4a88bb8b93e975c0)
* multilib: `install foo` takes the native arch only; globs and full names match every arch - poldek-multilib-bare-name-install.patch (https://github.com/pld-linux/poldek/commit/dcf566fbc923ff4bc085c2d9981dfeda98862bdf)
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
* Fix: a hangup, SIGTERM, `^\` or a broken output no longer ends poldek while rpm runs, and the shell keeps its history when a signal ends it (@arekm [#127](https://github.com/poldek-pm/poldek/pull/127), [#128](https://github.com/poldek-pm/poldek/pull/128), [#129](https://github.com/poldek-pm/poldek/pull/129))
* Fix: `hold` in a source section and hold masks of a name-version-release work, and an upgrade skips a held installed version instead of failing (@arekm [#130](https://github.com/poldek-pm/poldek/pull/130), [#131](https://github.com/poldek-pm/poldek/pull/131))
* Fix: HTTP: odd status lines and empty headers are read, a refused redirect is not retried, a server's control characters are not printed (@arekm [#134](https://github.com/poldek-pm/poldek/pull/134), [#135](https://github.com/poldek-pm/poldek/pull/135), [#136](https://github.com/poldek-pm/poldek/pull/136), [#137](https://github.com/poldek-pm/poldek/pull/137), [#138](https://github.com/poldek-pm/poldek/pull/138), [#139](https://github.com/poldek-pm/poldek/pull/139), [#160](https://github.com/poldek-pm/poldek/pull/160))
* Fix: a command that failed or could not start is no longer taken for successful, and none outlives poldek (@arekm [#140](https://github.com/poldek-pm/poldek/pull/140), [#141](https://github.com/poldek-pm/poldek/pull/141), [#142](https://github.com/poldek-pm/poldek/pull/142), [#143](https://github.com/poldek-pm/poldek/pull/143))
* Fix: an alias with a parameter no longer garbles a long argument, its arguments are no longer read as parts of its line, a redefined alias no longer keeps flags of the old one, and one that refers to itself no longer crashes poldek (@arekm [#144](https://github.com/poldek-pm/poldek/pull/144), [#146](https://github.com/poldek-pm/poldek/pull/146), [#193](https://github.com/poldek-pm/poldek/pull/193))
* Fix: the `greedy-upgrade` alias of `cli.conf` named `-G`, an option `install` no longer has (@arekm [#198](https://github.com/poldek-pm/poldek/pull/198))
* Fix: arguments of a command given on the command line are no longer split and parsed again (@arekm [#184](https://github.com/poldek-pm/poldek/pull/184))
* Fix: TAB after `--` no longer crashes where the command has no options, and completes the options of an aliased or prefixed command (@arekm [#197](https://github.com/poldek-pm/poldek/pull/197))
* Fix: the commands of one line (`a ; b`) no longer share their arguments, options and `-q`/`-v` (@arekm [#191](https://github.com/poldek-pm/poldek/pull/191))
* Fix: crashes and hangs on long names and versions, large rpm-md repositories, threaded loads, `search` in an empty directory ([#147](https://github.com/poldek-pm/poldek/pull/147), [#148](https://github.com/poldek-pm/poldek/pull/148), [#164](https://github.com/poldek-pm/poldek/pull/164), [#165](https://github.com/poldek-pm/poldek/pull/165), [#167](https://github.com/poldek-pm/poldek/pull/167), [#172](https://github.com/poldek-pm/poldek/pull/172), [#179](https://github.com/poldek-pm/poldek/pull/179), [#180](https://github.com/poldek-pm/poldek/pull/180), [#181](https://github.com/poldek-pm/poldek/pull/181))
* Fix: a bare `Revision` line of a changelog is read correctly, a `*` line with no space no longer crashes the parser, `ls -T` sorts by build day, `--greedy` refuses a level that would wrap or go negative, a multi-URL `%D` no longer crashes vfile, and `poldeklib_destroy()` removes its SIGWINCH handler ([#153](https://github.com/poldek-pm/poldek/pull/153))
* Fix: `p_close()` reports a failed close, bold output is reset, depdirs no longer leak between loads or get double-freed, and uninitialised reads in the semiorphan report and debug output are gone ([#153](https://github.com/poldek-pm/poldek/pull/153))
* Fix: a removal could abort poldek after it had read a file list ([#159](https://github.com/poldek-pm/poldek/pull/159))
* Fix: `--mkidx` reports an index it could not write and replaces index files at once; `--up` recovers an index with missing or stale metadata ([#161](https://github.com/poldek-pm/poldek/pull/161), [#162](https://github.com/poldek-pm/poldek/pull/162), [#166](https://github.com/poldek-pm/poldek/pull/166), [#168](https://github.com/poldek-pm/poldek/pull/168))
* Fix: a broken record of an index is reported instead of ending its package list (@arekm [#171](https://github.com/poldek-pm/poldek/pull/171), [#174](https://github.com/poldek-pm/poldek/pull/174))
* Fix: the shell no longer uses a cut stub index, and `ignore` masks hide what they should there ([#163](https://github.com/poldek-pm/poldek/pull/163), [#178](https://github.com/poldek-pm/poldek/pull/178), [#182](https://github.com/poldek-pm/poldek/pull/182), [#185](https://github.com/poldek-pm/poldek/pull/185))
* Fix: `--verify=file-conflicts` no longer keeps every pair of owners of a path in memory (@arekm [#173](https://github.com/poldek-pm/poldek/pull/173))
* Fix: a zstd `primary.xml` of an rpm-md repository is read directly instead of decompressed into the cache on every load (@arekm [#175](https://github.com/poldek-pm/poldek/pull/175))
* Fix: cache locks: a directory made meanwhile by another process, a child forked while a thread held a lock, a directory that could not be read, a lock name too long for the file system, and a waited-for lock taken within a second of its release ([#176](https://github.com/poldek-pm/poldek/pull/176), [#177](https://github.com/poldek-pm/poldek/pull/177), [#186](https://github.com/poldek-pm/poldek/pull/186), [#192](https://github.com/poldek-pm/poldek/pull/192), [#200](https://github.com/poldek-pm/poldek/pull/200))
* Fix: `pkgdir__rmf()` no longer keeps a lock or reports success when it could not clean a directory, and a failed `pthread_atfork()` of the vfile locks is logged ([#195](https://github.com/poldek-pm/poldek/pull/195))
* Fix: an upgrade of one of several installed versions into a directory (`--dt dir`) no longer aborts (@arekm [#183](https://github.com/poldek-pm/poldek/pull/183))
* Fix: a ^C during an install or removal finishes the current step and reports failure, also through sudo; a second ^C stops rpm (@arekm [#70](https://github.com/poldek-pm/poldek/pull/70), [#77](https://github.com/poldek-pm/poldek/pull/77), [#118](https://github.com/poldek-pm/poldek/pull/118), [#119](https://github.com/poldek-pm/poldek/pull/119), [#122](https://github.com/poldek-pm/poldek/pull/122), [#125](https://github.com/poldek-pm/poldek/pull/125), [#126](https://github.com/poldek-pm/poldek/pull/126))
* Fix: rpm runs in a session of its own, so `^\`, `^Z` and a closed terminal no longer kill it, and installs in sets are much faster (@arekm [#69](https://github.com/poldek-pm/poldek/pull/69), [#124](https://github.com/poldek-pm/poldek/pull/124))
* Fix: `--fetch` and `--destination` fail when a copy fails (@arekm [#75](https://github.com/poldek-pm/poldek/pull/75), [#76](https://github.com/poldek-pm/poldek/pull/76))
* Fix: a quoted argument beginning with `|` or `;` no longer aborts the shell or `--cmd`; several commands leaked memory (@arekm [#107](https://github.com/poldek-pm/poldek/pull/107), [#108](https://github.com/poldek-pm/poldek/pull/108), [#111](https://github.com/poldek-pm/poldek/pull/111), [#112](https://github.com/poldek-pm/poldek/pull/112), [#114](https://github.com/poldek-pm/poldek/pull/114))
* Fix: `up REPO` no longer crashes on a `-s` source with no name (@arekm [#113](https://github.com/poldek-pm/poldek/pull/113), [#115](https://github.com/poldek-pm/poldek/pull/115))
* Fix: removing a package with a very large file list no longer overflows the stack (@arekm [#121](https://github.com/poldek-pm/poldek/pull/121))
* Fix: `use sudo` no longer makes root run removal through sudo, and a missing sudo is reported as an error for removal too (@arekm [#73](https://github.com/poldek-pm/poldek/pull/73))
* depsolver: a package obsoleted by an installed one is not planned for install, and conflicts with installed packages are resolved correctly (@arekm [#35](https://github.com/poldek-pm/poldek/pull/35), [#38](https://github.com/poldek-pm/poldek/pull/38), [#41](https://github.com/poldek-pm/poldek/pull/41), [#46](https://github.com/poldek-pm/poldek/pull/46), [#47](https://github.com/poldek-pm/poldek/pull/47), [#90](https://github.com/poldek-pm/poldek/pull/90), [#95](https://github.com/poldek-pm/poldek/pull/95), [#96](https://github.com/poldek-pm/poldek/pull/96), [#97](https://github.com/poldek-pm/poldek/pull/97), [#116](https://github.com/poldek-pm/poldek/pull/116), [#117](https://github.com/poldek-pm/poldek/pull/117))
* Fix: a multilib obsoletes check no longer reads an unset arch, which could abort or report another arch ([#195](https://github.com/poldek-pm/poldek/pull/195))
* Fedora / rpm 6 fixes (@arekm [#98](https://github.com/poldek-pm/poldek/pull/98), [#99](https://github.com/poldek-pm/poldek/pull/99), [#101](https://github.com/poldek-pm/poldek/pull/101), [#102](https://github.com/poldek-pm/poldek/pull/102), [#103](https://github.com/poldek-pm/poldek/pull/103))
* Fix: `search --perlre` with a pattern that does not compile printed the error but exited with success; it now fails (@arekm [#105](https://github.com/poldek-pm/poldek/pull/105))
* Fix: the `--enable-strict` build failed with gcc 16 (@arekm [#105](https://github.com/poldek-pm/poldek/pull/105))
* Fix: `poldek -e` listed a package twice when two arguments named it (@arekm [#93](https://github.com/poldek-pm/poldek/pull/93))
* Fix: shell `uninstall name` with several installed versions failed with "ambiguous name"; it now removes all of them (@arekm [#92](https://github.com/poldek-pm/poldek/pull/92))
* Fix: `cd` to a directory that is not there stays where it was instead of going to `/` ([#205](https://github.com/poldek-pm/poldek/pull/205))
* Fix: multilib: masks without an arch (`install a-1-1`) match again (@arekm [#91](https://github.com/poldek-pm/poldek/pull/91))
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
* The cache of installed packages is not used or saved when the database changed while poldek ran (@arekm [#63](https://github.com/poldek-pm/poldek/pull/63))
  - the first run after an upgrade rebuilds that cache; later runs are unaffected
* An old-style `source = name,options path` line keeps its `pri`, `gpg`/`pgp` and `compr` settings (@arekm [#61](https://github.com/poldek-pm/poldek/pull/61))
* An old-style source converted with no `compr` value gets no `compr` ([#196](https://github.com/poldek-pm/poldek/pull/196))
* A `gpg`/`pgp` source of an old-style line has its packages' signatures verified (@arekm [#61](https://github.com/poldek-pm/poldek/pull/61))
* `poldek -l` no longer crashes on a source with `compr` set (@arekm [#60](https://github.com/poldek-pm/poldek/pull/60), [#62](https://github.com/poldek-pm/poldek/pull/62))
* Fix: a source option no longer sets internal option bits in its source's flags, so a library user that skips group sources no longer skips every source with a valued option (@arekm [#62](https://github.com/poldek-pm/poldek/pull/62), [#195](https://github.com/poldek-pm/poldek/pull/195))
* Fix: a `compr` value other than `gz`, `zst` or `none` is refused with its line instead of failing when the index is opened ([#195](https://github.com/poldek-pm/poldek/pull/195))
* `--verify=file-conflicts` reports every differing file of a pair of packages, including the last copy of a name (@arekm [#57](https://github.com/poldek-pm/poldek/pull/57))
* A package with an unusable header or a truncated file list no longer aborts poldek when loading a dir index (@arekm [#56](https://github.com/poldek-pm/poldek/pull/56))
* poldek no longer aborts on rpm v6 packages or files of 4 GiB and more; sizes no longer wrap on 32-bit systems (@arekm [#55](https://github.com/poldek-pm/poldek/pull/55))
* Sizes of 2 GiB and more are no longer printed as negative numbers (@arekm [#53](https://github.com/poldek-pm/poldek/pull/53)); `ls -v` sums sizes beyond 2 TB without overflow ([#195](https://github.com/poldek-pm/poldek/pull/195))
* On 32-bit systems the dir index is no longer rebuilt for every package; long repository paths and URL password masking work (@arekm [#52](https://github.com/poldek-pm/poldek/pull/52))
* Build fixes for newer toolchains that fail with `-Werror` (@arekm [#51](https://github.com/poldek-pm/poldek/pull/51))
* Paths, URLs and package file names containing `%` are handled correctly (@arekm [#50](https://github.com/poldek-pm/poldek/pull/50))
* `ls --qf` and `desc -L` no longer crash on packages whose metadata is unavailable (e.g. indexes built with `--mo=nodesc`) (@arekm [#49](https://github.com/poldek-pm/poldek/pull/49))
* Reading the installed database cache under `--root` now shows summaries and file lists, and no longer leaks memory (@arekm [#48](https://github.com/poldek-pm/poldek/pull/48))
* `desc -B` shows only packages that actually need the version being described, instead of every package sharing its name (@arekm [#44](https://github.com/poldek-pm/poldek/pull/44))
* `desc -R` / `desc -B` while in `/installed` now searches only installed packages, not the available ones (@arekm [#43](https://github.com/poldek-pm/poldek/pull/43))
* Required-packages listing (`desc -R`) no longer changes its result depending on the order packages were queried in (@arekm [#42](https://github.com/poldek-pm/poldek/pull/42))
* Refreshing a package index no longer keeps a broken partially-built index or prints stale package descriptions afterwards (@arekm [#40](https://github.com/poldek-pm/poldek/pull/40))
* Suggested packages: the 'r' key repeats the current round of suggested-package questions instead of skipping them (@arekm [#39](https://github.com/poldek-pm/poldek/pull/39))
* A stale keep-alive connection reconnects silently, and a TLS connection closed without close_notify is not fatal (@arekm [#37](https://github.com/poldek-pm/poldek/pull/37))
* `poldek --help` no longer loops or crashes on a narrow terminal; `$COLUMNS`/`$LINES` are used as a fallback when the terminal size is unknown (@arekm [#36](https://github.com/poldek-pm/poldek/pull/36))
* Fixed 'Q' in the suggested-packages question to abort the transaction (lp#1532694) (@arekm [#34](https://github.com/poldek-pm/poldek/pull/34))
* Fixed `--help` output so the "Report bugs to ..." line is shown again; rpm install notices are filtered from the output again (@arekm [#33](https://github.com/poldek-pm/poldek/pull/33))
* Fixed SIGSEGV with --noconf (@arekm [#32](https://github.com/poldek-pm/poldek/pull/32))
* Fix: `poldek_ts_get_summary()` and `packages_uniq()` were not exported from libpoldek, so a program using them failed to link; `tests/exports.sh` now checks every declared export (@arekm [#58](https://github.com/poldek-pm/poldek/pull/58))
* Fix: an explicit port of a URL is kept across redirects and proxies, and dead FTP connections are not reused - poldek-url-keep-port.patch, poldek-vcn-pool-reuse.patch, poldek-vfstat-redirect-crash.patch, poldek-ftp-pasv-crash.patch, poldek-ftp-pasv-parse.patch (https://github.com/pld-linux/poldek/commit/59e3fec55abec6a69632766852c42ac36aec064b)
* Fix path traversal detection (trailing `/..`); fix metadata handling with libxml2

### Build, tests and internal changes
* Tests: `make check-sh` runs scripts in parallel (`CHECK_SH_JOBS`), the test packages are built once and cached, `make lint` runs cppcheck, and the sh tests no longer need sudo ([#151](https://github.com/poldek-pm/poldek/pull/151), [#153](https://github.com/poldek-pm/poldek/pull/153), [#154](https://github.com/poldek-pm/poldek/pull/154), [#155](https://github.com/poldek-pm/poldek/pull/155), [#157](https://github.com/poldek-pm/poldek/pull/157), [#188](https://github.com/poldek-pm/poldek/pull/188), [#190](https://github.com/poldek-pm/poldek/pull/190), [#201](https://github.com/poldek-pm/poldek/pull/201))
* Tests: `make check` skips the tests of trurlib and tndb (`check-all` runs them), the CI build uses `make -k`, and tests were added for HTTP status lines, changelogs, translated descriptions and file conflicts ([#201](https://github.com/poldek-pm/poldek/pull/201))
* `openpty()` is required to build; the `--enable-strict --enable-trace` and `--disable-imode` builds work (@arekm [#109](https://github.com/poldek-pm/poldek/pull/109), [#110](https://github.com/poldek-pm/poldek/pull/110), [#120](https://github.com/poldek-pm/poldek/pull/120))
* Tests and CI: the CI build fails on compiler warnings, and `n_snprintf()` format checks and vfile test times are fixed (@arekm [#54](https://github.com/poldek-pm/poldek/pull/54), [#64](https://github.com/poldek-pm/poldek/pull/64), [#94](https://github.com/poldek-pm/poldek/pull/94))
* Installed headers no longer declare functions and variables that no library defines, and unused pty code is gone (@arekm [#59](https://github.com/poldek-pm/poldek/pull/59), [#74](https://github.com/poldek-pm/poldek/pull/74))
* Package arch and OS names are kept in one symbol table read without a lock; a package of a 65th arch is refused with a warning ([#194](https://github.com/poldek-pm/poldek/pull/194))
* Documentation and comments were updated, and dead code is gone ([#196](https://github.com/poldek-pm/poldek/pull/196))
* A package file is closed as soon as rpm has read its headers, and the unused `fd` parameter of `rpm_signatures()` is gone (@arekm [#104](https://github.com/poldek-pm/poldek/pull/104))
* Fix: a duplicate `PKG_NEVRA_MAX` definition broke the build (@arekm [#199](https://github.com/poldek-pm/poldek/pull/199))

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
