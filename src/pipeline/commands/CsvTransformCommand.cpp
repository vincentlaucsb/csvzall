#include "commands.hpp"

namespace csvzall::pipeline::commands {

CsvTransformCommand::CsvTransformCommand(std::istream& input, std::ostream& output,
                                         const RunOptions& options,
                                         const LoggerCallbacks& logger, RunStats& stats)
    : CsvInputCommand(input, options, logger, stats), output_(output) {}

}  // namespace csvzall::pipeline::commands
