#pragma once

#include "../../transform_pipeline.hpp"

#include <csv.hpp>

#include <string>
#include <vector>

namespace csvzall::pipeline::common {

std::string DoubleToString(double value);

void AccumulateRowBytes(const csv::CSVRow& row, RunStats& stats);

}  // namespace csvzall::pipeline::common
