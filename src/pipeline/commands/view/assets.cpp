#include "assets.hpp"

#include "viewer_assets.hpp"

#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace csvzall::pipeline::commands::view_internal {
bool ServeEmbeddedViewerAsset(std::string_view path, httplib::Response& response) {
  const auto* asset = FindEmbeddedViewerAsset(path);
  if (!asset) {
    return false;
  }
  response.set_content(
      EmbeddedViewerAssetText(*asset),
      std::string(asset->content_type));
  return true;
}

std::string ReadDevViewerAsset(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("unable to open viewer asset: " + path.string());
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

bool IsSafeViewerModuleRoute(std::string_view route) {
  constexpr std::string_view prefix = "/assets/modules/";
  if (!route.starts_with(prefix) || (!route.ends_with(".js") && !route.ends_with(".mjs"))) {
    return false;
  }
  const auto name = route.substr(prefix.size());
  if (name.empty()) {
    return false;
  }
  for (const char ch : name) {
    const bool allowed =
        (ch >= 'A' && ch <= 'Z') ||
        (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9') ||
        ch == '.' || ch == '_' || ch == '-';
    if (!allowed) {
      return false;
    }
  }
  return true;
}

bool HasDevViewerAssets(const std::filesystem::path& dir) {
  return std::filesystem::exists(dir / "index.html") &&
      std::filesystem::exists(dir / "viewer.css") &&
      std::filesystem::exists(dir / "viewer.js");
}

std::filesystem::path ResolveDevViewerAssetDir(const std::string& value) {
  if (value.empty()) {
    return {};
  }

  std::vector<std::filesystem::path> candidates;
  const std::filesystem::path requested(value);
  candidates.push_back(requested);
  if (requested.is_relative()) {
    candidates.emplace_back(std::filesystem::path(CSVZALL_SOURCE_DIR) / requested);
  }

  for (const auto& candidate : candidates) {
    if (HasDevViewerAssets(candidate)) {
      std::error_code ec;
      const auto canonical = std::filesystem::weakly_canonical(candidate, ec);
      return ec ? candidate : canonical;
    }
  }

  std::ostringstream message;
  message << "viewer asset directory must contain index.html, viewer.css, and viewer.js: "
          << value;
  if (requested.is_relative()) {
    message << " (also tried "
            << (std::filesystem::path(CSVZALL_SOURCE_DIR) / requested).string()
            << ")";
  }
  throw std::runtime_error(message.str());
}

std::optional<std::filesystem::path> DevViewerAssetPath(
    const std::filesystem::path& asset_dir,
    std::string_view route) {
  if (asset_dir.empty()) {
    return std::nullopt;
  }
  if (route == "/") {
    return asset_dir / "index.html";
  }
  if (route == "/assets/viewer.css") {
    return asset_dir / "viewer.css";
  }
  if (route == "/assets/viewer.js") {
    return asset_dir / "viewer.js";
  }
  if (IsSafeViewerModuleRoute(route)) {
    constexpr std::string_view prefix = "/assets/modules/";
    const auto path = asset_dir / "modules" / std::string(route.substr(prefix.size()));
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
      return path;
    }
  }
  return std::nullopt;
}

std::string_view ViewerAssetContentType(std::string_view route) {
  if (route == "/" || route == "/index.html") {
    return "text/html";
  }
  if (route.ends_with(".css")) {
    return "text/css";
  }
  if (route.ends_with(".js")) {
    return "application/javascript";
  }
  return "application/octet-stream";
}

bool ServeViewerAsset(const std::filesystem::path& dev_asset_dir,
                      std::string_view path,
                      httplib::Response& response) {
  if (const auto dev_path = DevViewerAssetPath(dev_asset_dir, path)) {
    response.set_content(
        ReadDevViewerAsset(*dev_path),
        std::string(ViewerAssetContentType(path)));
    return true;
  }
  return ServeEmbeddedViewerAsset(path, response);
}
}  // namespace csvzall::pipeline::commands::view_internal
