#include "view.hpp"

#include "../common/chart_spec.hpp"
#include "../common/gzip_stream.hpp"

#include "commands.hpp"
#include "viewer_assets.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include "../../../vendor/httplib/httplib.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace csvzall::pipeline::commands {

namespace {

constexpr std::uint64_t kDefaultRowsPerPage = 500;
constexpr std::uint64_t kMaxRowsPerPage = 5000;
constexpr std::uint64_t kBytesPerMiB = 1024 * 1024;

csv::CSVFormat MakeViewFormat(const RunOptions& options) {
  csv::CSVFormat format;
  if (options.delimiter) {
    format.delimiter(*options.delimiter);
  } else {
    format.delimiter({',', '|', '\t', ';', '^'});
  }
  format.quote('"').header_row(0);
  return format;
}

void AppendJsonString(std::string& output, std::string_view value) {
  output.push_back('"');
  for (const unsigned char ch : value) {
    switch (ch) {
      case '\\':
        output += "\\\\";
        break;
      case '"':
        output += "\\\"";
        break;
      case '\b':
        output += "\\b";
        break;
      case '\f':
        output += "\\f";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        if (ch < 0x20) {
          static constexpr char hex[] = "0123456789abcdef";
          output += "\\u00";
          output.push_back(hex[(ch >> 4) & 0x0f]);
          output.push_back(hex[ch & 0x0f]);
        } else {
          output.push_back(static_cast<char>(ch));
        }
        break;
    }
  }
  output.push_back('"');
}

std::string BuildSchemaJson(const CsvViewData& data, bool editable) {
  std::string json;
  json.reserve(256 + data.headers().size() * 32);
  json += "{\"file\":";
  AppendJsonString(json, data.file_name());
  json += ",\"columns\":[";
  for (std::size_t i = 0; i < data.headers().size(); ++i) {
    if (i > 0) {
      json.push_back(',');
    }
    AppendJsonString(json, data.headers()[i]);
  }
  json += "],\"readOnly\":";
  json += editable ? "false" : "true";
  json += ",\"editable\":";
  json += editable ? "true" : "false";
  json += ",\"mode\":";
  AppendJsonString(json, data.mode_name());
  json += ",\"totalRows\":";
  json += std::to_string(data.row_count());
  json += "}";
  return json;
}

std::string BuildRowsJson(const CsvViewData& data,
                          std::uint64_t offset,
                          std::uint64_t limit,
                          const std::vector<std::vector<std::string>>& rows) {
  std::string json;
  json += "{\"offset\":";
  json += std::to_string(offset);
  json += ",\"limit\":";
  json += std::to_string(limit);
  json += ",\"totalRows\":";
  json += std::to_string(data.row_count());
  json += ",\"rows\":[";

  for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
    if (row_index > 0) {
      json.push_back(',');
    }
    json.push_back('[');
    const auto& row = rows[row_index];
    for (std::size_t col_index = 0; col_index < data.headers().size(); ++col_index) {
      if (col_index > 0) {
        json.push_back(',');
      }
      if (col_index < row.size()) {
        AppendJsonString(json, row[col_index]);
      } else {
        AppendJsonString(json, "");
      }
    }
    json.push_back(']');
  }

  json += "]}";
  return json;
}

std::string BuildHealthJson() {
  return "{\"status\":\"ok\",\"readOnly\":true}";
}

std::string BuildStartupJson(const std::string& url) {
  std::string json = "{\"url\":";
  AppendJsonString(json, url);
  json += "}";
  return json;
}

std::string GenerateSessionToken() {
  std::random_device rd;
  std::uniform_int_distribution<unsigned int> dist(0, 255);
  static constexpr char hex[] = "0123456789abcdef";

  std::string token;
  token.reserve(32);
  for (int i = 0; i < 16; ++i) {
    const auto byte = dist(rd);
    token.push_back(hex[(byte >> 4) & 0x0f]);
    token.push_back(hex[byte & 0x0f]);
  }
  return token;
}

bool OpenBrowserUrl(const std::string& url) {
#ifdef _WIN32
  const auto rc = reinterpret_cast<std::uintptr_t>(
      ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  return rc > 32;
#elif defined(__APPLE__)
  const auto command = "open \"" + url + "\"";
  return std::system(command.c_str()) == 0;
#else
  const auto command = "xdg-open \"" + url + "\" >/dev/null 2>&1";
  return std::system(command.c_str()) == 0;
#endif
}

bool ParseUint64Param(const httplib::Request& request,
                      std::string_view name,
                      std::uint64_t default_value,
                      std::uint64_t& output) {
  const auto raw = request.get_param_value(std::string(name));
  if (raw.empty()) {
    output = default_value;
    return true;
  }
  if (!std::all_of(raw.begin(), raw.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
      })) {
    return false;
  }
  try {
    output = static_cast<std::uint64_t>(std::stoull(raw));
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

void BadRequest(httplib::Response& response, std::string_view message) {
  response.status = 400;
  response.set_content(std::string(message) + "\n", "text/plain; charset=utf-8");
}

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

void SkipJsonWs(std::string_view text, std::size_t& pos) {
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
    ++pos;
  }
}

std::string ParseJsonString(std::string_view text, std::size_t& pos) {
  SkipJsonWs(text, pos);
  if (pos >= text.size() || text[pos] != '"') {
    throw std::runtime_error("expected JSON string");
  }
  ++pos;
  std::string value;
  while (pos < text.size()) {
    const char ch = text[pos++];
    if (ch == '"') {
      return value;
    }
    if (ch != '\\') {
      value.push_back(ch);
      continue;
    }
    if (pos >= text.size()) {
      throw std::runtime_error("unterminated JSON escape");
    }
    const char escaped = text[pos++];
    switch (escaped) {
      case '"': value.push_back('"'); break;
      case '\\': value.push_back('\\'); break;
      case '/': value.push_back('/'); break;
      case 'b': value.push_back('\b'); break;
      case 'f': value.push_back('\f'); break;
      case 'n': value.push_back('\n'); break;
      case 'r': value.push_back('\r'); break;
      case 't': value.push_back('\t'); break;
      default:
        throw std::runtime_error("unsupported JSON escape");
    }
  }
  throw std::runtime_error("unterminated JSON string");
}

void SkipJsonValue(std::string_view text, std::size_t& pos) {
  SkipJsonWs(text, pos);
  if (pos >= text.size()) {
    throw std::runtime_error("expected JSON value");
  }
  if (text[pos] == '"') {
    (void)ParseJsonString(text, pos);
    return;
  }
  if (text[pos] == '{' || text[pos] == '[') {
    const char open = text[pos++];
    const char close = open == '{' ? '}' : ']';
    int depth = 1;
    while (pos < text.size() && depth > 0) {
      if (text[pos] == '"') {
        (void)ParseJsonString(text, pos);
      } else if (text[pos] == open) {
        ++depth;
        ++pos;
      } else if (text[pos] == close) {
        --depth;
        ++pos;
      } else {
        ++pos;
      }
    }
    if (depth != 0) {
      throw std::runtime_error("unterminated JSON value");
    }
    return;
  }
  while (pos < text.size() && text[pos] != ',' && text[pos] != '}' && text[pos] != ']') {
    ++pos;
  }
}

template <typename Parser>
auto ParseJsonField(std::string_view body, std::string_view field, Parser parser) {
  std::size_t pos = 0;
  SkipJsonWs(body, pos);
  if (pos >= body.size() || body[pos++] != '{') {
    throw std::runtime_error("expected JSON object");
  }
  while (true) {
    SkipJsonWs(body, pos);
    if (pos < body.size() && body[pos] == '}') {
      break;
    }
    const auto key = ParseJsonString(body, pos);
    SkipJsonWs(body, pos);
    if (pos >= body.size() || body[pos++] != ':') {
      throw std::runtime_error("expected JSON object separator");
    }
    if (key == field) {
      return parser(body, pos);
    }
    SkipJsonValue(body, pos);
    SkipJsonWs(body, pos);
    if (pos < body.size() && body[pos] == ',') {
      ++pos;
      continue;
    }
    if (pos < body.size() && body[pos] == '}') {
      break;
    }
  }
  throw std::runtime_error("missing JSON field: " + std::string(field));
}

std::uint64_t JsonUintField(std::string_view body, std::string_view field) {
  return ParseJsonField(body, field, [](std::string_view text, std::size_t& pos) {
    SkipJsonWs(text, pos);
    if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) {
      throw std::runtime_error("expected JSON integer");
    }
    std::uint64_t value = 0;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])) != 0) {
      value = value * 10 + static_cast<std::uint64_t>(text[pos++] - '0');
    }
    return value;
  });
}

