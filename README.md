[![build](https://github.com/poldek-pm/poldek/actions/workflows/build.yml/badge.svg)](https://github.com/poldek-pm/poldek/actions/workflows/build.yml)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html)

`poldek` is a full-featured frontend to RPM, designed to make package management easier and more efficient.

## Features

- Comprehensive package management for RPM
- Easy to use command-line interface
- Interactive shell mode
- Fast and efficient operations

For details see man page and info manual. Old project homepage can be found [http://poldek.pld-linux.org](http://poldek.pld-linux.org).



## Build

```shell
  ./autogen.sh && make
```

`poldek` is used by [PLD Linux Distribution](https://pld-linux.org), RPM package spec is available [here](https://github.com/pld-linux/poldek).

## Usage

Simply run the `poldek` command followed by the desired options and arguments. For example:

```sh
poldek install package-name
```

For a full list of available commands and options, run:

```sh
poldek help
```
Run `poldek` without arguments to get interactive shell mode with command and package name completion.

## Development

### Generating Changes Summary

To generate a summary of changes since the last release for release notes:

```sh
# Text format with colors
./scripts/generate-changes-summary.sh

# Markdown format with statistics and authors
./scripts/generate-changes-summary.sh --format=markdown --stats --authors

# Using make (after running autogen.sh)
make changes-summary
make changes-summary-markdown
```

See [scripts/README.md](scripts/README.md) for more information about available development scripts.

## License

This project is licensed under the GNU General Public License v2.0 - see the [LICENSE](LICENSE) file for details.

## Authors

See [Contributors](https://github.com/poldek-pm/poldek/graphs/contributors) + [CREDITS](doc/CREDITS)
