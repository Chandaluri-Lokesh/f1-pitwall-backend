#include "api/server.h"
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    f1::api::ServerConfig cfg;

    if (argc >= 2) cfg.port     = std::atoi(argv[1]);
    if (argc >= 3) cfg.data_dir = argv[2];

    std::cout << "[F1 PitWall] Starting API server on "
              << cfg.host << ":" << cfg.port << "\n";

    return f1::api::run_server(cfg);
}