std::string JsonStringField(std::string_view body, std::string_view field) {
  return ParseJsonField(body, field, [](std::string_view text, std::size_t& pos) {
    return ParseJsonString(text, pos);
  });
}

std::vector<std::string> JsonStringArrayField(std::string_view body, std::string_view field) {
  return ParseJsonField(body, field, [](std::string_view text, std::size_t& pos) {
    SkipJsonWs(text, pos);
    if (pos >= text.size() || text[pos++] != '[') {
      throw std::runtime_error("expected JSON array");
    }
    std::vector<std::string> values;
    while (true) {
      SkipJsonWs(text, pos);
      if (pos < text.size() && text[pos] == ']') {
        ++pos;
        return values;
      }
      values.push_back(ParseJsonString(text, pos));
      SkipJsonWs(text, pos);
      if (pos < text.size() && text[pos] == ',') {
        ++pos;
        continue;
      }
      if (pos < text.size() && text[pos] == ']') {
        ++pos;
        return values;
      }
      throw std::runtime_error("expected JSON array separator");
    }
  });
}

bool JsonBoolField(std::string_view body, std::string_view field) {
  return ParseJsonField(body, field, [](std::string_view text, std::size_t& pos) {
    SkipJsonWs(text, pos);
    if (text.substr(pos, 4) == "true") {
      pos += 4;
      return true;
    }
    if (text.substr(pos, 5) == "false") {
      pos += 5;
      return false;
    }
    throw std::runtime_error("expected JSON boolean");
  });
}

std::string JsonStringFieldOr(std::string_view body,
                              std::string_view field,
                              std::string fallback) {
  try {
    return JsonStringField(body, field);
  } catch (const std::exception& ex) {
    const std::string missing = "missing JSON field: " + std::string(field);
    if (ex.what() == missing) {
      return fallback;
    }
    throw;
  }
}

bool JsonBoolFieldOr(std::string_view body, std::string_view field, bool fallback) {
  try {
    return JsonBoolField(body, field);
  } catch (const std::exception& ex) {
    const std::string missing = "missing JSON field: " + std::string(field);
    if (ex.what() == missing) {
      return fallback;
    }
    throw;
  }
}

std::filesystem::path FindChartConfigPath(const std::filesystem::path& input_path) {
  auto current = std::filesystem::absolute(input_path).parent_path();
  while (!current.empty()) {
    const auto candidate = current / ".csvzall" / "charts.json";
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
    const auto parent = current.parent_path();
    if (parent == current) {
      break;
    }
    current = parent;
  }
  return std::filesystem::absolute(input_path).parent_path() / ".csvzall" / "charts.json";
}

std::filesystem::path ChartConfigRoot(const std::filesystem::path& config_path) {
  const auto parent = std::filesystem::absolute(config_path).parent_path();
  if (parent.filename() == ".csvzall") {
    return parent.parent_path();
  }
  return parent;
}

std::string RelativePathForConfig(const std::filesystem::path& root,
                                  const std::filesystem::path& path) {
  const auto absolute_path = std::filesystem::absolute(path).lexically_normal();
  const auto absolute_root = std::filesystem::absolute(root).lexically_normal();
  auto relative = absolute_path.lexically_relative(absolute_root);
  const auto relative_text = relative.generic_string();
  if (relative.empty() || relative_text.starts_with("..")) {
    relative = absolute_path;
  }
  return relative.generic_string();
}

std::filesystem::path NormalizeAbsolutePath(const std::filesystem::path& path) {
  std::error_code ec;
  const auto canonical = std::filesystem::weakly_canonical(std::filesystem::absolute(path), ec);
  return ec ? std::filesystem::absolute(path).lexically_normal() : canonical;
}

bool SamePath(const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
  return NormalizeAbsolutePath(lhs) == NormalizeAbsolutePath(rhs);
}

std::string SanitizeChartId(std::string value) {
  std::string result;
  bool last_dash = false;
  for (const unsigned char ch : value) {
    if (std::isalnum(ch) != 0) {
      result.push_back(static_cast<char>(std::tolower(ch)));
      last_dash = false;
      continue;
    }
    if (!last_dash && !result.empty()) {
      result.push_back('-');
      last_dash = true;
    }
  }
  while (!result.empty() && result.back() == '-') {
    result.pop_back();
  }
  return result;
}

void AppendJsonOption(std::string& json,
                      const char* key,
                      std::string_view value,
                      bool& first_option) {
  if (!first_option) {
    json += ",";
  }
  first_option = false;
  json += "\n        \"";
  json += key;
  json += "\": ";
  AppendJsonString(json, value);
}

