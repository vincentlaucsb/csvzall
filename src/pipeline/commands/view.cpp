#include "view.hpp"

#include "view/json.hpp"

#include <cstdlib>
#include <cstdint>
#include <exception>
#include <memory>
#include <ostream>
#include <string>

#ifdef _WIN32
#include <shellapi.h>
#include <windows.h>
#endif

namespace csvzall::pipeline::commands {

namespace {

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

}  // namespace

std::string FormatViewStartupOutput(const std::string& url, const bool startup_json) {
  if (startup_json) {
    return view_internal::BuildStartupJson(url);
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
