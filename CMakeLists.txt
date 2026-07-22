cmake_minimum_required(VERSION 3.24)

project(OpenMSViewer VERSION 0.2.0 LANGUAGES CXX)

# Dual build modes. Standalone (top-level) resolves OpenMS/Qt and owns its own
# packaging. Embedded — added via add_subdirectory() from a parent build such as
# OpenMS's release pipeline — reuses the parent's already-configured OpenMS/Qt
# targets and lets the parent own install layout, deployment, signing and CPack.
set(OPENMS_VIEWER_STANDALONE ${PROJECT_IS_TOP_LEVEL})

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if(OPENMS_VIEWER_STANDALONE)
  if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
  endif()

  # Development OpenMS exports commonly provide only a Release location.
  set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
  set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG Release)
endif()

# OpenMS is already a target when embedded in the OpenMS build tree; only resolve
# it for standalone builds. Pin to the 3.6 series as a range (OpenMS has no
# cross-minor ABI). Embedded builds link whatever OpenMS the parent provides.
if(NOT TARGET OpenMS)
  find_package(OpenMS 3.6...<3.7 CONFIG REQUIRED)
endif()
# Qt is needed in both modes; find_package is idempotent if the parent already ran it.
find_package(Qt6 6.4 REQUIRED COMPONENTS Concurrent Gui Widgets Svg)

add_library(openms_viewer_core STATIC
  src/export/MzMLExporter.cpp
  src/export/MzMLExporter.h
  src/annotation/SpectrumAnnotation.cpp
  src/annotation/SpectrumAnnotation.h
  src/model/IdentificationData.h
  src/model/FormatRegistry.cpp
  src/model/FormatRegistry.h
  src/layout/LayoutModel.cpp
  src/layout/LayoutModel.h
  src/logging/ApplicationLog.cpp
  src/logging/ApplicationLog.h
  src/model/ImagingDocument.cpp
  src/model/ImagingDocument.h
  src/model/RunData.h
  src/model/RtUnit.h
  src/model/SelectionController.cpp
  src/model/SelectionController.h
  src/model/ViewerDocument.cpp
  src/model/ViewerDocument.h
  src/model/OswStore.cpp
  src/model/OswStore.h
  src/model/Sqlite3Api.h
  src/model/ChromatogramSource.cpp
  src/model/ChromatogramSource.h
  src/model/TraceSmoothing.cpp
  src/model/TraceSmoothing.h
  src/model/OswDocument.cpp
  src/model/OswDocument.h
  src/model/ConsensusDocument.cpp
  src/model/ConsensusDocument.h
  src/model/ConsensusDrilldown.cpp
  src/model/ConsensusDrilldown.h
  src/plot/PeakMapRasterizer.cpp
  src/plot/PeakMapRasterizer.h
  src/plot/PlotAxis.cpp
  src/plot/PlotAxis.h
  src/plot/PlotTheme.cpp
  src/plot/PlotTheme.h
  src/plot/RasterShading.cpp
  src/plot/RasterShading.h
  src/plot/IonMobilityRasterizer.cpp
  src/plot/IonMobilityRasterizer.h
  src/plot/PlotRange.h
)
target_include_directories(openms_viewer_core PUBLIC src)
target_link_libraries(openms_viewer_core
  PUBLIC
    OpenMS
    Qt6::Core
    Qt6::Gui
)

# OpenMS vendors SQLite and its shared library exposes the C symbols on Unix.
# Windows DLL exports are explicit, however, so downstream code using the
# sqlite3 C API must also link the static library produced by the OpenMS build.
# Portable-package CI builds OpenMS from source and passes that exact artifact.
set(OPENMS_VIEWER_SQLITE3_LIBRARY "" CACHE FILEPATH
    "sqlite3 library required by standalone Windows builds")
if(WIN32)
  if(TARGET sqlite3)
    target_link_libraries(openms_viewer_core PUBLIC sqlite3)
  else()
    if(NOT OPENMS_VIEWER_SQLITE3_LIBRARY)
      message(FATAL_ERROR
        "OPENMS_VIEWER_SQLITE3_LIBRARY must name the sqlite3 library used to build OpenMS")
    endif()
    if(NOT EXISTS "${OPENMS_VIEWER_SQLITE3_LIBRARY}")
      message(FATAL_ERROR
        "OPENMS_VIEWER_SQLITE3_LIBRARY does not exist: ${OPENMS_VIEWER_SQLITE3_LIBRARY}")
    endif()
    target_link_libraries(openms_viewer_core PUBLIC "${OPENMS_VIEWER_SQLITE3_LIBRARY}")
  endif()
endif()
target_compile_features(openms_viewer_core PUBLIC cxx_std_23)

