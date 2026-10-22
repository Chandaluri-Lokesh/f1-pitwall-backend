#pragma once
#include <string>

namespace f1 {
namespace api {

struct ServerConfig {
    std::string host        = "0.0.0.0";
    int         port        = 8080;
    bool        cors_all    = true;   // allow all origins (dev mode)
    std::string data_dir    = "./data";
};

// Starts the blocking HTTP server. Returns non-zero on failure.
int run_server(const ServerConfig& cfg);

} // namespace api
} // namespace f1
