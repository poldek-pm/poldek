## 0.44.0 (2025/02/26)
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
  Two new commands has been added `up` (wortks like `--up` switch) and `clean` (`--clean`).
 
  See `poldek help` and `poldek <command> --help` for details.
* Tilde (`~`) at the end of package name is treated as a asterisk wildcard (`*`), e.g: 
  ```
  poldek install foo-~
  ```  
* Boolean dependency support (at least tested cases - see tests/sh/10-booldeps)
* New option --color forces coloured output (#21)
* Coloured and compact output of `llu` (`ls -lu`); old 4-column view is available with `ls -llu`
* Fixed depsolver crash (issues #15, #24)
* Reduced startup time:
    - lazy dep indexing
    - repos loading in threads
    - zlib-ng is used by default for (de)compressing
    - `install` arguments validation before repo load
* Removed features:
    - package set splitting (`--split`)
    - package set graphing (nobody using it probably)
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

## previous versions

See NEWS.old
