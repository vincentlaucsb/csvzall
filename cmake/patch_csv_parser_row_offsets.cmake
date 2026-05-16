if(NOT DEFINED CSV_PARSER_SOURCE_DIR)
  message(FATAL_ERROR "CSV_PARSER_SOURCE_DIR is required")
endif()

set(_csv_parser_root "${CSV_PARSER_SOURCE_DIR}")

set(_raw_csv_data "${_csv_parser_root}/include/internal/raw_csv_data.hpp")
set(_csv_row "${_csv_parser_root}/include/internal/csv_row.hpp")

if(NOT EXISTS "${_raw_csv_data}" OR NOT EXISTS "${_csv_row}")
  message(FATAL_ERROR "csv-parser headers not found under ${_csv_parser_root}")
endif()

# RawCSVData source_start. Handles both current csv-parser and older in-repo layouts.
file(READ "${_raw_csv_data}" _raw_content)
if(NOT _raw_content MATCHES "source_start")
  if(_raw_content MATCHES "internals::RawCSVFieldList fields;")
    string(REPLACE
      "            csv::string_view data = \"\";\n\n            internals::RawCSVFieldList fields;"
      "            csv::string_view data = \"\";\n\n            /** Absolute byte offset where this data chunk starts in the source. */\n            size_t source_start = 0;\n\n            internals::RawCSVFieldList fields;"
      _raw_content "${_raw_content}")
  else()
    string(REPLACE
      "            csv::string_view data = \"\";\n\n            internals::CSVFieldList fields;"
      "            csv::string_view data = \"\";\n\n            /** Absolute byte offset where this data chunk starts in the source. */\n            size_t source_start = 0;\n\n            internals::CSVFieldList fields;"
      _raw_content "${_raw_content}")
  endif()
  file(WRITE "${_raw_csv_data}" "${_raw_content}")
  message(STATUS "Patched csv-parser RawCSVData::source_start")
endif()

# CSVRow::byte_offset().
file(READ "${_csv_row}" _row_content)
if(NOT _row_content MATCHES "byte_offset\\(")
  string(REPLACE
    "        /** Return the number of fields in this row */\n        CONSTEXPR size_t size() const noexcept { return row_length; }\n"
    "        /** Return the number of fields in this row */\n        CONSTEXPR size_t size() const noexcept { return row_length; }\n\n        /** Return the absolute byte offset where this row starts in the source. */\n        size_t byte_offset() const noexcept {\n            if (!this->data) {\n                return 0;\n            }\n            return this->data->source_start + this->data_start;\n        }\n"
    _row_content "${_row_content}")
  file(WRITE "${_csv_row}" "${_row_content}")
  message(STATUS "Patched csv-parser CSVRow::byte_offset()")
endif()

set(_new_core "${_csv_parser_root}/include/internal/parser/core.hpp")
set(_new_orchestrator "${_csv_parser_root}/include/internal/parser/orchestrator.hpp")
set(_new_parallel "${_csv_parser_root}/include/internal/speculative/parallel_parser.hpp")
set(_new_chunks "${_csv_parser_root}/include/internal/speculative/chunks.hpp")

