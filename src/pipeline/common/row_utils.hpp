#pragma once

#include "../../pipeline_types.hpp"

#include <csv.hpp>

#include <string>
#include <vector>

namespace csvzall::pipeline::common {

std::string DoubleToString(double value);

void AccumulateRowBytes(const csv::CSVRow& row, RunStats& stats);

}  // namespace csvzall::pipeline::common
