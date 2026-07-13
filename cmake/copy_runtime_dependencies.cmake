if(POLICY CMP0207)
  cmake_policy(SET CMP0207 NEW)
endif()

set(_runtime_dependency_mode "copy")
if(DEFINED mode AND NOT mode STREQUAL "")
  set(_runtime_dependency_mode "${mode}")
endif()

if(NOT DEFINED exe OR NOT EXISTS "${exe}")
  message(FATAL_ERROR "Runtime dependency scan requires an existing executable")
endif()

if(_runtime_dependency_mode STREQUAL "copy" AND (NOT DEFINED dest OR NOT IS_DIRECTORY "${dest}"))
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
    "[Aa][Dd][Vv][Aa][Pp][Ii]32\\.dll"
    "[Bb][Cc][Rr][Yy][Pp][Tt][Pp][Rr][Ii][Mm][Ii][Tt][Ii][Vv][Ee][Ss]\\.dll"
    "[Ii][Pp][Hh][Ll][Pp][Aa][Pp][Ii]\\.dll"
    "[Kk][Ee][Rr][Nn][Ee][Ll]32\\.dll"
    "[Kk][Ee][Rr][Nn][Ee][Ll][Bb][Aa][Ss][Ee]\\.dll"
    "[Nn][Tt][Dd][Ll][Ll]\\.dll"
    "[Oo][Ll][Ee][Aa][Uu][Tt]32\\.dll"
    "[Rr][Pp][Cc][Rr][Tt]4\\.dll"
    "[Ss][Ss][Pp][Ii][Cc][Ll][Ii]\\.dll"
    "[Ww][Ii][Nn][Tt][Rr][Uu][Ss][Tt]\\.dll"
    "[Ww][Ss]2_32\\.dll"
  POST_EXCLUDE_REGEXES
    ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\/][Ss]ystem32[\\/].*"
    ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\/][Ss][Yy][Ss][Ww][Oo][Ww]64[\\/].*"
    "^/lib/.*"
    "^/lib64/.*"
    "^/usr/lib/.*"
    "^/usr/lib64/.*"
    "^/System/Library/.*")

if(_runtime_dependency_mode STREQUAL "assert-no-non-system-shared-libs")
  if(_resolved_deps OR _unresolved_deps)
    list(SORT _resolved_deps)
    list(SORT _unresolved_deps)
    string(REPLACE ";" "\n  " _resolved_report "${_resolved_deps}")
    string(REPLACE ";" "\n  " _unresolved_report "${_unresolved_deps}")
    message(FATAL_ERROR
      "Obsidian helper build must not require non-system runtime shared libraries.\n"
      "Resolved non-system runtime shared libraries:\n  ${_resolved_report}\n"
      "Unresolved runtime dependencies:\n  ${_unresolved_report}\n"
      "Disable the feature that introduced the dependency, or make it static before publishing the Obsidian asset.")
  endif()
  message(STATUS "Obsidian helper runtime dependency check passed: no non-system shared libraries required")
  return()
elseif(NOT _runtime_dependency_mode STREQUAL "copy")
  message(FATAL_ERROR "Unknown runtime dependency mode: ${_runtime_dependency_mode}")
endif()

foreach(_dep IN LISTS _resolved_deps)
  file(COPY "${_dep}" DESTINATION "${dest}")
endforeach()

if(_unresolved_deps)
  message(WARNING "Unresolved runtime dependencies for ${exe}: ${_unresolved_deps}")
endif()
