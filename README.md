# PBKDEnliveTools

Custom video filters and transitions for [Kdenlive](https://kdenlive.org/), packaged as
[frei0r](https://frei0r.dyne.org/) plugins with their matching Kdenlive effect
descriptors.

Want to contribute or build a new filter? See [CODING.md](CODING.md).

## Installing

```bash
make env-create
conda activate pbkdenlivetools
make install
```

`make install` installs, for every filter:

- the compiled `.so` into `~/.local/lib/frei0r-1/`
- the Kdenlive effect XML into `~/.local/share/kdenlive/effects/`

and additionally installs `~/.local/bin/kdenlive-with-filters`.

After installing, restart Kdenlive so it picks up the new plugin.

### No GUI install

These filters cannot be installed from within Kdenlive's interface. Kdenlive's
"Download New Effects" dialog (Get Hot New Stuff) only fetches **effect
templates** — XML stacks of *existing* built-in effects, the kind you get
from "Export effect" — not compiled frei0r plugins. There is no GUI action to
import a `.so` plugin: Kdenlive only picks up plugins from fixed directories
(`frei0r-1` for the `.so`, `kdenlive/effects` for the XML descriptor) that it
scans at startup, so installation has to go through `make install` as
described above, followed by a restart of Kdenlive.

## Using an AppImage install of Kdenlive

If Kdenlive is installed as an AppImage, its bundled frei0r module resolves
its plugin directory **relative to its own bundled library**, not through
the standard `FREI0R_PATH` environment variable — so plugins installed under
`~/.local/lib/frei0r-1/` are invisible to it by default.

`kdenlive-with-filters` works around this: it keeps a local extracted copy
of the AppImage (re-extracting automatically whenever the AppImage file
changes), symlinks everything from `~/.local/lib/frei0r-1/` into that
extracted copy's own frei0r directory, and launches it. Use it instead of
launching the AppImage directly:

```bash
kdenlive-with-filters
```

It auto-detects the AppImage by looking for `kdenlive*.AppImage` in every
directory listed in `$PATH`, picking the most recently modified match;
override with `KDENLIVE_APPIMAGE=/path/to/Kdenlive.AppImage`.

If Kdenlive is installed some other way (distro package, or a Flatpak with
filesystem access to `~/.local`), it most likely already honours the
standard frei0r plugin locations and this wrapper isn't needed — just
launch Kdenlive normally after `make install`.

---

## Filters

### Extend edges

When a clip's geometry doesn't match the project profile, Kdenlive/MLT pads
it with flat letterbox (top/bottom) or pillarbox (left/right) bars before
handing the frame to filters. "Extend edges" detects those flat bars from
the outer edges of the frame and replaces them with a stretched, blurred
extension of the adjacent picture content, instead of leaving flat bars.

Parameters:

- **Edge sample width (px)** — how many pixels of the existing picture edge
  are sampled to build the stretched extension.
- **Blur amount (px)** — blur radius applied to the extended region, so it
  blends into the real footage instead of showing a hard seam.

Genuine picture content is left untouched; only the detected bar regions
(plus a small feathered blend zone at their boundary) are modified.

---

## License

PBKDEnliveTools is free software: you can redistribute it and/or modify it
under the terms of the **GNU General Public License v3** as published by the
Free Software Foundation. See the [LICENSE](LICENSE) file for details.

## Author

Marcel Spock <mrspock@cardolan.net>
PBMou

"PBMou" is a bilingual pun: "PB" stands for Poilbarbe, and "Mou" is the
French translation of "Soft" — so PBMou reads as "PBSoft" translated halfway
into French, a French/English wordplay typical of the author's naming
conventions.
