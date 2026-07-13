# Test data

Sample mass-spectrometry data used by the automated tests (see
`tests/FeatureVisualizationTest.cpp`). The path is passed to the test binary via
the `OPENMS_VIEWER_TEST_DATA_DIR` compile definition (see `CMakeLists.txt`).

| File | Contents |
|------|----------|
| `BSA1_F1.mzML` | BSA tryptic-digest LC-MS/MS run — 767 spectra (MS1 + MS2). |
| `BSA1_F1.featureXML` | 256 features detected on the run above (FeatureFinderCentroided), with convex hulls. |
| `BSA1_F1.idXML` | 48 peptide identifications (score type FDR) for the run. |

These three files form a raw-run + detected-features + identifications trio for
testing annotated feature visualization and the feature-table ↔ peak-map
interaction.

## Provenance & license

Vendored from the sibling **pyopenms-viewer** project
(`tests/data/BSA1_F1.*`), which is licensed **BSD 3-Clause**
(Copyright © 2025 Timo Sachsenberg) — the same author and license as this
repository, so redistribution here is unencumbered.
