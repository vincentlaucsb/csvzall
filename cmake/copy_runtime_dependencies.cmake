if(POLICY CMP0207)
  cmake_policy(SET CMP0207 NEW)
endif()

if(NOT DEFINED exe OR NOT EXISTS "${exe}")
  message(FATAL_ERROR "Runtime dependency copy requires an existing executable")
endif()

if(NOT DEFINED dest OR NOT IS_DIRECTORY "${dest}")
  message(FATAL_ERROR "Runtime dependency copy requires an existing destination directory")
endif()

set(_runtime_dirs "")
if(DEFINED dirs AND NOT dirs STREQUAL "")
  list(APPEND _runtime_dirs ${dirs})
endif()

file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES "${exe}"
  DIRECTORIES ${_runtime_dirs}
  RESOLVED_DEPENDENCIES_VAR _resolved_deps
  UNRESOLVED_DEPENDENCIES_VAR _unresolved_deps
  PRE_EXCLUDE_REGEXES
    "api-ms-.*"
    "ext-ms-.*"
  POST_EXCLUDE_REGEXES
    ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/[Ss]ystem32/.*"
    ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/[Ss][Yy][Ss][Ww][Oo][Ww]64/.*")

foreach(_dep IN LISTS _resolved_deps)
  file(COPY "${_dep}" DESTINATION "${dest}")
endforeach()

if(_unresolved_deps)
  message(WARNING "Unresolved runtime dependencies for ${exe}: ${_unresolved_deps}")
endif()
