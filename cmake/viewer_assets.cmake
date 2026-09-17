# A target always runs the inexpensive content check before consumers compile.
# BYPRODUCTS lets Ninja restat the outputs, avoiding recompilation on a no-op build.
function(csvzall_add_viewer_assets target source_dir generated_dir)
  add_custom_target(${target}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${generated_dir}"
    COMMAND "${CMAKE_COMMAND}"
      "-DOUTPUT_CPP=${generated_dir}/viewer_assets.cpp"
      "-DOUTPUT_HPP=${generated_dir}/viewer_assets.hpp"
      "-DVIEWER_SOURCE_DIR=${source_dir}/src/viewer"
      "-DAG_GRID_SOURCE_DIR=${source_dir}/vendor/ag-grid"
      "-DPOPRIGHT_SOURCE_DIR=${source_dir}/vendor/popright/dist"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/embed_viewer_assets.cmake"
    BYPRODUCTS "${generated_dir}/viewer_assets.cpp"
      "${generated_dir}/viewer_assets.hpp" "${generated_dir}/viewer_assets.cpp.sha256"
    VERBATIM)
endfunction()
