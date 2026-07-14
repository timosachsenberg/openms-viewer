cmake_minimum_required(VERSION 3.24)

foreach(_required IN ITEMS BUNDLE SEARCH_DIRECTORIES)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "${_required} must be passed with -D${_required}=...")
  endif()
endforeach()

if(NOT APPLE)
  message(FATAL_ERROR "FixupMacBundle.cmake must run on macOS")
endif()
if(NOT IS_DIRECTORY "${BUNDLE}")
  message(FATAL_ERROR "Application bundle does not exist: ${BUNDLE}")
endif()

set(_search_directories)
foreach(_directory IN LISTS SEARCH_DIRECTORIES)
  if(IS_DIRECTORY "${_directory}")
    list(APPEND _search_directories "${_directory}")
  endif()
endforeach()
if(NOT _search_directories)
  message(FATAL_ERROR "None of the bundle dependency search directories exist")
endif()

# macdeployqt installs the normal Qt plugins. Include every deployed plugin in
# BundleUtilities' fixup pass, plus optional probes copied into Contents/MacOS,
# so their transitive dependencies are rewritten along with the main app.
file(GLOB_RECURSE _plugins LIST_DIRECTORIES FALSE
  "${BUNDLE}/Contents/PlugIns/*")
if(DEFINED EXTRA_ITEMS AND NOT "${EXTRA_ITEMS}" STREQUAL "")
  list(APPEND _plugins ${EXTRA_ITEMS})
endif()

set(BU_CHMOD_BUNDLE_ITEMS ON)
include(BundleUtilities)
fixup_bundle("${BUNDLE}" "${_plugins}" "${_search_directories}")
verify_app("${BUNDLE}")
