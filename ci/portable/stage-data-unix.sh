#!/usr/bin/env bash
# Stage the OpenMS shared data tree, the Thermo managed bridge, third-party
# licenses, provenance (BUILD-INFO.txt), and the platform README into the
# viewer package. Invoked by the "Add runtime data, licenses, and provenance"
# stage.
#
# Required environment:
#   RUNNER_OS             "Linux" or "macOS"
#   VIEWER_STAGE          viewer staging prefix
#   OPENMS_INSTALL        OpenMS install prefix
#   OPENMS_CONTRIB_SOURCE cloned OpenMS contrib source (license texts)
#   QT_ROOT_DIR           Qt prefix (Qt LICENSES)
#   PACKAGE_PLATFORM      "Linux" or "macOS" (matrix.platform)
#   QT_ARCH               Qt arch label (matrix.qt_arch)
#   ARCH                  package arch label (matrix.arch)
#   VIEWER_SHA OPENMS_REF OPENMS_SHA CONTRIB_TAG DEPLOYED_QT_VERSION
#   DOTNET_RUNTIME_VERSION DOTNET_FXR_VERSION   provenance values from earlier stages
# shellcheck disable=SC2154  # inputs above are provided by the workflow environment
set -eo pipefail

managed_source=$(find "$OPENMS_INSTALL" -type d \
  -path '*/openms_thermo_bridge/managed' -print -quit)
if [[ -z "$managed_source" ]]; then
  echo "The installed OpenMS Thermo managed bridge was not found." >&2
  exit 1
fi

if [[ "$RUNNER_OS" == "macOS" ]]; then
  bundle_contents="$VIEWER_STAGE/OpenMS Viewer.app/Contents"
  # The .NET managed assemblies are CLR-loaded PE data, not Mach-O code.
  # codesign treats everything under Frameworks/ as nested code and
  # rejects them, so stage them under Resources/ (sealed as resources);
  # a Frameworks/managed symlink added after macdeployqt keeps the
  # OpenMS Thermo bridge's default_managed_directory() resolving them.
  managed_destination="$bundle_contents/Resources/managed"
  share_destination="$bundle_contents/share/OpenMS"
else
  managed_destination="$VIEWER_STAGE/lib/managed"
  share_destination="$VIEWER_STAGE/share/OpenMS"
fi
mkdir -p "$managed_destination" "$share_destination"
cp -a "$managed_source/." "$managed_destination/"
cp -a "$OPENMS_INSTALL/share/OpenMS/." "$share_destination/"

license_root="$VIEWER_STAGE/share/licenses"
mkdir -p "$license_root"
cp "$OPENMS_INSTALL/share/OpenMS/LICENSES/OpenMS-BSD-3-Clause.txt" \
  "$license_root/OpenMS-BSD-3-Clause.txt"
cp "$OPENMS_CONTRIB_SOURCE/LICENSE.md" "$license_root/OpenMS-contrib.md"
cp -a "$OPENMS_CONTRIB_SOURCE/licensetexts" \
  "$license_root/OpenMS-contrib-licenses"
thermo_license="$OPENMS_INSTALL/share/OpenMS/LICENSES/ThermoRawFileReader-License.doc"
if [[ -f "$thermo_license" ]]; then
  cp "$thermo_license" "$license_root/"
fi
if [[ -d "$QT_ROOT_DIR/LICENSES" ]]; then
  cp -a "$QT_ROOT_DIR/LICENSES" "$license_root/Qt"
fi

cat > "$VIEWER_STAGE/BUILD-INFO.txt" <<EOF
OpenMS Viewer portable $PACKAGE_PLATFORM build
Viewer commit: $VIEWER_SHA
OpenMS ref: $OPENMS_REF (pinned)
OpenMS commit: $OPENMS_SHA
OpenMS contrib release: $CONTRIB_TAG
OpenMS WITH_GUI: OFF
OpenMS BUILD_TOPP_TOOLS: OFF
OpenMS WITH_THERMO_RAW: ON
OpenMS WITH_OPENTIMS: ON
OpenMS OpenMP: ON
Qt: $DEPLOYED_QT_VERSION ($QT_ARCH)
.NET runtime: $DOTNET_RUNTIME_VERSION (hostfxr $DOTNET_FXR_VERSION, $ARCH, app-local)
Architecture: $ARCH
Built at: $(date -u +%Y-%m-%dT%H:%M:%SZ)
EOF

if [[ "$RUNNER_OS" == "macOS" ]]; then
  cat > "$VIEWER_STAGE/README-MACOS.txt" <<'EOF'
Open "OpenMS Viewer.app". No dependency installation is required.

This CI artifact is ad-hoc signed, not Apple-notarized. Depending on
Gatekeeper settings, the first launch may require Control-clicking the
app and choosing Open.
EOF
else
  cat > "$VIEWER_STAGE/README-LINUX.txt" <<'EOF'
Run bin/openms-viewer. No dependency installation is required.

The package targets x64 Linux with a glibc baseline matching Ubuntu
24.04. A desktop X11 or Wayland session is still required.
EOF
fi
