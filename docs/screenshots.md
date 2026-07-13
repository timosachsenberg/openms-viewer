# Regenerating the screenshots

The images under `docs/screenshots/` are produced by a committed generator so
visual changes show up as **image diffs in git** — re-run it after a rendering
change and `git diff --stat docs/screenshots/` tells you what moved.

Raw-centric views (peak map, metadata, FAIMS, ion mobility / diaPASEF) are
rendered from **real datasets on [archive.openms.de](https://archive.openms.de)**;
derived views (features, identifications, consensus, OpenSWATH) and the annotated
mirror spectrum use deterministic OpenMS test fixtures / synthetic bundles, since
the archive ships raw runs only.

## 1. Fetch the data (cached, resumable)

```bash
tools/screenshots/fetch-data.sh small        # ~170 MB: LTQ-Velos mzML + Angiotensin raw
tools/screenshots/fetch-data.sh small large  # +FAIMS mzML (1 GB) + timsTOF diaPASEF .d.zip (329 MB)
tools/screenshots/fetch-data.sh all          # + Bruker MALDI .d (~3 GB)
```

Files land in `.screenshot-cache/` (gitignored, set `SCREENSHOT_CACHE` to move it).
Already-present files are skipped, so re-running is cheap. The dataset list lives
in [`tools/screenshots/manifest.tsv`](../tools/screenshots/manifest.tsv).

## 2. Generate

```bash
cmake --build build --target openms-viewer-gallery
QT_QPA_PLATFORM=offscreen ./build/openms-viewer-gallery          # whole gallery
QT_QPA_PLATFORM=offscreen ./build/openms-viewer-gallery faims im # only these views
```

View tags: `hero spectrum consensus osw imaging peakmap faims im`. Real-data views
are **skipped** (not failed) when their cached file is absent, so the generator
works on any tier. Env overrides: `SCREENSHOT_OUT`, `SCREENSHOT_CACHE`,
`OPENMS_TEST_DIR` (OpenMS `src/tests/topp` fixtures).

The generator source is [`tools/screenshots/gallery.cpp`](../tools/screenshots/gallery.cpp);
it is a standalone dev target (`OPENMS_VIEWER_STANDALONE` only) and is not part of
the embedded OpenMS release build.

## Notes

- **MALDI imaging.** The viewer reads Bruker **`.tdf`** MALDI imaging `.d`
  directories (via OpenMS `BrukerTimsImagingFile`, converted to a temporary imzML).
  The archive's MALDI dataset, however, is single-quad **`.tsf`**, which OpenMS has
  no reader for — so `imaging.png` stays a synthetic continuous-mode image until a
  real `.tdf` MALDI dataset is available. The MALDI entry stays in the manifest
  (`xlarge`).
- **Comparison.** [comparison-pyopenms-viewer.md](comparison-pyopenms-viewer.md)
  renders the same real dataset in both this viewer and `pyopenms-viewer` and
  judges where each looks better.