std::string ChartSpecJsonForConfig(const common::ChartSpec& spec,
                                   const std::filesystem::path& root) {
  if (!spec.output) {
    throw std::runtime_error("chart output path is required: " + spec.id);
  }
  std::string json;
  json += "{\n      \"id\": ";
  AppendJsonString(json, spec.id);
  json += ",\n      \"type\": ";
  AppendJsonString(json, spec.type);
  json += ",\n      \"input\": ";
  AppendJsonString(json, RelativePathForConfig(root, spec.input));
  json += ",\n      \"output\": ";
  AppendJsonString(json, RelativePathForConfig(root, *spec.output));
  json += ",\n      \"options\": {";
  bool first_option = true;
  if (spec.type == "heatmap") {
    AppendJsonOption(json, "date", spec.heatmap.date_column, first_option);
    if (!spec.heatmap.value_column.empty()) {
      AppendJsonOption(json, "value", spec.heatmap.value_column, first_option);
    }
    if (!spec.heatmap.label_column.empty()) {
      AppendJsonOption(json, "label", spec.heatmap.label_column, first_option);
    }
    if (!spec.heatmap.lookback.empty()) {
      AppendJsonOption(json, "lookback", spec.heatmap.lookback, first_option);
      if (!spec.heatmap.end_date.empty()) {
        AppendJsonOption(json, "end", spec.heatmap.end_date, first_option);
      }
    } else {
      AppendJsonOption(json, "start", spec.heatmap.start_date, first_option);
      AppendJsonOption(json, "end", spec.heatmap.end_date, first_option);
    }
    if (!spec.heatmap.title.empty()) {
      AppendJsonOption(json, "title", spec.heatmap.title, first_option);
    }
  } else if (spec.type == "bar") {
    AppendJsonOption(json, "label", spec.bar.label_column, first_option);
    AppendJsonOption(json, "value", spec.bar.value_column, first_option);
    if (!spec.bar.title.empty()) AppendJsonOption(json, "title", spec.bar.title, first_option);
    if (!spec.bar.x_label.empty()) AppendJsonOption(json, "xLabel", spec.bar.x_label, first_option);
    if (!spec.bar.y_label.empty()) AppendJsonOption(json, "yLabel", spec.bar.y_label, first_option);
  } else if (spec.type == "line") {
    AppendJsonOption(json, "x", spec.line.x_column, first_option);
    AppendJsonOption(json, "y", spec.line.y_column, first_option);
    if (!spec.line.series_column.empty()) AppendJsonOption(json, "series", spec.line.series_column, first_option);
    if (!spec.line.title.empty()) AppendJsonOption(json, "title", spec.line.title, first_option);
    if (!spec.line.x_label.empty()) AppendJsonOption(json, "xLabel", spec.line.x_label, first_option);
    if (!spec.line.y_label.empty()) AppendJsonOption(json, "yLabel", spec.line.y_label, first_option);
  }
  json += "\n      },\n      \"runOnSave\": ";
  json += spec.run_on_save ? "true" : "false";
  json += "\n    }";
  return json;
}

std::string ChartSpecJsonForApi(const common::ChartSpec& spec,
                                const std::filesystem::path& root) {
  std::string json;
  json += "{\"id\":";
  AppendJsonString(json, spec.id);
  json += ",\"type\":";
  AppendJsonString(json, spec.type);
  json += ",\"input\":";
  AppendJsonString(json, RelativePathForConfig(root, spec.input));
  json += ",\"output\":";
  AppendJsonString(json, spec.output ? RelativePathForConfig(root, *spec.output) : "");
  json += ",\"options\":{";
  if (spec.type == "heatmap") {
    json += "\"date\":";
    AppendJsonString(json, spec.heatmap.date_column);
    json += ",\"value\":";
    AppendJsonString(json, spec.heatmap.value_column);
    json += ",\"label\":";
    AppendJsonString(json, spec.heatmap.label_column);
    json += ",\"start\":";
    AppendJsonString(json, spec.heatmap.start_date);
    json += ",\"end\":";
    AppendJsonString(json, spec.heatmap.end_date);
    json += ",\"lookback\":";
    AppendJsonString(json, spec.heatmap.lookback);
    json += ",\"title\":";
    AppendJsonString(json, spec.heatmap.title);
  } else if (spec.type == "bar") {
    json += "\"label\":";
    AppendJsonString(json, spec.bar.label_column);
    json += ",\"value\":";
    AppendJsonString(json, spec.bar.value_column);
    json += ",\"title\":";
    AppendJsonString(json, spec.bar.title);
    json += ",\"xLabel\":";
    AppendJsonString(json, spec.bar.x_label);
    json += ",\"yLabel\":";
    AppendJsonString(json, spec.bar.y_label);
  } else if (spec.type == "line") {
    json += "\"x\":";
    AppendJsonString(json, spec.line.x_column);
    json += ",\"y\":";
    AppendJsonString(json, spec.line.y_column);
    json += ",\"series\":";
    AppendJsonString(json, spec.line.series_column);
    json += ",\"title\":";
    AppendJsonString(json, spec.line.title);
    json += ",\"xLabel\":";
    AppendJsonString(json, spec.line.x_label);
    json += ",\"yLabel\":";
    AppendJsonString(json, spec.line.y_label);
  }
  json += "},\"runOnSave\":";
  json += spec.run_on_save ? "true" : "false";
  json += "}";
  return json;
}

std::string SerializeChartConfig(const std::vector<common::ChartSpec>& charts,
                                 const std::filesystem::path& root) {
  std::string json = "{\n  \"charts\": [";
  for (std::size_t i = 0; i < charts.size(); ++i) {
    json += i == 0 ? "\n    " : ",\n    ";
    json += ChartSpecJsonForConfig(charts[i], root);
  }
  if (!charts.empty()) {
    json += "\n  ";
  }
  json += "]\n}\n";
  return json;
}

std::vector<common::ChartSpec> LoadChartConfigIfExists(const std::filesystem::path& config_path) {
  if (!std::filesystem::exists(config_path)) {
    return {};
  }
  return common::LoadChartConfig(config_path).charts;
}

void WriteTextFile(const std::filesystem::path& path, std::string_view text) {
  const auto parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    throw std::runtime_error("unable to open chart config for writing: " + path.string());
  }
  output << text;
  output.flush();
  if (!output.good()) {
    throw std::runtime_error("failed to write chart config: " + path.string());
  }
}

std::filesystem::path ResolveChartOutputPath(const std::filesystem::path& root,
                                             const std::string& output) {
  const std::filesystem::path output_path(output);
  return output_path.is_absolute() ? output_path : root / output_path;
}

void RenderSavedChart(common::ChartSpec spec,
                      const LoggerCallbacks& logger) {
#ifdef CSVZALL_HAVE_SVGPLOT
  RunOptions options;
  options.input_path = spec.input.string();
  RunStats stats;
  std::string render_error;
  LoggerCallbacks chart_logger = logger;
  chart_logger.error = [&logger, &render_error](const std::string& message) {
    render_error = message;
    if (logger.error) {
      logger.error(message);
    }
  };
  const auto rc = RunChart(spec, options, chart_logger, stats);
  if (rc != 0) {
    auto message = "chart config saved, but chart rendering failed: " + spec.id;
    if (!render_error.empty()) {
      message += ": " + render_error;
    }
    throw std::runtime_error(message);
  }
#else
  (void)spec;
  (void)logger;
  throw std::runtime_error("chart config saved, but SVG rendering is disabled in this build");
#endif
}

void ValidateChartBeforeSave(const common::ChartSpec& spec,
                             const LoggerCallbacks& logger) {
#ifdef CSVZALL_HAVE_SVGPLOT
  RunOptions options;
  options.input_path = spec.input.string();
  RunStats stats;
  std::string validation_error;
  LoggerCallbacks chart_logger = logger;
  chart_logger.error = [&logger, &validation_error](const std::string& message) {
    validation_error = message;
    if (logger.error) {
      logger.error(message);
    }
  };
  const auto rc = ValidateChart(spec, options, chart_logger, stats);
  if (rc != 0) {
    throw std::runtime_error(validation_error.empty() ? "chart validation failed"
                                                     : validation_error);
  }
#else
  (void)spec;
  (void)logger;
  throw std::runtime_error("SVG rendering is disabled in this build");
#endif
}