add_library(openms_viewer_ui STATIC
  src/icons/icons.qrc
  src/export/PlotExporter.cpp
  src/export/PlotExporter.h
  src/export/MzMLExportDialog.cpp
  src/export/MzMLExportDialog.h
  src/MainWindow.cpp
  src/MainWindow.h
  src/widgets/PeakMapWidget.cpp
  src/widgets/PeakMapWidget.h
  src/widgets/PeakSurface3DWidget.cpp
  src/widgets/PeakSurface3DWidget.h
  src/widgets/RowStackWidget.cpp
  src/widgets/RowStackWidget.h
  src/widgets/ChromatogramPanelWidget.cpp
  src/widgets/ChromatogramPanelWidget.h
  src/widgets/CompactControls.h
  src/widgets/DataLayersWidget.cpp
  src/widgets/DataLayersWidget.h
  src/widgets/FeatureTableWidget.cpp
  src/widgets/FeatureTableWidget.h
  src/widgets/FeatureEditDialog.cpp
  src/widgets/FeatureEditDialog.h
  src/widgets/HelpDialog.cpp
  src/widgets/HelpDialog.h
  src/widgets/TableClipboard.cpp
  src/widgets/TableClipboard.h
  src/widgets/FaimsPanelWidget.cpp
  src/widgets/FaimsPanelWidget.h
  src/widgets/IdentificationTableWidget.cpp
  src/widgets/IdentificationTableWidget.h
  src/widgets/IonMobilityPanelWidget.cpp
  src/widgets/IonMobilityPanelWidget.h
  src/widgets/LogWidget.cpp
  src/widgets/LogWidget.h
  src/widgets/MetadataBrowserWidget.cpp
  src/widgets/MetadataBrowserWidget.h
  src/widgets/LoadingOverlayWidget.cpp
  src/widgets/LoadingOverlayWidget.h
  src/widgets/ImagingPanelWidget.cpp
  src/widgets/ImagingPanelWidget.h
  src/widgets/SpectrumWidget.cpp
  src/widgets/SpectrumWidget.h
  src/widgets/SpectrumTableWidget.cpp
  src/widgets/SpectrumTableWidget.h
  src/widgets/TicWidget.cpp
  src/widgets/TicWidget.h
  src/widgets/TransitionGroupPlot.cpp
  src/widgets/TransitionGroupPlot.h
  src/widgets/OswPanel.cpp
  src/widgets/OswPanel.h
  src/widgets/ConsensusQuantChart.cpp
  src/widgets/ConsensusQuantChart.h
  src/widgets/ConsensusPanel.cpp
  src/widgets/ConsensusPanel.h
  src/widgets/ToastOverlay.cpp
  src/widgets/ToastOverlay.h
  src/widgets/WelcomeWidget.cpp
  src/widgets/WelcomeWidget.h
)
target_include_directories(openms_viewer_ui PUBLIC src)
target_link_libraries(openms_viewer_ui
  PUBLIC
    openms_viewer_core
    Qt6::Concurrent
    Qt6::Widgets
    Qt6::Svg
)

add_executable(openms-viewer src/main.cpp)
target_link_libraries(openms-viewer PRIVATE openms_viewer_ui)

# Portable packages carry their own version-locked OpenMS libOpenMS + share tree.
# In that mode the app forces its bundled data, fails closed if the bundle is
# incomplete, and verifies the loaded libOpenMS is its own (see src/main.cpp).
option(OPENMS_VIEWER_PORTABLE
  "Build a self-contained portable package: force the bundled OpenMS data tree and fail closed if it or the bundled libOpenMS is missing"
  OFF)
if(OPENMS_VIEWER_PORTABLE)
  target_compile_definitions(openms-viewer PRIVATE OPENMS_VIEWER_PORTABLE)
  # dladdr (used to locate the loaded libOpenMS) lives in libdl on Linux.
  target_link_libraries(openms-viewer PRIVATE ${CMAKE_DL_LIBS})
endif()
set_target_properties(openms-viewer PROPERTIES
  WIN32_EXECUTABLE TRUE
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_BUNDLE_NAME "OpenMS Viewer"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.openms.viewer"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
if(APPLE)
  # Use a native, user-facing name for the installed application bundle while
  # keeping the stable CMake target name used by builds and CI.
  set_target_properties(openms-viewer PROPERTIES OUTPUT_NAME "OpenMS Viewer")
endif()

# Optional integration probe for portable packages. It deliberately links only
# OpenMS and is not installed, so CI can copy the executable into an isolated
# directory and prove that the staged native/.NET Thermo stack loads real data.
option(OPENMS_VIEWER_BUILD_THERMO_SMOKE
       "Build the Thermo RAW portable-package smoke probe" OFF)
if(OPENMS_VIEWER_BUILD_THERMO_SMOKE)
  add_executable(openms-viewer-thermo-smoke tools/ThermoRawSmoke.cpp)
  target_link_libraries(openms-viewer-thermo-smoke PRIVATE OpenMS)
endif()

# Developer tool: regenerates docs/screenshots/ from cached archive.openms.de
# datasets + fixtures (see tools/screenshots/). Standalone dev builds only; not
# part of the embedded OpenMS release. Run offscreen from the repo root.
if(OPENMS_VIEWER_STANDALONE)
  add_executable(openms-viewer-gallery tools/screenshots/gallery.cpp)
  target_link_libraries(openms-viewer-gallery PRIVATE openms_viewer_ui)
endif()

if(OPENMS_VIEWER_STANDALONE)
  # GNUInstallDirs is included here (not in embedded mode): it seeds global
  # CMAKE_INSTALL_* cache entries — including CMAKE_INSTALL_DOCDIR derived from
  # PROJECT_NAME — which would otherwise leak into the parent build.
  include(GNUInstallDirs)
  install(TARGETS openms-viewer
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
  )
  install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
  install(FILES LICENSES/Apache-2.0.txt DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses)
  install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR})
