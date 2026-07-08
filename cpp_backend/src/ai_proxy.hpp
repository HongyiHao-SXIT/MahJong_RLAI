#pragma once
// AI Proxy — communicates with Python AI engine via TCP/pipe

#include "json.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define SOCKET int
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR (-1)
    #define closesocket close
#endif

// AIProxy connects to a Python AI engine process and forwards
// game state / receives AI decisions.
class AIProxy {
public:
    AIProxy(const std::string& host = "127.0.0.1", int port = 8888)
        : m_host(host), m_port(port) {}

    ~AIProxy() { disconnect(); }

    bool connect() {
#ifdef _WIN32
        m_sock = socket(AF_INET, SOCK_STREAM, 0);
#else
        m_sock = socket(AF_INET, SOCK_STREAM, 0);
#endif
        if (m_sock == INVALID_SOCKET) return false;

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(m_port));
        addr.sin_addr.s_addr = inet_addr(m_host.c_str());

        if (::connect(m_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            closesocket(m_sock);
            m_sock = INVALID_SOCKET;
            return false;
        }

        m_connected = true;
        LOG_INFO("AIProxy connected to %s:%d", m_host.c_str(), m_port);
        return true;
    }

    void disconnect() {
        m_connected = false;
        if (m_sock != INVALID_SOCKET) {
            closesocket(m_sock);
            m_sock = INVALID_SOCKET;
        }
    }

    bool is_connected() const { return m_connected; }

    // Send game state to AI engine, get back a decision
    json::Value request_decision(const json::Value& state) {
        if (!m_connected) {
            // Fallback: simple random AI
            return fallback_decision(state);
        }

        std::string data = json::serialize(state) + "\n";
        if (::send(m_sock, data.c_str(), static_cast<int>(data.size()), 0) <= 0) {
            LOG_WARN("AIProxy send failed, using fallback");
            disconnect();
            return fallback_decision(state);
        }

        // Read response
        std::string response;
        char ch;
        while (recv(m_sock, &ch, 1, 0) > 0) {
            if (ch == '\n') break;
            response += ch;
        }

        if (response.empty()) {
            LOG_WARN("AIProxy empty response, using fallback");
            disconnect();
            return fallback_decision(state);
        }

        return json::parse(response);
    }

    // Request discard decision
    int request_discard(const json::Value& state, const std::vector<int>& tiles,
                        const std::vector<int>& banned) {
        json::Value req = state;
        req["type"] = "discard";
        json::Value tiles_arr, banned_arr;
        for (int t : tiles) tiles_arr.push_back(t);
        for (int b : banned) banned_arr.push_back(b);
        req["tiles"] = tiles_arr;
        req["banned"] = banned_arr;

        auto resp = request_decision(req);
        if (resp.contains("tile_id")) {
            return resp["tile_id"].as_int();
        }
        // Fallback: random
        return fallback_discard(tiles, banned);
    }

private:
    std::string m_host;
    int m_port = 8888;
    SOCKET m_sock = INVALID_SOCKET;
    std::atomic<bool> m_connected{false};

    json::Value fallback_decision(const json::Value& state) {
        json::Value resp;
        resp["type"] = "pass";
        return resp;
    }

    int fallback_discard(const std::vector<int>& tiles, const std::vector<int>& banned) {
        std::vector<int> candidates;
        for (int t : tiles) {
            if (std::find(banned.begin(), banned.end(), tile_type(t)) == banned.end())
                candidates.push_back(t);
        }
        if (candidates.empty()) candidates = tiles;
        return candidates[rand() % candidates.size()];
    }
};