std::string BuildChartConfigListJson(const CsvViewData& data) {
  const auto config_path = FindChartConfigPath(data.input_path());
  const auto root = ChartConfigRoot(config_path);
  const auto charts = LoadChartConfigIfExists(config_path);
  std::string result = "{\"ok\":true,\"configPath\":";
  AppendJsonString(result, config_path.string());
  result += ",\"charts\":[";
  bool first = true;
  std::set<std::string> seen_ids;
  for (const auto& chart : charts) {
    if (!SamePath(chart.input, data.input_path())) {
      continue;
    }
    if (!seen_ids.insert(chart.id).second) {
      continue;
    }
    if (!first) {
      result += ",";
    }
    first = false;
    result += ChartSpecJsonForApi(chart, root);
  }
  result += "]}";
  return result;
}

common::ChartSpec FindCurrentCsvChart(const CsvViewData& data, const std::string& id) {
  const auto config_path = FindChartConfigPath(data.input_path());
  const auto charts = LoadChartConfigIfExists(config_path);
  for (const auto& chart : charts) {
    if (chart.id == id && SamePath(chart.input, data.input_path())) {
      return chart;
    }
  }
  throw std::runtime_error("chart not found for current CSV: " + id);
}

std::string GenerateCurrentCsvChart(const CsvViewData& data,
                                    std::string_view body,
                                    const LoggerCallbacks& logger) {
  const auto id = JsonStringField(body, "id");
  const auto config_path = FindChartConfigPath(data.input_path());
  const auto root = ChartConfigRoot(config_path);
  const auto chart = FindCurrentCsvChart(data, id);
  if (!chart.output) {
    throw std::runtime_error("chart output path is required: " + chart.id);
  }
  (void)data;
  RenderSavedChart(chart, logger);

  std::string result = "{\"ok\":true,\"id\":";
  AppendJsonString(result, chart.id);
  result += ",\"output\":";
  AppendJsonString(result, RelativePathForConfig(root, *chart.output));
  result += ",\"generated\":true}";
  return result;
}

std::string AppendHeatmapChartConfig(const CsvViewData& data,
                                     std::string_view body,
                                     const LoggerCallbacks& logger) {
  const auto config_path = FindChartConfigPath(data.input_path());
  const auto root = ChartConfigRoot(config_path);
  const auto input_path = RelativePathForConfig(root, data.input_path());
  const auto id = SanitizeChartId(JsonStringFieldOr(body, "id", ""));
  const auto type = JsonStringFieldOr(body, "type", "heatmap");
  const auto output = JsonStringFieldOr(body, "output", "");
  const auto title = JsonStringFieldOr(body, "title", "");
  const auto run_on_save = JsonBoolFieldOr(body, "runOnSave", true);

  auto charts = LoadChartConfigIfExists(config_path);
  common::ChartSpec spec;
  std::optional<std::filesystem::path> output_path;
  if (!output.empty()) {
    output_path = ResolveChartOutputPath(root, output);
  }
  if (type == "heatmap") {
    const auto date = JsonStringFieldOr(body, "date", "");
    const auto value = JsonStringFieldOr(body, "value", "");
    const auto label = JsonStringFieldOr(body, "label", "");
    const auto start = JsonStringFieldOr(body, "start", "");
    const auto end = JsonStringFieldOr(body, "end", "");
    const auto lookback = JsonStringFieldOr(body, "lookback", "");
    common::HeatmapSpec heatmap;
    heatmap.date_column = date;
    heatmap.value_column = value;
    heatmap.label_column = label;
    heatmap.start_date = start;
    heatmap.end_date = end;
    heatmap.lookback = lookback;
    heatmap.title = title;
    spec = common::MakeHeatmapChartSpec(
        id,
        ResolveChartOutputPath(root, input_path),
        output_path,
        run_on_save,
        heatmap);
  } else if (type == "bar") {
    const auto label = JsonStringFieldOr(body, "label", "");
    const auto value = JsonStringFieldOr(body, "value", "");
    common::BarSpec bar;
    bar.label_column = label;
    bar.value_column = value;
    bar.title = title;
    bar.x_label = JsonStringFieldOr(body, "xLabel", "");
    bar.y_label = JsonStringFieldOr(body, "yLabel", "");
    spec = common::MakeBarChartSpec(
        id,
        ResolveChartOutputPath(root, input_path),
        output_path,
        run_on_save,
        bar);
  } else if (type == "line") {
    const auto x = JsonStringFieldOr(body, "x", "");
    const auto y = JsonStringFieldOr(body, "y", "");
    const auto series = JsonStringFieldOr(body, "series", "");
    common::LineSpec line;
    line.x_column = x;
    line.y_column = y;
    line.series_column = series;
    line.title = title;
    line.x_label = JsonStringFieldOr(body, "xLabel", "");
    line.y_label = JsonStringFieldOr(body, "yLabel", "");
    spec = common::MakeLineChartSpec(
        id,
        ResolveChartOutputPath(root, input_path),
        output_path,
        run_on_save,
        line);
  } else {
    throw std::runtime_error("unknown chart type: " + type);
  }
  ValidateChartBeforeSave(spec, logger);

  bool updated_existing = false;
  std::vector<common::ChartSpec> updated_charts;
  updated_charts.reserve(charts.size() + 1);
  for (auto& chart : charts) {
    if (chart.id == id) {
      if (!updated_existing) {
        updated_charts.push_back(spec);
        updated_existing = true;
      }
      continue;
    }
    updated_charts.push_back(std::move(chart));
  }
  if (!updated_existing) {
    updated_charts.push_back(spec);
  }
  WriteTextFile(config_path, SerializeChartConfig(updated_charts, root));
  RenderSavedChart(spec, logger);

  std::string result = "{\"ok\":true,\"configPath\":";
  AppendJsonString(result, config_path.string());
  result += ",\"id\":";
  AppendJsonString(result, id);
  result += ",\"output\":";
  AppendJsonString(result, output);
  result += ",\"action\":";
  AppendJsonString(result, updated_existing ? "updated" : "created");
  result += ",\"generated\":true";
  result += "}";
  return result;
}

void ValidatePlainLocalViewInput(const std::string& input_path, const RunOptions& options) {
  if (input_path.empty() || input_path == "-") {
    throw std::runtime_error("stdin is not supported; pass a plain local CSV file path");
  }
  if (!options.zip_entry.empty() || common::IsZipPath(input_path) || common::IsGzipPath(input_path)) {
    throw std::runtime_error("view currently supports plain local CSV files only");
  }
}

std::uint64_t GetFileSize(const std::string& input_path) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(input_path, ec);
  if (ec) {
    throw std::runtime_error("unable to stat input file: " + input_path);
  }
  return static_cast<std::uint64_t>(size);
}

std::filesystem::file_time_type GetFileMtime(const std::string& input_path) {
  std::error_code ec;
  const auto mtime = std::filesystem::last_write_time(input_path, ec);
  if (ec) {
    throw std::runtime_error("unable to stat input file mtime: " + input_path);
  }
  return mtime;
}

