# Developer Guide

This document covers everything you need to work on PBKDEnliveTools itself:
repository layout, build/test workflow, versioning, and code quality tooling.
For user-facing installation/usage, see [README.md](README.md).

## Repository layout

```text
PBKDEnliveTools/
  environment.yml          conda environment (compiler, make, pkg-config, mlt, ffmpeg)
  Makefile                 env management + delegates to each filter's Makefile
  bin/kdenlive-with-filters  launcher helper (see README's "Using an AppImage install")
  extendedges/              the "Extend edges" filter (source, XML, own Makefile)
```

Each filter/transition lives in its own top-level directory with its own
source, its own Kdenlive XML descriptor, and its own `Makefile`. The
repository root only handles the shared conda build/test environment and
delegates `build`/`install`/`uninstall`/`test`/`clean`/`bump-major`/`bump-minor`
to each filter's `Makefile`.

## Setting up the build environment

```bash
make env-create   # first time
make env-update   # after environment.yml changes
conda activate pbkdenlivetools
```

The environment provides a C compiler, `make`, `pkg-config`, `melt`/`libmlt`
(for testing) and `ffmpeg` (to generate test clips). A C compiler is enough
to build the plugins; conda-forge's `mlt` package does not include frei0r
support, so `melt` from this environment can load and run the plugins for
unit-style testing but cannot be used to validate the full frei0r bridge —
each filter's own test suite (see below) is the reliable, MLT-independent
way to validate its logic.

## Build, test, clean

```bash
make build      # compile every filter
make test       # run every filter's test target
make clean      # remove build artifacts
```

Any filter can also be built/tested/installed on its own from its directory,
e.g.:

```bash
cd extendedges
make build
make test       # standalone check of the fill/blur algorithm, no MLT needed
make install
```

## Adding a new filter/transition

1. Create a new top-level directory (e.g. `myfilter/`), using `extendedges/`
   as a template: the frei0r plugin source, a vendored `frei0r.h` if not
   already shared, the Kdenlive effect XML (`frei0r_<name>.xml`), a standalone
   test harness, and a `Makefile` exposing the same targets as
   `extendedges/Makefile` (`build`, `install`, `uninstall`, `test`, `clean`,
   `bump-major`, `bump-minor`).
2. Add the directory name to `FILTERS` in the top-level `Makefile` so it's
   picked up by the root `build`/`install`/`uninstall`/`test`/`clean` targets.
3. Add a chapter for it in the README's "Filters" section.

## Versioning

Two independent version numbers coexist in this repository — don't confuse
them:

### Per-plugin version

Each frei0r plugin reports its own `major_version`/`minor_version` as part
of the frei0r ABI (`f0r_plugin_info_t`) — this isn't optional, every
conforming plugin must fill it in. It's mirrored in the `version` attribute
of the plugin's Kdenlive effect XML, which Kdenlive uses to keep loading
older saved projects correctly if the effect's parameters change later.

Bump it with:

```bash
make bump-major FILTER=extendedges   # e.g. 1.2 -> 2.0 (also resets minor to 0)
make bump-minor FILTER=extendedges   # e.g. 1.2 -> 1.3
```

Both targets update the plugin's `.c` source and its Kdenlive XML together,
so they never drift apart.

### Repository version

The repository as a whole is versioned via **git tags only**, using a
`YYYY.N` scheme (release year and a sequence number within that year, e.g.
`2026.1`) — no `VERSION` file. This is a release/distribution marker for the
whole tree, deliberately decoupled from individual plugins' frei0r versions:
a repo release can bundle changes to one filter without implying anything
changed in the others.

## Changelog

`CHANGELOG.md` follows the [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
format. Add entries under `## [Unreleased]` as you go; when cutting a
release, retitle that section `## [YYYY.N] - YYYY-MM-DD` and tag the commit
`git tag YYYY.N`. Before writing a release entry, check
`git log --oneline <previous-tag>..HEAD` and make sure every relevant commit
is reflected.

## Code quality (pre-commit)

```bash
pre-commit install       # once, sets up the git hook
pre-commit run --all-files
```

Hooks (`.pre-commit-config.yaml`):

- Standard hygiene: `trailing-whitespace`, `end-of-file-fixer`,
  `mixed-line-ending`, `check-merge-conflict`, `check-added-large-files`,
  `check-yaml`, `check-xml` (covers the Kdenlive effect XML files).
- `shellcheck` for `bin/kdenlive-with-filters`.
- `markdownlint-cli2` for Markdown docs (see `.markdownlint-cli2.yaml`).
- `clang-format` (style, see `.clang-format`) and `cppcheck` (static
  analysis) for C sources, both provisioned from conda-forge in an isolated
  environment so no system install is required.
- A `gcc -fsyntax-only -Wall -Wextra -Werror` check, distinct from
  `cppcheck`: the compiler catches real compile errors, `cppcheck` catches
  data-flow bugs (e.g. unchecked `malloc`, dead conditions) the compiler
  doesn't flag by default — they're complementary, not redundant.
- Each filter's own test suite (e.g. `extendedges-test`), run whenever its
  `.c`/`.h` files change.

## Third-party code

`extendedges/frei0r.h` is a vendored, unmodified copy of the
[frei0r](https://frei0r.dyne.org/) plugin API header from the
[dyne/frei0r](https://github.com/dyne/frei0r) project, included here so the
plugin can be built without a separate frei0r development package. That
project is licensed under the GNU General Public License v2 — see
[dyne/frei0r's COPYING file](https://github.com/dyne/frei0r/blob/master/COPYING).
If you add a new filter that also vendors this header, keep it unmodified
and don't duplicate this note per-filter — point back here instead.

## License

GPLv3 — all contributions must be compatible with this license. See
[LICENSE](LICENSE).
