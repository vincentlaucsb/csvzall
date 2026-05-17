cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED OUTPUT_CPP OR NOT DEFINED OUTPUT_HPP)
  message(FATAL_ERROR "OUTPUT_CPP and OUTPUT_HPP are required")
endif()
if(NOT DEFINED VIEWER_SOURCE_DIR OR NOT DEFINED AG_GRID_SOURCE_DIR OR NOT DEFINED POPRIGHT_SOURCE_DIR)
  message(FATAL_ERROR "VIEWER_SOURCE_DIR, AG_GRID_SOURCE_DIR, and POPRIGHT_SOURCE_DIR are required")
endif()

set(_assets
  "/|${VIEWER_SOURCE_DIR}/index.html|text/html|index_html"
  "/assets/viewer.css|${VIEWER_SOURCE_DIR}/viewer.css|text/css|viewer_css"
  "/assets/viewer.js|${VIEWER_SOURCE_DIR}/viewer.js|application/javascript|viewer_js"
  "/assets/ag-grid-community.min.js|${AG_GRID_SOURCE_DIR}/ag-grid-community.min.js|application/javascript|ag_grid_js"
  "/assets/ag-grid.css|${AG_GRID_SOURCE_DIR}/ag-grid.css|text/css|ag_grid_css"
  "/assets/ag-theme-alpine.css|${AG_GRID_SOURCE_DIR}/ag-theme-alpine.css|text/css|ag_theme_css"
  "/assets/popright/styles.css|${POPRIGHT_SOURCE_DIR}/styles.css|text/css|popright_styles_css"
  "/assets/popright/constants.js|${POPRIGHT_SOURCE_DIR}/constants.js|application/javascript|popright_constants_js"
  "/assets/popright/ContextMenu.js|${POPRIGHT_SOURCE_DIR}/ContextMenu.js|application/javascript|popright_context_menu_js"
  "/assets/popright/index.js|${POPRIGHT_SOURCE_DIR}/index.js|application/javascript|popright_index_js"
  "/assets/popright/MenuController.js|${POPRIGHT_SOURCE_DIR}/MenuController.js|application/javascript|popright_menu_controller_js"
  "/assets/popright/positioning.js|${POPRIGHT_SOURCE_DIR}/positioning.js|application/javascript|popright_positioning_js"
  "/assets/popright/render.js|${POPRIGHT_SOURCE_DIR}/render.js|application/javascript|popright_render_js"
  "/assets/popright/theme-utils.js|${POPRIGHT_SOURCE_DIR}/theme-utils.js|application/javascript|popright_theme_utils_js"
  "/assets/popright/theme.js|${POPRIGHT_SOURCE_DIR}/theme.js|application/javascript|popright_theme_js"
  "/assets/popright/ThemeStore.js|${POPRIGHT_SOURCE_DIR}/ThemeStore.js|application/javascript|popright_theme_store_js"
  "/assets/popright/types.js|${POPRIGHT_SOURCE_DIR}/types.js|application/javascript|popright_types_js"
  "/assets/popright/utils.js|${POPRIGHT_SOURCE_DIR}/utils.js|application/javascript|popright_utils_js")

set(_hpp "#pragma once\n\n#include <cstddef>\n#include <string>\n#include <string_view>\n\nnamespace csvzall::pipeline::commands {\n\nstruct EmbeddedViewerAsset {\n  std::string_view path;\n  std::string_view content_type;\n  const std::string_view* chunks;\n  std::size_t chunk_count;\n};\n\nconst EmbeddedViewerAsset* FindEmbeddedViewerAsset(std::string_view path);\nstd::string EmbeddedViewerAssetText(const EmbeddedViewerAsset& asset);\n\n}  // namespace csvzall::pipeline::commands\n")

set(_cpp "#include \"viewer_assets.hpp\"\n\nnamespace csvzall::pipeline::commands {\n\nnamespace {\n")
set(_asset_entries "")
set(_asset_index 0)
set(_chunk_size 12000)

foreach(_asset IN LISTS _assets)
  string(REPLACE "|" ";" _parts "${_asset}")
  list(GET _parts 0 _route)
  list(GET _parts 1 _path)
  list(GET _parts 2 _content_type)
  list(GET _parts 3 _symbol)

  if(NOT EXISTS "${_path}")
    message(FATAL_ERROR "viewer asset not found: ${_path}")
  endif()

  file(READ "${_path}" _content)
  string(LENGTH "${_content}" _content_len)
  string(APPEND _cpp "constexpr std::string_view k_${_symbol}_chunks[] = {\n")
  set(_offset 0)
  set(_chunk_index 0)
  while(_offset LESS _content_len)
    set(_take ${_chunk_size})
    math(EXPR _remaining "${_content_len} - ${_offset}")
    if(_remaining LESS _take)
      set(_take ${_remaining})
    endif()
    string(SUBSTRING "${_content}" ${_offset} ${_take} _chunk)
    set(_delimiter "CZ${_asset_index}_${_chunk_index}")
    string(FIND "${_chunk}" ")${_delimiter}\"" _delimiter_collision)
    if(NOT _delimiter_collision EQUAL -1)
      message(FATAL_ERROR "raw string delimiter collision in viewer asset: ${_path}")
    endif()
    string(APPEND _cpp "  R\"${_delimiter}(${_chunk})${_delimiter}\",\n")
    math(EXPR _offset "${_offset} + ${_take}")
    math(EXPR _chunk_index "${_chunk_index} + 1")
  endwhile()
  string(APPEND _cpp "};\n\n")
  string(APPEND _asset_entries "  {\"${_route}\", \"${_content_type}\", k_${_symbol}_chunks, sizeof(k_${_symbol}_chunks) / sizeof(k_${_symbol}_chunks[0])},\n")
  math(EXPR _asset_index "${_asset_index} + 1")
endforeach()

string(APPEND _cpp "constexpr EmbeddedViewerAsset kAssets[] = {\n${_asset_entries}};\n\n}  // namespace\n\nconst EmbeddedViewerAsset* FindEmbeddedViewerAsset(std::string_view path) {\n  for (const auto& asset : kAssets) {\n    if (asset.path == path) {\n      return &asset;\n    }\n  }\n  return nullptr;\n}\n\nstd::string EmbeddedViewerAssetText(const EmbeddedViewerAsset& asset) {\n  std::size_t size = 0;\n  for (std::size_t i = 0; i < asset.chunk_count; ++i) {\n    size += asset.chunks[i].size();\n  }\n  std::string text;\n  text.reserve(size);\n  for (std::size_t i = 0; i < asset.chunk_count; ++i) {\n    text.append(asset.chunks[i]);\n  }\n  return text;\n}\n\n}  // namespace csvzall::pipeline::commands\n")

file(WRITE "${OUTPUT_HPP}" "${_hpp}")
file(WRITE "${OUTPUT_CPP}" "${_cpp}")
