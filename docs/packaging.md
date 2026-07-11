# Packaging

The project installs as a native GUI application and provides baseline CPack
generators for every target platform:

- Linux: `.tar.gz` and `.deb`;
- macOS: application bundle in a DMG, plus `.tar.gz`;
- Windows: NSIS installer and `.zip`.

Build and create packages with:

```bash
cmake -S . -B build -DOpenMS_DIR=/path/to/OpenMS-build
cmake --build build --config Release -j
cpack --config build/CPackConfig.cmake -C Release
```

The baseline packaging implementation is complete: the generated package
contains OpenMS Viewer, its license, README, and project documentation. Release
publishing still needs the platform deployment step that
bundles or resolves the exact Qt and OpenMS shared libraries used by the build:
`windeployqt` on Windows, `macdeployqt` plus code signing/notarization on macOS,
and distribution-specific runtime dependencies or an AppImage build on Linux.
Those deployment/signing credentials intentionally remain release-infrastructure
concerns rather than source-tree assumptions.
