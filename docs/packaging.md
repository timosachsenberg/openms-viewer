# Packaging

## Windows portable package

The `Windows portable package` GitHub Actions workflow is the first complete
deployment pipeline. Run it manually from the Actions tab. Each run:

1. checks out `OpenMS/OpenMS` at the current `develop` commit;
2. builds only the OpenMS core libraries (`WITH_GUI=OFF`) and installs their
   headers, CMake package, and shared data;
3. builds OpenMS Viewer in `Release` mode;
4. recursively deploys OpenMS and contrib DLLs, runs `windeployqt` for Qt DLLs
   and plugins, and adds the Visual C++ and OpenMP runtimes app-locally;
5. bundles the .NET 8 runtime and managed Thermo RawFileReader bridge;
6. runs both the viewer and OpenMS's real Thermo RAW regression test against
   the portable folder with a sanitized `PATH`; and
7. uploads `OpenMSViewer-Windows-x64-openms-<sha>.zip` plus its SHA-256 file.

The ZIP contains one top-level `OpenMSViewer` folder. Run
`OpenMSViewer/bin/openms-viewer.exe` directly after extraction. Keeping the
executable in `bin` is intentional: OpenMS discovers its required shared data at
`../share/OpenMS` relative to the executable.

The artifact includes `BUILD-INFO.txt` with the exact Viewer/OpenMS commits and
build switches. Bruker (`WITH_OPENTIMS=ON`), Thermo RAW
(`WITH_THERMO_RAW=ON`), and OpenMP are enabled. The viewer automatically uses
the bundled .NET 8 runtime unless `DOTNET_ROOT` is already set. The archive is
currently unsigned, so Windows may show a SmartScreen warning.

## Baseline local packages

The project also installs as a native GUI application and provides baseline
CPack generators for every target platform:

- Linux: `.tar.gz` and `.deb`;
- macOS: application bundle in a DMG, plus `.tar.gz`;
- Windows: NSIS installer and `.zip`.

Build and create packages with:

```bash
cmake -S . -B build -DOpenMS_DIR=/path/to/OpenMS-build
cmake --build build --config Release -j
cpack --config build/CPackConfig.cmake -C Release
```

The generated local package contains OpenMS Viewer, its license, README, and
project documentation. Only the Windows Actions pipeline currently adds and
verifies every runtime dependency. Local Windows CPack output, macOS packages,
and Linux packages still need their platform deployment step: `windeployqt`,
`macdeployqt` plus code signing/notarization, or distribution-specific runtime
dependencies/an AppImage build, respectively.