if(EXISTS "${_new_core}" AND EXISTS "${_new_orchestrator}")
  file(READ "${_new_core}" _core)
  if(NOT _core MATCHES "source_start = 0")
    string(REPLACE
      "            explicit ParserChunkOptions(ParserDFAState initial_state, bool scan_bom = true) noexcept\n                : initial_state(initial_state), scan_bom(scan_bom) {}\n\n            ParserDFAState initial_state;\n            bool scan_bom = true;"
      "            explicit ParserChunkOptions(ParserDFAState initial_state, bool scan_bom = true, size_t source_start = 0) noexcept\n                : initial_state(initial_state), scan_bom(scan_bom), source_start(source_start) {}\n\n            ParserDFAState initial_state;\n            bool scan_bom = true;\n            size_t source_start = 0;"
      _core "${_core}")
  endif()
  if(NOT _core MATCHES "data_ptr_->source_start = options.source_start")
    string(REPLACE
      "                this->data_ptr_->data = chunk;\n"
      "                this->data_ptr_->data = chunk;\n                this->data_ptr_->source_start = options.source_start;\n"
      _core "${_core}")
  endif()
  if(NOT _core MATCHES "RowSink& output,[ \t\r\n]+size_t source_start")
    string(REPLACE
      "            ParserChunkResult parse_chunk(\n                csv::string_view chunk,\n                std::shared_ptr<void> owner,\n                RowSink& output,\n                const ParserChunkOptions& options\n            ) {"
      "            ParserChunkResult parse_chunk(\n                csv::string_view chunk,\n                std::shared_ptr<void> owner,\n                RowSink& output,\n                size_t source_start\n            ) {\n                return this->parse_chunk(\n                    chunk,\n                    std::move(owner),\n                    output,\n                    ParserChunkOptions(this->initial_state_, true, source_start)\n                );\n            }\n\n            ParserChunkResult parse_chunk(\n                csv::string_view chunk,\n                std::shared_ptr<void> owner,\n                RowSink& output,\n                const ParserChunkOptions& options\n            ) {"
      _core "${_core}")
  endif()
  file(WRITE "${_new_core}" "${_core}")
  message(STATUS "Patched csv-parser parser core row source offsets")

  file(READ "${_new_orchestrator}" _orchestrator)
  if(NOT _orchestrator MATCHES "parse_serial_window\\(\\n                    chunk,\\n                    std::move\\(owner\\),\\n                    base_offset,")
    string(REPLACE
      "                return this->parse_serial_window(\n                    chunk,\n                    std::move(owner),\n                    source_exhausted,\n                    output\n                );"
      "                return this->parse_serial_window(\n                    chunk,\n                    std::move(owner),\n                    base_offset,\n                    source_exhausted,\n                    output\n                );"
      _orchestrator "${_orchestrator}")
  endif()
  string(REPLACE
    "                std::shared_ptr<void> owner,\n                bool source_exhausted,"
    "                std::shared_ptr<void> owner,\n                size_t base_offset,\n                bool source_exhausted,"
    _orchestrator "${_orchestrator}")
  string(REPLACE
    "                    std::move(owner),\n                    output\n                );"
    "                    std::move(owner),\n                    output,\n                    base_offset\n                );"
    _orchestrator "${_orchestrator}")
  file(WRITE "${_new_orchestrator}" "${_orchestrator}")
  message(STATUS "Patched csv-parser parser orchestrator row source offsets")

  if(EXISTS "${_new_parallel}")
    file(READ "${_new_parallel}" _parallel)
    string(REPLACE
      "                    ParserChunkOptions(chunk.speculation.assumed_start_state, chunk.scan_bom)"
      "                    ParserChunkOptions(chunk.speculation.assumed_start_state, chunk.scan_bom, chunk.offset)"
      _parallel "${_parallel}")
    file(WRITE "${_new_parallel}" "${_parallel}")
    message(STATUS "Patched csv-parser speculative parser row source offsets")
  endif()

  if(EXISTS "${_new_chunks}")
    file(READ "${_new_chunks}" _chunks)
    string(REPLACE
      "                ParserChunkOptions(ParserDFAState(), false)"
      "                ParserChunkOptions(ParserDFAState(), false, fragment.offset)"
      _chunks "${_chunks}")
    string(REPLACE
      "                ParserChunkOptions(corrected_initial_state, chunk.scan_bom)"
      "                ParserChunkOptions(corrected_initial_state, chunk.scan_bom, chunk.offset)"
      _chunks "${_chunks}")
    file(WRITE "${_new_chunks}" "${_chunks}")
    message(STATUS "Patched csv-parser speculative chunk repair row source offsets")
  endif()
else()
  # Older csv-parser layout used by the in-repo fallback.
  set(_old_parser_hpp "${_csv_parser_root}/include/internal/basic_csv_parser.hpp")
  set(_old_parser_cpp "${_csv_parser_root}/include/internal/basic_csv_parser.cpp")
  if(EXISTS "${_old_parser_hpp}")
    file(READ "${_old_parser_hpp}" _old_hpp)
    if(NOT _old_hpp MATCHES "data_ptr->source_start = offset")
      string(REPLACE
        "                size_t length = std::min(source_size - stream_pos, bytes);\n                std::unique_ptr<char[]> buff(new char[length]);"
        "                size_t length = std::min(source_size - stream_pos, bytes);\n                const size_t offset = stream_pos;\n                std::unique_ptr<char[]> buff(new char[length]);"
        _old_hpp "${_old_hpp}")
      string(REPLACE
        "                ((std::string*)(this->data_ptr->_data.get()))->assign(buff.get(), length);\n\n                // Create string_view"
        "                ((std::string*)(this->data_ptr->_data.get()))->assign(buff.get(), length);\n                this->data_ptr->source_start = offset;\n\n                // Create string_view"
        _old_hpp "${_old_hpp}")
      file(WRITE "${_old_parser_hpp}" "${_old_hpp}")
      message(STATUS "Patched csv-parser stream parser row source offsets")
    endif()
  endif()
  if(EXISTS "${_old_parser_cpp}")
    file(READ "${_old_parser_cpp}" _old_cpp)
    if(NOT _old_cpp MATCHES "data_ptr->source_start = offset")
      string(REPLACE
        "            this->data_ptr->_data = std::make_shared<mio::basic_mmap_source<char>>(std::move(mmap));\n            this->mmap_pos += length;"
        "            this->data_ptr->_data = std::make_shared<mio::basic_mmap_source<char>>(std::move(mmap));\n            this->data_ptr->source_start = offset;\n            this->mmap_pos += length;"
        _old_cpp "${_old_cpp}")
      file(WRITE "${_old_parser_cpp}" "${_old_cpp}")
      message(STATUS "Patched csv-parser mmap parser row source offsets")
    endif()
  endif()
endif()
