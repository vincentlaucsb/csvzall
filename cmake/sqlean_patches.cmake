# csvzall-specific adjustments to sqlean sources, applied by writing patched
# copies into the build tree so the fetched (or user-supplied) checkout stays
# untouched. Each patch fails configure loudly when its anchor text is missing,
# so a sqlean bump cannot silently drop one.
#
#   text/extension.c — skip sqlean's ltrim, rtrim, concat, concat_ws, and
#     octet_length aliases. They are application-defined functions with the
#     same names as SQLite built-ins and would silently replace them (an
#     app-defined function always wins, and SQLite cannot unregister one back
#     to the built-in). The text_ltrim, text_rtrim, text_concat, text_join, and
#     text_size spellings remain.
#   regexp/regexp.c — add PCRE2_MATCH_INVALID_UTF. sqlean compiles patterns in
#     UTF mode, where any invalid UTF-8 byte in the subject makes the whole
#     match fail. With this option a Latin-1 or otherwise non-UTF-8 CSV value
#     still matches on its valid parts; invalid bytes just never match.

function(_csvzall_write_if_changed path content)
  if(EXISTS "${path}")
    file(READ "${path}" _existing)
    if(_existing STREQUAL content)
      return()
    endif()
  endif()
  file(WRITE "${path}" "${content}")
endfunction()

# Writes patched copies under out_dir and sets out_var to a list of
# "<original>;<patched>" pairs flattened as ORIGINAL1 PATCHED1 ORIGINAL2 ...
function(csvzall_patch_sqlean_sources sqlean_src out_dir out_var)
  set(_pairs "")

  # text/extension.c
  set(_text_src "${sqlean_src}/text/extension.c")
  file(READ "${_text_src}" _text)
  foreach(_alias concat_ws concat ltrim rtrim octet_length)
    # Matches stop before the trailing semicolon: CMake would split a match on it.
    set(_pattern "\n[ \t]*sqlite3_create_function\\(db, \"${_alias}\",[^\n;]*\\)")
    string(REGEX MATCHALL "${_pattern}" _matches "${_text}")
    list(LENGTH _matches _count)
    if(NOT _count EQUAL 1)
      message(FATAL_ERROR
        "sqlean patch: expected one '${_alias}' registration in ${_text_src}, found ${_count}. "
        "Review cmake/sqlean_patches.cmake for this sqlean version.")
    endif()
    string(REGEX REPLACE "${_pattern};"
      "\n    /* csvzall: '${_alias}' alias removed so the SQLite built-in stays in effect. */"
      _text "${_text}")
  endforeach()
  set(_text_out "${out_dir}/text/extension.c")
  file(MAKE_DIRECTORY "${out_dir}/text")
  _csvzall_write_if_changed("${_text_out}" "${_text}")
  list(APPEND _pairs "${_text_src}" "${_text_out}")

  # regexp/regexp.c
  set(_regexp_src "${sqlean_src}/regexp/regexp.c")
  file(READ "${_regexp_src}" _regexp)
  set(_options "uint32_t options = PCRE2_UCP | PCRE2_UTF;")
  string(REGEX MATCHALL "uint32_t options = PCRE2_UCP \\| PCRE2_UTF" _option_matches "${_regexp}")
  list(LENGTH _option_matches _option_count)
  if(NOT _option_count EQUAL 2)
    message(FATAL_ERROR
      "sqlean patch: expected two PCRE2 compile option lines in ${_regexp_src}, found ${_option_count}. "
      "Review cmake/sqlean_patches.cmake for this sqlean version.")
  endif()
  string(REPLACE "${_options}"
    "uint32_t options = PCRE2_UCP | PCRE2_UTF | PCRE2_MATCH_INVALID_UTF; /* csvzall */"
    _regexp "${_regexp}")
  set(_regexp_out "${out_dir}/regexp/regexp.c")
  file(MAKE_DIRECTORY "${out_dir}/regexp")
  _csvzall_write_if_changed("${_regexp_out}" "${_regexp}")
  list(APPEND _pairs "${_regexp_src}" "${_regexp_out}")

  set(${out_var} "${_pairs}" PARENT_SCOPE)
endfunction()
