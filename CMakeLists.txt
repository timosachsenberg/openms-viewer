cmake_minimum_required(VERSION 3.24)

project(OpenMSViewer VERSION 0.1.0 LANGUAGES CXX)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Development OpenMS exports commonly provide only a Release location.
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG Release)

find_package(OpenMS CONFIG REQUIRED)
find_package(Qt6 6.4 REQUIRED COMPONENTS Concurrent Gui Widgets)

add_library(openms_viewer_core STATIC
  src/export/MzMLExporter.cpp
  src/export/MzMLExporter.h
  src/annotation/SpectrumAnnotation.cpp
  src/annotation/SpectrumAnnotation.h
  src/model/IdentificationData.h
  src/logging/ApplicationLog.cpp
  src/logging/ApplicationLog.h
  src/model/ImagingDocument.cpp
  src/model/ImagingDocument.h
  src/model/RunData.h
  src/model/SelectionController.cpp
  src/model/SelectionController.h
  src/model/ViewerDocument.cpp
  src/model/ViewerDocument.h
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
  src/widgets/ChromatogramPanelWidget.cpp
  src/widgets/ChromatogramPanelWidget.h
  src/widgets/FeatureTableWidget.cpp
  src/widgets/FeatureTableWidget.h
  src/widgets/FaimsPanelWidget.cpp
  src/widgets/FaimsPanelWidget.h
  src/widgets/IdentificationTableWidget.cpp
  src/widgets/IdentificationTableWidget.h
  src/widgets/IonMobilityPanelWidget.cpp
  src/widgets/IonMobilityPanelWidget.h
  src/widgets/LogWidget.cpp
  src/widgets/LogWidget.h
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

include(GNUInstallDirs)
install(TARGETS openms-viewer
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  BUNDLE DESTINATION .
)
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR})

option(OPENMS_VIEWER_BUILD_TESTS "Build OpenMS Viewer tests" ON)
if(OPENMS_VIEWER_BUILD_TESTS)
  enable_testing()
  find_package(Qt6 6.4 REQUIRED COMPONENTS Test)

  add_executable(openms_viewer_tests
    tests/TestMain.cpp
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
