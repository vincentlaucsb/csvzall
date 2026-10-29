set(CSVZALL_CORE_SOURCES
  ${CSVZALL_VIEWER_ASSET_CPP}
  ${CSVZALL_VIEWER_ASSET_HPP}
  ${CMAKE_CURRENT_SOURCE_DIR}/src/credentials.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/util.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/transform_pipeline.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/charts/chart_schema.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/charts/chart_spec.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/common/column_lookup.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/common/file_replace.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/common/flexible_date.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/common/gzip_stream.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/common/json_path.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/common/markdown_calendar.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/common/markdown_table.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/common/row_utils.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/sqlite/sqlite_db.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/sqlite/csv_loader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/sqlite/sql_functions.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/viewer_core/indexed_csv.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/append.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/CsvInputCommand.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/CsvTransformCommand.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/derive.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/filter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/json_extract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/max.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/sql_query.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/sql_table_load.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/sql_export.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/summarize.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/timeseries.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/view.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/view/assets.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/view/charts.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/view/json.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/view/model.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/view/server.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/view/sql.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/view/support.cpp)

# Keep the standalone source order intact. The helper never creates object
# files for commands outside its viewer/chart/SQL profile.
if(CSVZALL_OBSIDIAN_BUILD)
  list(REMOVE_ITEM CSVZALL_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/credentials.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/common/json_path.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/common/markdown_calendar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/append.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/derive.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/filter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/json_extract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/max.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/sql_export.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/summarize.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/timeseries.cpp
  )
endif()

if(CSVZALL_HAVE_SVGPLOT)
  list(APPEND CSVZALL_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/charts/bar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/charts/csv_chart_common.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/charts/heatmap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/charts/line.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/charts/validation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/bar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/chart.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/charts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/heatmap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/line.cpp)
endif()

if(CSVZALL_HAVE_POSTGRESQL)
  list(APPEND CSVZALL_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/postgres/exporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/postgres/postgres_connection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/postgres/schema_inference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/postgres/row_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/commands/postgres_export.cpp)
endif()