std::uint64_t ThresholdBytes(std::size_t threshold_mb) {
  constexpr auto max = std::numeric_limits<std::uint64_t>::max();
  if (threshold_mb > max / kBytesPerMiB) {
    return max;
  }
  return static_cast<std::uint64_t>(threshold_mb) * kBytesPerMiB;
}

CsvMaterializedFile OpenMaterializedFile(const std::string& input_path,
                                         const RunOptions& options,
                                         const LoggerCallbacks& logger,
                                         RunStats& stats) {
  ValidatePlainLocalViewInput(input_path, options);

  CsvMaterializedFile materialized;
  materialized.input_path = input_path;
  materialized.file_name = std::filesystem::path(input_path).filename().string();
  const auto file_size = GetFileSize(input_path);
  materialized.source_size = file_size;
  materialized.source_mtime = GetFileMtime(input_path);

  auto format = MakeViewFormat(options);
  materialized.format = format;
  csv::CSVReader reader(input_path, format);
  materialized.frame = std::make_shared<csv::DataFrame<>>(reader);
  if (materialized.frame->columns().empty()) {
    throw std::runtime_error("input appears to have no header row");
  }

  stats.rows_processed = static_cast<std::uint64_t>(materialized.frame->n_rows());
  stats.bytes_processed = file_size;
  if (logger.verbose) {
    logger.verbose("view: materialized " + std::to_string(materialized.frame->n_rows()) +
                   " row(s) from " + input_path);
  }
  return materialized;
}

}  // namespace

CsvIndexedFile CsvIndexedFile::Open(const std::string& input_path,
                                    const RunOptions& options,
                                    const LoggerCallbacks& logger,
                                    RunStats& stats) {
  ValidatePlainLocalViewInput(input_path, options);

  CsvIndexedFile indexed;
  indexed.input_path_ = input_path;
  indexed.file_name_ = std::filesystem::path(input_path).filename().string();

  indexed.file_size_ = GetFileSize(input_path);

  auto format = MakeViewFormat(options);
  csv::CSVReader reader(input_path, format);
  indexed.format_ = reader.get_format();
  indexed.headers_ = reader.get_col_names();
  if (indexed.headers_.empty()) {
    throw std::runtime_error("input appears to have no header row");
  }

  for (auto& row : reader) {
    indexed.index_.push_back({static_cast<std::uint64_t>(row.byte_offset())});
  }

  stats.rows_processed = static_cast<std::uint64_t>(indexed.index_.size());
  stats.bytes_processed = indexed.file_size_;
  if (logger.verbose) {
    logger.verbose("view: indexed " + std::to_string(indexed.index_.size()) +
                   " row offset(s) from " + input_path);
  }
  return indexed;
}

const std::string& CsvIndexedFile::input_path() const {
  return input_path_;
}

const std::string& CsvIndexedFile::file_name() const {
  return file_name_;
}

const std::vector<std::string>& CsvIndexedFile::headers() const {
  return headers_;
}

std::uint64_t CsvIndexedFile::row_count() const {
  return static_cast<std::uint64_t>(index_.size());
}

std::vector<std::vector<std::string>> CsvIndexedFile::read_rows(
    const std::uint64_t offset,
    const std::uint64_t limit) const {
  if (offset >= row_count() || limit == 0) {
    return {};
  }

  const auto count = std::min<std::uint64_t>(limit, row_count() - offset);
  const auto start = index_[static_cast<std::size_t>(offset)].byte_offset;
  const auto after_last_row = offset + count;
  const auto end = after_last_row < row_count()
      ? index_[static_cast<std::size_t>(after_last_row)].byte_offset
      : file_size_;
  if (end < start) {
    throw std::runtime_error("row index is not monotonic");
  }

  const auto length = end - start;
  if (length > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    throw std::runtime_error("requested row page is too large to materialize");
  }

  std::ifstream input(input_path_, std::ios::binary);
  if (!input) {
    throw std::runtime_error("unable to open input file: " + input_path_);
  }
  input.seekg(static_cast<std::streamoff>(start), std::ios::beg);

  std::string buffer(static_cast<std::size_t>(length), '\0');
  input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  buffer.resize(static_cast<std::size_t>(input.gcount()));

  auto page_format = format_;
  page_format.no_header().variable_columns(csv::VariableColumnPolicy::KEEP);
  std::stringstream page_stream(buffer);
  csv::CSVReader page_reader(page_stream, page_format);

  std::vector<std::vector<std::string>> rows;
  rows.reserve(static_cast<std::size_t>(count));
  for (auto& row : page_reader) {
    rows.emplace_back(std::vector<std::string>(row));
    if (rows.size() == count) {
      break;
    }
  }
  return rows;
}

CsvViewData CsvViewData::Open(const std::string& input_path,
                              const RunOptions& options,
                              const LoggerCallbacks& logger,
                              RunStats& stats) {
  ValidatePlainLocalViewInput(input_path, options);
  const auto file_size = GetFileSize(input_path);
  const auto materialize = [&]() {
    if (options.view_edit) {
      return true;
    }
    switch (options.view_mode) {
      case ViewModeSelection::Materialized:
        return true;
      case ViewModeSelection::Paged:
        return false;
      case ViewModeSelection::Auto:
        return file_size <= ThresholdBytes(options.view_materialize_threshold_mb);
    }
    return false;
  }();

  if (materialize) {
    return CsvViewData(OpenMaterializedFile(input_path, options, logger, stats));
  }
  return CsvViewData(CsvIndexedFile::Open(input_path, options, logger, stats));
}

CsvViewData::CsvViewData(CsvMaterializedFile materialized)
    : data_(std::move(materialized)) {}

CsvViewData::CsvViewData(CsvIndexedFile indexed)
    : data_(std::move(indexed)) {}

CsvViewDataMode CsvViewData::mode() const {
  return std::holds_alternative<CsvMaterializedFile>(data_)
      ? CsvViewDataMode::Materialized
      : CsvViewDataMode::Paged;
}

std::string_view CsvViewData::mode_name() const {
  return mode() == CsvViewDataMode::Materialized ? "materialized" : "paged";
}

const std::string& CsvViewData::input_path() const {
  if (const auto* materialized = std::get_if<CsvMaterializedFile>(&data_)) {
    return materialized->input_path;
  }
  return std::get<CsvIndexedFile>(data_).input_path();
}

const std::string& CsvViewData::file_name() const {
  if (const auto* materialized = std::get_if<CsvMaterializedFile>(&data_)) {
    return materialized->file_name;
  }
  return std::get<CsvIndexedFile>(data_).file_name();
}

const std::vector<std::string>& CsvViewData::headers() const {
  if (const auto* materialized = std::get_if<CsvMaterializedFile>(&data_)) {
    return materialized->frame->columns();
  }
  return std::get<CsvIndexedFile>(data_).headers();
}

std::uint64_t CsvViewData::row_count() const {
  if (const auto* materialized = std::get_if<CsvMaterializedFile>(&data_)) {
    return static_cast<std::uint64_t>(materialized->frame->n_rows());
  }
  return std::get<CsvIndexedFile>(data_).row_count();
}

