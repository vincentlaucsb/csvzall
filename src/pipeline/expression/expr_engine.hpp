#pragma once

#include "../../transform_pipeline.hpp"

#include <csv.hpp>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <exprtk.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <optional>
#include <string>
#include <vector>

namespace csvzall::pipeline::expression {

enum class NumericParseMode {
  Strict,
  EmptyOrInvalidAsZero,
};

struct ExprContext {
  std::vector<std::string> variable_names;
  std::vector<std::size_t> column_indexes;
  std::vector<double> variable_values;
  exprtk::symbol_table<double> symbol_table;
  exprtk::expression<double> expression;
};

std::optional<ExprContext> CompileExpression(const std::string& expression_text,
                                             const std::vector<std::string>& headers,
                                             const RunOptions& options,
                                             const LoggerCallbacks& logger);

bool BindRowValues(const csv::CSVRow& row, ExprContext& ctx,
                   std::size_t row_number, const LoggerCallbacks& logger,
                   NumericParseMode parse_mode);

}  // namespace csvzall::pipeline::expression
