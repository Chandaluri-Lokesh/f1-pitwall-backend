#include "api/server.h"
#include "db/db_reader.h"
#include "httplib.h"
#include <iostream>

// Route registration (defined in routes_coa.cpp)
namespace f1 { namespace api {
    void register_coa_routes(httplib::Server& svr);
}}

namespace f1 {
namespace api {

int run_server(const ServerConfig& cfg) {
    // Open DB before registering routes (all route handlers need it)
    try {
        db::open(cfg.data_dir + "/pitwall.db");
    } catch (const std::exception& e) {
        std::cerr << "[WARN] DB open failed: " << e.what()
                  << " — routes will use defaults\n";
    }

    httplib::Server svr;

    if (cfg.cors_all) {
        svr.set_default_headers({
            { "Access-Control-Allow-Origin",  "*"                },
            { "Access-Control-Allow-Headers", "Content-Type"     },
            { "Access-Control-Allow-Methods", "GET, POST, OPTIONS" }
        });
        svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
            res.status = 204;
        });
    }

    // Health check (both paths: bare /health and /api/health used by frontend proxy)
    auto health_handler = [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ok\"}", "application/json");
    };
    svr.Get("/health",      health_handler);
    svr.Get("/api/health",  health_handler);

    register_coa_routes(svr);

    std::cout << "[API] Listening on " << cfg.host << ":" << cfg.port << "\n";
    svr.listen(cfg.host.c_str(), cfg.port);
    return 0;
}

} // namespace api
} // namespace f1
