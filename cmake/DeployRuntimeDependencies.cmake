cmake_minimum_required(VERSION 3.24)

foreach(_required IN ITEMS DESTINATION SEARCH_DIRECTORIES)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "${_required} must be passed with -D${_required}=...")
  endif()
endforeach()

if((NOT DEFINED EXECUTABLE OR "${EXECUTABLE}" STREQUAL "")
   AND (NOT DEFINED LIBRARIES OR "${LIBRARIES}" STREQUAL ""))
  message(FATAL_ERROR "At least one of EXECUTABLE or LIBRARIES must be passed")
endif()

set(_dependency_roots)
if(DEFINED EXECUTABLE AND NOT "${EXECUTABLE}" STREQUAL "")
  if(NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "Executable does not exist: ${EXECUTABLE}")
  endif()
  list(APPEND _dependency_roots EXECUTABLES "${EXECUTABLE}")
endif()

if(DEFINED LIBRARIES AND NOT "${LIBRARIES}" STREQUAL "")
  foreach(_library IN LISTS LIBRARIES)
    if(NOT EXISTS "${_library}")
      message(FATAL_ERROR "Runtime library does not exist: ${_library}")
    endif()
  endforeach()
  list(APPEND _dependency_roots LIBRARIES ${LIBRARIES})
endif()

file(MAKE_DIRECTORY "${DESTINATION}")

set(_search_directories)
foreach(_directory IN LISTS SEARCH_DIRECTORIES)
  if(IS_DIRECTORY "${_directory}")
    list(APPEND _search_directories "${_directory}")
  endif()
endforeach()

if(NOT _search_directories)
  message(FATAL_ERROR "None of the runtime dependency search directories exist")
endif()

if(WIN32)
  # Windows API sets and these system/CRT DLLs are supplied by Windows or by
  # the explicit app-local CRT deployment in the packaging workflow.
  set(_pre_excludes
    "api-ms-.*"
    "ext-ms-.*"
    "hvsi.*"
    "pdmutilities.*"
    "vcruntime.*"
    "msvcp.*"
    "concrt.*"
    "vccorlib.*"
    "ucrtbase.*"
  )
  set(_post_excludes
    ".*[\\/][Ss][Yy][Ss][Tt][Ee][Mm]32[\\/].*"
  )
elseif(APPLE)
  set(_pre_excludes)
  set(_post_excludes "^/usr/lib/.*" "^/System/.*")
else()
  # Exclude the glibc/loader ABI surface before resolution. In particular,
  # linux-vdso has no filesystem path and would otherwise be reported as an
  # unresolved portable dependency.
  set(_pre_excludes
    "^linux-vdso.*"
    "^ld-linux-.*"
    "^libc\\.so.*"
    "^libdl\\.so.*"
    "^libm\\.so.*"
    "^libpthread\\.so.*"
  )
  set(_post_excludes
    ".*/ld-linux-.*"
    ".*/linux-vdso.*"
    ".*/libc\\.so.*"
    ".*/libdl\\.so.*"
    ".*/libm\\.so.*"
    ".*/libpthread\\.so.*"
  )
endif()

file(GET_RUNTIME_DEPENDENCIES
  ${_dependency_roots}
  DIRECTORIES ${_search_directories}
  RESOLVED_DEPENDENCIES_VAR _resolved_dependencies
  UNRESOLVED_DEPENDENCIES_VAR _unresolved_dependencies
  PRE_EXCLUDE_REGEXES ${_pre_excludes}
  POST_EXCLUDE_REGEXES ${_post_excludes}
)

list(REMOVE_DUPLICATES _resolved_dependencies)
foreach(_dependency IN LISTS _resolved_dependencies)
  get_filename_component(_name "${_dependency}" NAME)
  file(COPY_FILE
    "${_dependency}"
    "${DESTINATION}/${_name}"
    ONLY_IF_DIFFERENT
  )
endforeach()

list(LENGTH _resolved_dependencies _dependency_count)
message(STATUS "Deployed ${_dependency_count} runtime dependencies to ${DESTINATION}")

if(_unresolved_dependencies)
  list(REMOVE_DUPLICATES _unresolved_dependencies)
  list(JOIN _unresolved_dependencies "\n  " _unresolved_list)
  message(FATAL_ERROR "Unresolved runtime dependencies:\n  ${_unresolved_list}")
endif()
