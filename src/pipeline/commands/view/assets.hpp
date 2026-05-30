#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "../../../../vendor/httplib/httplib.h"

namespace csvzall::pipeline::commands::view_internal {

std::filesystem::path ResolveDevViewerAssetDir(const std::string& value);
bool ServeEmbeddedViewerAsset(std::string_view path, httplib::Response& response);
bool ServeViewerAsset(const std::filesystem::path& dev_asset_dir,
                      std::string_view path,
                      httplib::Response& response);

}  // namespace csvzall::pipeline::commands::view_internal
