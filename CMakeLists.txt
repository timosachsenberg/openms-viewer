cmake_minimum_required(VERSION 3.24)

project(OpenMSViewer VERSION 0.1.0 LANGUAGES CXX)

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
find_package(Qt6 6.4 REQUIRED COMPONENTS Concurrent Gui Widgets)

add_library(openms_viewer_core STATIC
  src/export/MzMLExporter.cpp
  src/export/MzMLExporter.h
  src/annotation/SpectrumAnnotation.cpp
  src/annotation/SpectrumAnnotation.h
  src/model/IdentificationData.h
  src/model/FormatRegistry.cpp
  src/model/FormatRegistry.h
  src/logging/ApplicationLog.cpp
  src/logging/ApplicationLog.h
  src/model/ImagingDocument.cpp
  src/model/ImagingDocument.h
  src/model/RunData.h
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
target_compile_features(openms_viewer_core PUBLIC cxx_std_23)

add_library(openms_viewer_ui STATIC
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
  src/widgets/ChromatogramPanelWidget.cpp
  src/widgets/ChromatogramPanelWidget.h
  src/widgets/FeatureTableWidget.cpp
  src/widgets/FeatureTableWidget.h
  src/widgets/FeatureEditDialog.cpp
  src/widgets/FeatureEditDialog.h
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
)

add_executable(openms-viewer src/main.cpp)
target_link_libraries(openms-viewer PRIVATE openms_viewer_ui)
set_target_properties(openms-viewer PROPERTIES
  WIN32_EXECUTABLE TRUE
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_BUNDLE_NAME "OpenMS Viewer"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.openms.viewer"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

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
  install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
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
    tests/SpectrumInteractionTest.cpp
    tests/LogWidgetTest.cpp
    tests/UxWorkflowTest.cpp
  )
  target_link_libraries(openms_viewer_tests
    PRIVATE
      openms_viewer_core
      openms_viewer_ui
      Qt6::Test
  )
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
