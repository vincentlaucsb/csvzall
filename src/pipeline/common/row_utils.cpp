#include "row_utils.hpp"

#include <iomanip>
#include <sstream>

namespace csvzall::pipeline::common {

std::string DoubleToString(double value) {
  std::ostringstream oss;
  oss << std::setprecision(15) << value;
  return oss.str();
}

void AccumulateRowBytes(const csv::CSVRow& row, RunStats& stats) {
  for (std::size_t i = 0; i < row.size(); ++i) {
    stats.bytes_processed += static_cast<std::uint64_t>(row[i].get<std::string>().size());
  }
}

}  // namespace csvzall::pipeline::common