std::vector<std::vector<std::string>> CsvViewData::read_rows(
    const std::uint64_t offset,
    const std::uint64_t limit) const {
  if (const auto* materialized = std::get_if<CsvMaterializedFile>(&data_)) {
    if (offset >= row_count() || limit == 0) {
      return {};
    }
    const auto count = std::min<std::uint64_t>(limit, row_count() - offset);
    std::vector<std::vector<std::string>> rows;
    rows.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
      rows.emplace_back(std::vector<std::string>(
          materialized->frame->at(static_cast<std::size_t>(offset + i))));
    }
    return rows;
  }
  return std::get<CsvIndexedFile>(data_).read_rows(offset, limit);
}

void CsvViewData::edit_cell(const std::uint64_t row,
                            const std::string& column,
                            const std::string& value) {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    throw std::runtime_error("editing requires materialized view mode");
  }
  if (!materialized->frame->has_column(column)) {
    throw std::runtime_error("unknown column: " + column);
  }
  if (row >= materialized->frame->n_rows()) {
    throw std::out_of_range("row index out of bounds");
  }
  materialized->frame->at(static_cast<std::size_t>(row))[column] = value;
}

void CsvViewData::delete_row(const std::uint64_t row) {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    throw std::runtime_error("editing requires materialized view mode");
  }
  if (row >= materialized->frame->n_rows()) {
    throw std::out_of_range("row index out of bounds");
  }
  if (!materialized->frame->at(static_cast<std::size_t>(row)).erase()) {
    throw std::runtime_error("failed to delete row");
  }
}

void CsvViewData::insert_row(const std::uint64_t row, const std::vector<std::string>& values) {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    throw std::runtime_error("editing requires materialized view mode");
  }
  if (values.size() != materialized->frame->n_cols()) {
    throw std::runtime_error("inserted row must match header shape");
  }
  if (row > materialized->frame->n_rows()) {
    throw std::out_of_range("row index out of bounds");
  }
  materialized->frame->insert_row(static_cast<std::size_t>(row), values);
}

void CsvViewData::insert_column(const std::uint64_t column,
                                const std::string& name,
                                const std::string& value) {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    throw std::runtime_error("editing requires materialized view mode");
  }
  if (column > materialized->frame->n_cols()) {
    throw std::out_of_range("column index out of bounds");
  }
  materialized->frame->insert_column(static_cast<std::size_t>(column), name, value);
}

void CsvViewData::delete_column(const std::string& column) {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    throw std::runtime_error("editing requires materialized view mode");
  }
  if (materialized->frame->n_cols() <= 1) {
    throw std::runtime_error("cannot delete the last column");
  }
  if (!materialized->frame->has_column(column)) {
    throw std::runtime_error("unknown column: " + column);
  }
  if (!materialized->frame->column_view(column).erase()) {
    throw std::runtime_error("failed to delete column: " + column);
  }
}

void CsvViewData::reset() {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    throw std::runtime_error("reset requires materialized view mode");
  }

  const auto file_size = GetFileSize(materialized->input_path);
  csv::CSVReader reader(materialized->input_path, materialized->format);
  auto frame = std::make_shared<csv::DataFrame<>>(reader);
  if (frame->columns().empty()) {
    throw std::runtime_error("input appears to have no header row");
  }

  materialized->frame = std::move(frame);
  materialized->source_size = file_size;
  materialized->source_mtime = GetFileMtime(materialized->input_path);
}

void CsvViewData::save() {
  auto* materialized = std::get_if<CsvMaterializedFile>(&data_);
  if (!materialized) {
    throw std::runtime_error("saving requires materialized view mode");
  }

  if (GetFileSize(materialized->input_path) != materialized->source_size ||
      GetFileMtime(materialized->input_path) != materialized->source_mtime) {
    throw std::runtime_error("source file changed externally; reload before saving");
  }

  const auto target = std::filesystem::path(materialized->input_path);
  const auto temp = target.parent_path() /
      (target.filename().string() + ".csvzall-save-" + GenerateSessionToken() + ".tmp");
  try {
    {
      std::ofstream output(temp, std::ios::binary);
      if (!output) {
        throw std::runtime_error("unable to open temporary save file: " + temp.string());
      }
      auto writer = csv::make_csv_writer(output).set_auto_flush(false);
      writer << materialized->frame->columns();
      for (const auto& row : *materialized->frame) {
        writer << std::vector<std::string>(row);
      }
      writer.flush();
      output.close();
      if (!output) {
        throw std::runtime_error("failed to write temporary save file: " + temp.string());
      }
    }

#ifdef _WIN32
    if (!MoveFileExW(temp.wstring().c_str(),
                     target.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      throw std::system_error(
          static_cast<int>(GetLastError()), std::system_category(), "failed to replace CSV file");
    }
#else
    std::filesystem::rename(temp, target);
#endif
    materialized->source_size = GetFileSize(materialized->input_path);
    materialized->source_mtime = GetFileMtime(materialized->input_path);
  } catch (...) {
    std::error_code ec;
    std::filesystem::remove(temp, ec);
    throw;
  }
}

struct ViewServer::Impl {
  explicit Impl(const CsvViewData& source_data, const LoggerCallbacks& source_logger)
      : data(source_data), logger(source_logger) {}

  CsvViewData data;
  LoggerCallbacks logger;
  httplib::Server server;
  std::thread thread;
  std::atomic<bool> running{false};
  std::atomic<bool> stop_requested{false};
  int port = -1;
  bool serve_once = false;
  bool editable = false;
  std::string token;
  std::filesystem::path viewer_asset_dir;

  bool HasValidToken(const httplib::Request& request) const {
    const auto header = request.get_header_value("X-Session-Token");
    if (!header.empty()) {
      return header == token;
    }
    const auto param = request.get_param_value("token");
    return !param.empty() && param == token;
  }

  void MaybeStopAfterRequest() {
    if (!serve_once || stop_requested.exchange(true)) {
      return;
    }
    std::thread([this]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      server.stop();
    }).detach();
  }

  void RejectUnauthorized(httplib::Response& response) {
    response.status = 403;
    response.set_content("forbidden\n", "text/plain; charset=utf-8");
  }

  bool RequireEditable(httplib::Response& response) const {
    if (editable && data.mode() == CsvViewDataMode::Materialized) {
      return true;
    }
    response.status = 405;
    response.set_content("viewer is read-only\n", "text/plain; charset=utf-8");
    return false;
  }
};

ViewServer::ViewServer(const CsvViewData& data, const LoggerCallbacks& logger)
    : impl_(std::make_unique<Impl>(data, logger)) {}

ViewServer::~ViewServer() {
  Stop();
}

