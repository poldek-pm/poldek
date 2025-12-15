# Poldek Scripts

This directory contains various utility scripts for poldek development and maintenance.

## generate-changes-summary.sh

Generate a summary of code changes since the last release. This script is useful for preparing release notes and understanding what has changed in the codebase.

### Usage

```bash
./scripts/generate-changes-summary.sh [OPTIONS]
```

### Options

- `--format=FORMAT` - Output format: `text` or `markdown` (default: `text`)
- `--from=REF` - Compare from specific tag/commit (default: auto-detect latest tag)
- `--stats` - Show commit statistics including file changes
- `--authors` - Show list of contributors
- `--no-grouping` - Don't group commits by type (show chronologically)
- `--help`, `-h` - Show help message

### Features

- **Automatic tag detection**: Finds the most recent release tag automatically
- **Commit categorization**: Groups commits by type (Features, Bug Fixes, Chores, etc.)
- **Multiple output formats**: Supports plain text with colors and Markdown
- **Statistics**: Can show file change statistics
- **Contributors list**: Shows all authors who contributed since the last release

### Examples

Basic usage (auto-detect latest release):
```bash
./scripts/generate-changes-summary.sh
```

Generate markdown output with full details:
```bash
./scripts/generate-changes-summary.sh --format=markdown --stats --authors
```

Compare from a specific tag:
```bash
./scripts/generate-changes-summary.sh --from=v0.44.0
```

Show all changes chronologically without grouping:
```bash
./scripts/generate-changes-summary.sh --no-grouping
```

### Commit Types

The script recognizes the following commit type prefixes:

- `feat:` or `feat(scope):` - New features
- `fix:` or `Fix:` - Bug fixes
- `docs:` or `doc:` - Documentation changes
- `chore:` - Maintenance tasks
- `test:` or `tests:` - Test additions or modifications
- `refactor:` - Code refactoring
- `perf:` - Performance improvements
- `style:` - Code style changes
- `build:` or `ci:` - Build system or CI changes
- `Merge pull request` - Pull request merges

Commits that don't match any of these patterns are categorized as "Other Changes".

### Output Examples

#### Text Output

```
=== Changes Since Last Release ===
From: v0.44.0 (2025-02-27)
Commits: 1

## Changes by Category

Chores (1):
  11c71f1 - chore: test case for #25
         by mis on 2025-12-15
```

#### Markdown Output

```markdown
# Changes Since Last Release

**From:** `v0.44.0` (2025-02-27)
**Commits:** 1

## Changes by Category

### Chores (1)

* test case for #25 (`11c71f1`) - mis, 2025-12-15
```

## Other Scripts

### build-dist-packages.sh

Build distribution packages for multiple platforms.

### zlib-in-rpm.sh

Helper script for handling zlib compression in RPM packages.

### vfcompr, vfjuggle, vfsmb

Virtual file system utility scripts.

### cdpoldek.sh

Change directory helper for poldek development.

### dotrepo.sh

Repository configuration helper.

### poldekuser-setup.sh

User setup script for poldek.
