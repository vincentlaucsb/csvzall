#pragma once

#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace csvzall::pipeline::common {

/// Thrown by MoveIntoPlaceNoReplace when the target path already exists.
class TargetExistsError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

/// Temporary sibling used for safe in-place writes: "<target>.csvzall.tmp".
std::filesystem::path TempSiblingFor(const std::filesystem::path& target_path);

/// Moves temp_path over target_path, replacing an existing target atomically
/// where the platform allows. Throws "<label>: unable to write <target>" on
/// failure.
void ReplaceFileWith(const std::filesystem::path& temp_path,
                     const std::filesystem::path& target_path,
                     std::string_view label);

/// Moves temp_path to target_path only when target_path does not exist.
/// Never overwrites: if the target exists (including one created after an
/// earlier existence check), throws TargetExistsError and leaves both files
/// untouched. Other failures throw "<label>: unable to write <target>".
void MoveIntoPlaceNoReplace(const std::filesystem::path& temp_path,
                            const std::filesystem::path& target_path,
                            std::string_view label);

}  // namespace csvzall::pipeline::common
