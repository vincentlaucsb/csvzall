#include "../view.hpp"

#include "assets.hpp"
#include "charts.hpp"
#include "json.hpp"
#include "support.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include "../../../../vendor/httplib/httplib.h"

namespace csvzall::pipeline::commands {

namespace {

using view_internal::AppendHeatmapChartConfig;
using view_internal::BuildChartConfigListJson;
using view_internal::BuildHealthJson;
using view_internal::BuildRowsJson;
using view_internal::BuildSchemaJson;
using view_internal::GenerateCurrentCsvChart;
using view_internal::GenerateSessionToken;
using view_internal::JsonStringArrayField;
using view_internal::JsonStringArrayFieldOr;
using view_internal::JsonStringField;
using view_internal::JsonUintField;
using view_internal::RenderRunOnSaveChartsForCurrentCsv;
using view_internal::ResolveDevViewerAssetDir;
using view_internal::SaveResultJson;
using view_internal::ServeEmbeddedViewerAsset;
using view_internal::ServeViewerAsset;
using view_internal::kDefaultRowsPerPage;
using view_internal::kMaxRowsPerPage;
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
}  // namespace
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

  impl_->server.Post("/api/swap-rows",
                     [this](const httplib::Request& request, httplib::Response& response) {
                       if (!impl_->HasValidToken(request)) {
                         impl_->RejectUnauthorized(response);
                         return;
                       }
                       if (!impl_->RequireEditable(response)) {
                         return;
                       }
                       try {
                         impl_->data.swap_rows(
                             JsonUintField(request.body, "first"),
                             JsonUintField(request.body, "second"));
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
                         impl_->data.save(
                             JsonStringArrayFieldOr(request.body, "columns", {}));
                         std::size_t charts_generated = 0;
                         std::string chart_error;
                         try {
                           charts_generated = RenderRunOnSaveChartsForCurrentCsv(
                               impl_->data, impl_->logger);
                         } catch (const std::exception& ex) {
                           chart_error = ex.what();
                           if (impl_->logger.error) {
                             impl_->logger.error(chart_error);
                           }
                         }
                         response.set_content(SaveResultJson(charts_generated, chart_error),
                                              "application/json; charset=utf-8");
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
}  // namespace csvzall::pipeline::commands
