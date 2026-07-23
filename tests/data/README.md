# Test data

Sample mass-spectrometry data used by the automated tests (see
`tests/FeatureVisualizationTest.cpp`). The path is passed to the test binary via
the `OPENMS_VIEWER_TEST_DATA_DIR` compile definition (see `CMakeLists.txt`).

| File | Contents |
|------|----------|
| `BSA1_F1.mzML` | BSA tryptic-digest LC-MS/MS run — 767 spectra (MS1 + MS2). |
| `BSA1_F1.featureXML` | 256 features detected on the run above (FeatureFinderCentroided), with convex hulls. |
| `BSA1_F1.idXML` | 48 peptide identifications (score type FDR) for the run. |
| `openswath_transitions.xic` | A small OpenSWATH `.xic` (CHROMPARQUET) chromatogram store — 18 SRM transition XICs — for the standalone-`.xic` loader. |

These three files form a raw-run + detected-features + identifications trio for
testing annotated feature visualization and the feature-table ↔ peak-map
interaction.

## Provenance & license

The `BSA1_F1.*` trio is vendored from the sibling **pyopenms-viewer** project
(`tests/data/BSA1_F1.*`), which is licensed **BSD 3-Clause**
(Copyright © 2025 Timo Sachsenberg) — the same author and license as this
repository, so redistribution here is unencumbered.

`openswath_transitions.xic` is vendored from the **OpenMS** test data
(`src/tests/class_tests/openms/data/XICParquetFile_1_input.xic`), which is
licensed **BSD 3-Clause** — redistribution here is likewise unencumbered.
