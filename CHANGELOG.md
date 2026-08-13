# Changelog

All notable changes to this project will be documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

This project uses a `YYYY.N` versioning scheme (release year and a sequence
number within that year) for the repository as a whole — a release marks a
snapshot of the whole tree, not a specific plugin. Individual plugins carry
their own independent version instead (frei0r `major_version`/`minor_version`,
mirrored in the `version` attribute of their Kdenlive effect XML, and tracked
via each plugin's `bump-major`/`bump-minor` Makefile targets); the two
numbers are unrelated by design.

## [Unreleased]

### Changed

- `kdenlive-with-filters`: instead of a hardcoded default path, the Kdenlive
  AppImage is now auto-detected by scanning `$PATH` for `kdenlive*.AppImage`,
  picking the most recently modified match; `KDENLIVE_APPIMAGE` still
  overrides. A warning is printed to stderr listing all matches when more
  than one AppImage is found.
- Migrated the build/test environment from Conda (`environment.yml`) to
  [pixi](https://pixi.sh) (`pixi.toml`/`pixi.lock`). `make env-create`/
  `make env-update` are replaced by `make bootstrap` (installs pixi itself
  if missing, then `pixi install`); `pixi run build`/`install`/`uninstall`/
  `test`/`clean`/`hooks` are thin tasks around the equivalent `make`
  targets. The pre-commit hooks for `clang-format`/`cppcheck`/the C syntax
  check/the `extendedges` test suite now run as `language: system` instead
  of `language: conda`, relying on the pixi environment's `PATH`
  (`clang-format`/`cppcheck` added as pixi dependencies) rather than
  pre-commit's own per-hook Conda provisioning.

### Removed

- `environment.yml` (superseded by `pixi.toml`/`pixi.lock`).

## [2026.1] - 2026-07-14

### Added

- Project scaffolding: conda environment (`environment.yml`), top-level
  `Makefile` delegating `build`/`install`/`uninstall`/`test`/`clean` to each
  filter's own `Makefile`.
- "Extend edges" frei0r filter: fills letterbox/pillarbox bars left by a
  mismatched clip geometry with a stretched, blurred extension of the
  picture edges.
- `kdenlive-with-filters` launcher script, working around the Kdenlive
  AppImage's frei0r plugin directory not honouring `FREI0R_PATH`.
- Per-plugin version tracking: `version` attribute in the Kdenlive effect
  XML, mirroring the frei0r `major_version`/`minor_version`, with
  `bump-major`/`bump-minor` Makefile targets callable per filter.
- Free-software repository scaffolding: GPLv3 `LICENSE`, License and Author
  sections in the README.
- `pre-commit` setup: standard hygiene hooks, XML and shell linting,
  `clang-format`, `cppcheck`, a C syntax check, and the filter's own test
  suite.
- IDE entries (VS Code, JetBrains, Eclipse/CDT, Vim, Emacs) in `.gitignore`.
- `CHANGELOG.md` (this file, Keep a Changelog format).
- `CODING.md`: developer guide split out from the user-facing README.