else()
  # Embedded: install into the parent's tree under a component it packages (the
  # parent owns Qt/OpenMS deployment, bundling and signing). Default matches the
  # OpenMS "applications" component; the parent may override. Reuse the parent's
  # bindir if it set one, else fall back to "bin" without pulling in
  # GNUInstallDirs (which would pollute the parent's install-path cache).
  set(OPENMS_VIEWER_INSTALL_COMPONENT "applications" CACHE STRING
      "Install component for openms-viewer when embedded in a parent build")
  if(DEFINED CMAKE_INSTALL_BINDIR)
    set(_openms_viewer_bindir "${CMAKE_INSTALL_BINDIR}")
  else()
    set(_openms_viewer_bindir "bin")
  endif()
  # COMPONENT must be attached per artifact kind — a single trailing COMPONENT
  # binds only to the block it follows (e.g. BUNDLE), leaving RUNTIME Unspecified.
  install(TARGETS openms-viewer
    RUNTIME DESTINATION ${_openms_viewer_bindir} COMPONENT ${OPENMS_VIEWER_INSTALL_COMPONENT}
    BUNDLE DESTINATION . COMPONENT ${OPENMS_VIEWER_INSTALL_COMPONENT}
  )
endif()

option(OPENMS_VIEWER_BUILD_TESTS "Build OpenMS Viewer tests" ${OPENMS_VIEWER_STANDALONE})
if(OPENMS_VIEWER_BUILD_TESTS)
  enable_testing()
  find_package(Qt6 6.4 REQUIRED COMPONENTS Test)

  add_executable(openms_viewer_tests
    tests/TestMain.cpp
    tests/LayoutModelTest.cpp
    tests/LayoutPersistenceTest.cpp
    tests/FormatRegistryTest.cpp
    tests/RichFormatsTest.cpp
    tests/OswStoreTest.cpp
    tests/ChromatogramSourceTest.cpp
    tests/PrecursorOverlayTest.cpp
    tests/MetadataBrowserTest.cpp
    tests/WriteBackTest.cpp
    tests/TraceSmoothingTest.cpp
    tests/OswPanelTest.cpp
    tests/ConsensusDocumentTest.cpp
    tests/ConsensusPanelTest.cpp
    tests/ConsensusDrilldownTest.cpp
    tests/MainWindowConsensusWorkflowTest.cpp
    tests/PeakMapRasterizerTest.cpp
    tests/FeatureDocumentTest.cpp
    tests/IdentificationDocumentTest.cpp
    tests/MainWindowFeatureWorkflowTest.cpp
    tests/MainWindowIdentificationWorkflowTest.cpp
    tests/SpectrumAnnotationTest.cpp
    tests/ViewerDocumentTest.cpp
    tests/DataPanelsWorkflowTest.cpp
    tests/MobilityFaimsWorkflowTest.cpp
    tests/ExportWorkflowTest.cpp
    tests/ImagingWorkflowTest.cpp
    tests/ImagingMaldiTest.cpp
    tests/SpectrumInteractionTest.cpp
    tests/SelectionControllerTest.cpp
    tests/LogWidgetTest.cpp
    tests/UxWorkflowTest.cpp
    tests/FeatureVisualizationTest.cpp
  )
  target_link_libraries(openms_viewer_tests
    PRIVATE
      openms_viewer_core
      openms_viewer_ui
      Qt6::Test
  )
  # Real sample data (BSA tryptic digest: raw run + 256 features + 48 IDs) vendored
  # under tests/data, for tests that exercise file loading and feature visualization.
  target_compile_definitions(openms_viewer_tests
    PRIVATE "OPENMS_VIEWER_TEST_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/data\"")
  add_test(NAME openms_viewer_tests COMMAND openms_viewer_tests)
  set_tests_properties(openms_viewer_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  )
endif()

# Standalone packaging only. When embedded, the parent build (e.g. OpenMS) owns
# CPack; running include(CPack) here would clash with the parent's generators.
if(OPENMS_VIEWER_STANDALONE)
  set(CPACK_PACKAGE_NAME "OpenMSViewer")
  set(CPACK_PACKAGE_VENDOR "OpenMS")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Standalone OpenMS mass-spectrometry data viewer")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "OpenMSViewer")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
  set(CPACK_STRIP_FILES TRUE)
  if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "OpenMS Viewer")
    set(CPACK_NSIS_PACKAGE_NAME "OpenMS Viewer")
  elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop;TGZ")
    set(CPACK_DMG_VOLUME_NAME "OpenMS Viewer")
  else()
    set(CPACK_GENERATOR "TGZ;DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "OpenMS developers")
    set(CPACK_DEBIAN_PACKAGE_SECTION "science")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  endif()
  include(CPack)
endif()
