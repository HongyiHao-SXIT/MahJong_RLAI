// MahJong RLAI — C++ Backend Server
// WebSocket-based game server for Riichi Mahjong
//
// Usage:
//   server.exe -H 0.0.0.0 -P 9999 -A 0 -ob
//
// Connect with any WebSocket client (e.g. Godot WebSocketClient)
// Send join message: {"username":"Player1","observe":false}
// Then follow the NDJSON protocol.

#include "server.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

static MahJongServer* g_server = nullptr;

void signal_handler(int) {
    if (g_server) {
        LOG_INFO("Shutting down...");
        g_server->stop();
    }
}

void print_usage(const char* prog) {
    printf("MahJong RLAI Backend Server\n");
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -H, --host HOST       Server host (default: 0.0.0.0)\n");
    printf("  -P, --port PORT       Server port (default: 9999)\n");
    printf("  -A, --AI COUNT        Number of AI players (default: 0)\n");
    printf("  -m, --min-score N     Minimum score threshold (default: 0)\n");
    printf("  -ob, --allow-observe  Allow observing\n");
    printf("  -f, --fast            Skip AI thinking delay\n");
    printf("  -d, --debug           Enable debug logging\n");
    printf("  -h, --help            Show this help\n");
    printf("\n");
    printf("Protocol: WebSocket + NDJSON (same as existing server)\n");
}

int main(int argc, char* argv[]) {
    std::string host = "0.0.0.0";
    int port = 9999;
    int ai_count = 0;
    int min_score = 0;
    bool fast = false;
    bool allow_observe = false;
    bool debug = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "-H" || arg == "--host") && i + 1 < argc) {
            host = argv[++i];
        } else if ((arg == "-P" || arg == "--port") && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if ((arg == "-A" || arg == "--AI") && i + 1 < argc) {
            ai_count = std::stoi(argv[++i]);
        } else if ((arg == "-m" || arg == "--min-score") && i + 1 < argc) {
            min_score = std::stoi(argv[++i]);
        } else if (arg == "-ob" || arg == "--allow-observe") {
            allow_observe = true;
        } else if (arg == "-f" || arg == "--fast") {
            fast = true;
        } else if (arg == "-d" || arg == "--debug") {
            debug = true;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (debug) log_set_level(LOG_DEBUG);

    srand(static_cast<unsigned>(time(nullptr)));

    LOG_INFO("MahJong RLAI Server starting...");
    LOG_INFO("  Host: %s", host.c_str());
    LOG_INFO("  Port: %d", port);
    LOG_INFO("  AI players: %d", ai_count);
    LOG_INFO("  Fast mode: %s", fast ? "yes" : "no");
    LOG_INFO("  Observe: %s", allow_observe ? "yes" : "no");

    MahJongServer server(host, port, ai_count, min_score, fast, allow_observe);

    g_server = &server;
#ifdef _WIN32
    SetConsoleCtrlHandler([](DWORD) -> BOOL {
        signal_handler(0);
        return TRUE;
    }, TRUE);
#endif

    if (!server.start()) {
        LOG_ERROR("Failed to start server");
        return 1;
    }

    server.wait();
    return 0;
}