int ViewServer::Start(const ViewServerOptions& options) {
  impl_->serve_once = options.serve_once;
  impl_->editable = options.editable;
  impl_->stop_requested = false;
  impl_->token = options.session_token.empty() ? GenerateSessionToken() : options.session_token;
  try {
    impl_->viewer_asset_dir = ResolveDevViewerAssetDir(options.viewer_asset_dir);
  } catch (const std::exception& ex) {
    if (impl_->logger.error) {
      impl_->logger.error(std::string("view: ") + ex.what());
    }
    return 1;
  }

  impl_->server.Get("/", [this](const httplib::Request& request, httplib::Response& response) {
    if (!impl_->HasValidToken(request)) {
      impl_->RejectUnauthorized(response);
      return;
    }
    try {
      if (!ServeViewerAsset(impl_->viewer_asset_dir, "/", response)) {
        response.status = 500;
        response.set_content("viewer asset missing\n", "text/plain; charset=utf-8");
      }
    } catch (const std::exception& ex) {
      response.status = 500;
      response.set_content(std::string(ex.what()) + "\n", "text/plain; charset=utf-8");
    }
    impl_->MaybeStopAfterRequest();
  });

  impl_->server.Get("/assets/viewer.css",
                    [this](const httplib::Request&, httplib::Response& response) {
                      try {
                        if (!ServeViewerAsset(impl_->viewer_asset_dir, "/assets/viewer.css", response)) {
                          response.status = 404;
                        }
                      } catch (const std::exception& ex) {
                        response.status = 500;
                        response.set_content(std::string(ex.what()) + "\n",
                                             "text/plain; charset=utf-8");
                      }
                    });
  impl_->server.Get("/assets/viewer.js",
                    [this](const httplib::Request&, httplib::Response& response) {
                      try {
                        if (!ServeViewerAsset(impl_->viewer_asset_dir, "/assets/viewer.js", response)) {
                          response.status = 404;
                        }
                      } catch (const std::exception& ex) {
                        response.status = 500;
                        response.set_content(std::string(ex.what()) + "\n",
                                             "text/plain; charset=utf-8");
                      }
                    });
  impl_->server.Get("/assets/ag-grid-community.min.js",
                    [this](const httplib::Request&, httplib::Response& response) {
                      if (!ServeEmbeddedViewerAsset("/assets/ag-grid-community.min.js", response)) {
                        response.status = 404;
                      }
                    });
  impl_->server.Get("/assets/ag-grid.css",
                    [this](const httplib::Request&, httplib::Response& response) {
                      if (!ServeEmbeddedViewerAsset("/assets/ag-grid.css", response)) {
                        response.status = 404;
                      }
                    });
  impl_->server.Get("/assets/ag-theme-alpine.css",
                    [this](const httplib::Request&, httplib::Response& response) {
                      if (!ServeEmbeddedViewerAsset("/assets/ag-theme-alpine.css", response)) {
                        response.status = 404;
                      }
                    });
  impl_->server.Get(R"(/assets/popright/([A-Za-z0-9._-]+\.js))",
                    [](const httplib::Request& request, httplib::Response& response) {
                      const auto route = std::string("/assets/popright/") + request.matches[1].str();
                      if (!ServeEmbeddedViewerAsset(route, response)) {
                        response.status = 404;
                      }
                    });
  impl_->server.Get("/assets/popright/styles.css",
                    [](const httplib::Request&, httplib::Response& response) {
                      if (!ServeEmbeddedViewerAsset("/assets/popright/styles.css", response)) {
                        response.status = 404;
                      }
                    });

  impl_->server.Get("/api/schema",
                    [this](const httplib::Request& request, httplib::Response& response) {
                      if (!impl_->HasValidToken(request)) {
                        impl_->RejectUnauthorized(response);
                        return;
                      }
                      response.set_content(BuildSchemaJson(impl_->data, impl_->editable),
                                           "application/json; charset=utf-8");
                      impl_->MaybeStopAfterRequest();
                    });

  impl_->server.Get("/api/rows",
                    [this](const httplib::Request& request, httplib::Response& response) {
                      if (!impl_->HasValidToken(request)) {
                        impl_->RejectUnauthorized(response);
                        return;
                      }

                      std::uint64_t offset = 0;
                      std::uint64_t limit = kDefaultRowsPerPage;
                      if (!ParseUint64Param(request, "offset", 0, offset) ||
                          !ParseUint64Param(request, "limit", kDefaultRowsPerPage, limit) ||
                          limit == 0) {
                        BadRequest(response, "offset and limit must be non-negative integers; limit must be greater than 0");
                        return;
                      }
                      if (impl_->data.mode() == CsvViewDataMode::Paged) {
                        limit = std::min<std::uint64_t>(limit, kMaxRowsPerPage);
                      } else if (offset < impl_->data.row_count()) {
                        limit = std::min<std::uint64_t>(limit, impl_->data.row_count() - offset);
                      }

                      try {
                        const auto rows = impl_->data.read_rows(offset, limit);
                        response.set_content(BuildRowsJson(impl_->data, offset, limit, rows),
                                             "application/json; charset=utf-8");
                      } catch (const std::exception& ex) {
                        if (impl_->logger.error) {
                          impl_->logger.error(std::string("view: ") + ex.what());
                        }
                        response.status = 500;
                        response.set_content("failed to read rows\n", "text/plain; charset=utf-8");
                      }
                      impl_->MaybeStopAfterRequest();
                    });

  impl_->server.Post("/api/edit-cell",
                     [this](const httplib::Request& request, httplib::Response& response) {
                       if (!impl_->HasValidToken(request)) {
                         impl_->RejectUnauthorized(response);
                         return;
                       }
                       if (!impl_->RequireEditable(response)) {
                         return;
                       }
                       try {
                         impl_->data.edit_cell(
                             JsonUintField(request.body, "row"),
                             JsonStringField(request.body, "column"),
                             JsonStringField(request.body, "value"));
                         response.set_content("{\"ok\":true}", "application/json; charset=utf-8");
                       } catch (const std::exception& ex) {
                         BadRequest(response, ex.what());
                       }
                     });

  impl_->server.Post("/api/delete-row",
                     [this](const httplib::Request& request, httplib::Response& response) {
                       if (!impl_->HasValidToken(request)) {
                         impl_->RejectUnauthorized(response);
                         return;
                       }
                       if (!impl_->RequireEditable(response)) {
                         return;
                       }
                       try {
                         impl_->data.delete_row(JsonUintField(request.body, "row"));
                         response.set_content("{\"ok\":true}", "application/json; charset=utf-8");
                       } catch (const std::exception& ex) {
                         BadRequest(response, ex.what());
                       }
                     });

  impl_->server.Post("/api/insert-row",
                     [this](const httplib::Request& request, httplib::Response& response) {
                       if (!impl_->HasValidToken(request)) {
                         impl_->RejectUnauthorized(response);
                         return;
                       }
                       if (!impl_->RequireEditable(response)) {
                         return;
                       }
                       try {
                         impl_->data.insert_row(
                             JsonUintField(request.body, "row"),
                             JsonStringArrayField(request.body, "values"));
                         response.set_content("{\"ok\":true}", "application/json; charset=utf-8");
                       } catch (const std::exception& ex) {
                         BadRequest(response, ex.what());
                       }
                     });

  impl_->server.Post("/api/insert-column",
                     [this](const httplib::Request& request, httplib::Response& response) {
                       if (!impl_->HasValidToken(request)) {
                         impl_->RejectUnauthorized(response);
                         return;
                       }
                       if (!impl_->RequireEditable(response)) {
                         return;
                       }
                       try {
                         impl_->data.insert_column(
                             JsonUintField(request.body, "column"),
                             JsonStringField(request.body, "name"),
                             JsonStringField(request.body, "value"));
                         response.set_content("{\"ok\":true}", "application/json; charset=utf-8");
                       } catch (const std::exception& ex) {
                         BadRequest(response, ex.what());
                       }
                     });

  impl_->server.Post("/api/delete-column",
                     [this](const httplib::Request& request, httplib::Response& response) {
                       if (!impl_->HasValidToken(request)) {
                         impl_->RejectUnauthorized(response);
                         return;
                       }
                       if (!impl_->RequireEditable(response)) {
                         return;
                       }
                       try {
                         impl_->data.delete_column(JsonStringField(request.body, "column"));
                         response.set_content("{\"ok\":true}", "application/json; charset=utf-8");
                       } catch (const std::exception& ex) {
                         BadRequest(response, ex.what());
                       }
                     });

  impl_->server.Post("/api/reset",
                     [this](const httplib::Request& request, httplib::Response& response) {
                       if (!impl_->HasValidToken(request)) {
                         impl_->RejectUnauthorized(response);
                         return;
                       }
                       if (!impl_->RequireEditable(response)) {
                         return;
                       }
                       try {
                         impl_->data.reset();
                         response.set_content("{\"ok\":true}", "application/json; charset=utf-8");
                       } catch (const std::exception& ex) {
                         response.status = 409;
                         response.set_content(std::string(ex.what()) + "\n",
                                              "text/plain; charset=utf-8");
                       }
                     });

  impl_->server.Post("/api/save",
                     [this](const httplib::Request& request, httplib::Response& response) {
                       if (!impl_->HasValidToken(request)) {
                         impl_->RejectUnauthorized(response);
                         return;
                       }
                       if (!impl_->RequireEditable(response)) {
                         return;
                       }
                       try {
                         impl_->data.save();
                         response.set_content("{\"ok\":true}", "application/json; charset=utf-8");
                       } catch (const std::exception& ex) {
                         response.status = 409;
                        response.set_content(std::string(ex.what()) + "\n",
                                              "text/plain; charset=utf-8");
                      }
                     });

  impl_->server.Post("/api/chart-config/heatmap",
                     [this](const httplib::Request& request, httplib::Response& response) {
                       if (!impl_->HasValidToken(request)) {
                         impl_->RejectUnauthorized(response);
                         return;
                       }
                       try {
                         response.set_content(AppendHeatmapChartConfig(
                                                  impl_->data, request.body, impl_->logger),
                                              "application/json; charset=utf-8");
                       } catch (const std::exception& ex) {
                         BadRequest(response, ex.what());
                       }
                     });

  impl_->server.Get("/api/chart-config",
                    [this](const httplib::Request& request, httplib::Response& response) {
                      if (!impl_->HasValidToken(request)) {
                        impl_->RejectUnauthorized(response);
                        return;
                      }
                      try {
                        response.set_content(BuildChartConfigListJson(impl_->data),
                                             "application/json; charset=utf-8");
                      } catch (const std::exception& ex) {
                        BadRequest(response, ex.what());
                      }
                    });

  impl_->server.Post("/api/chart-config/generate",
                     [this](const httplib::Request& request, httplib::Response& response) {
                       if (!impl_->HasValidToken(request)) {
                         impl_->RejectUnauthorized(response);
                         return;
                       }
                       try {
                         response.set_content(GenerateCurrentCsvChart(
                                                  impl_->data, request.body, impl_->logger),
                                              "application/json; charset=utf-8");
                       } catch (const std::exception& ex) {
                         BadRequest(response, ex.what());
                       }
                     });

  impl_->server.Get("/api/health",
                    [this](const httplib::Request& request, httplib::Response& response) {
                      if (!impl_->HasValidToken(request)) {
                        impl_->RejectUnauthorized(response);
                        return;
                      }
                      response.set_content(BuildHealthJson(),
                                           "application/json; charset=utf-8");
                      impl_->MaybeStopAfterRequest();
                    });

  if (options.requested_port == 0) {
    impl_->port = impl_->server.bind_to_any_port("127.0.0.1");
  } else if (impl_->server.bind_to_port("127.0.0.1", options.requested_port)) {
    impl_->port = options.requested_port;
  } else {
    impl_->port = -1;
  }

  if (impl_->port < 0) {
    if (impl_->logger.error) {
      impl_->logger.error("view: failed to bind a local HTTP port on 127.0.0.1");
    }
    return 1;
  }

  impl_->running = true;
  impl_->thread = std::thread([this]() {
    impl_->server.listen_after_bind();
    impl_->running = false;
  });
  impl_->server.wait_until_ready();
  return 0;
}

void ViewServer::Stop() {
  if (!impl_) {
    return;
  }
  if (impl_->running) {
    impl_->server.stop();
    impl_->running = false;
  }
  if (impl_->thread.joinable()) {
    impl_->thread.join();
  }
}

int ViewServer::Wait() {
  if (impl_->thread.joinable()) {
    impl_->thread.join();
  }
  impl_->running = false;
  return 0;
}

int ViewServer::bound_port() const {
  return impl_->port;
}

const std::string& ViewServer::session_token() const {
  return impl_->token;
}

std::string ViewServer::viewer_url() const {
  return "http://127.0.0.1:" + std::to_string(impl_->port) + "/?token=" + impl_->token;
}

std::string FormatViewStartupOutput(const std::string& url, const bool startup_json) {
  if (startup_json) {
    return BuildStartupJson(url);
  }
  return url;
}

int RunView(const std::string& input_path,
            std::ostream& output,
            const RunOptions& options,
            const LoggerCallbacks& logger,
            RunStats& stats,
            int requested_port,
            bool open_browser,
            bool serve_once,
            bool startup_json) {
  std::unique_ptr<CsvViewData> data;
  try {
    data = std::make_unique<CsvViewData>(CsvViewData::Open(input_path, options, logger, stats));
  } catch (const std::exception& ex) {
    if (logger.error) {
      logger.error(std::string("view: ") + ex.what());
    }
    return 1;
  }

  ViewServer server(*data, logger);
  if (const auto rc = server.Start(
          {requested_port, serve_once, options.view_edit, {}, options.view_asset_dir});
      rc != 0) {
    return rc;
  }

  const auto url = server.viewer_url();
  output << FormatViewStartupOutput(url, startup_json) << '\n';
  output.flush();

  if (logger.info) {
    logger.info(options.view_edit
                    ? "view: local-only editable viewer on 127.0.0.1"
                    : "view: local-only read-only viewer on 127.0.0.1");
    logger.info("view: " + std::to_string(data->row_count()) + " row(s) loaded in " +
                std::string(data->mode_name()) + " mode from " + input_path);
  }

  if (open_browser && !OpenBrowserUrl(url) && logger.info) {
    logger.info("view: could not open a browser automatically; open the printed URL manually");
  }

  return server.Wait();
}

}  // namespace csvzall::pipeline::commands
