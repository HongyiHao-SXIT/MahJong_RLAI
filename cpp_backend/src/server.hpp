#pragma once
// MahJong WebSocket Server — integrates WSConnection with GameClient/GameEnvironment

#include "game_engine.hpp"
#include "json.hpp"
#include "websocket.hpp"

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

// ----------------------------------------------------------------
// WSGameClient — adapter between WebSocket connections and game clients
// ----------------------------------------------------------------
class WSGameClient : public GameClient {
public:
    WSGameClient(std::shared_ptr<WSConnection> ws, const std::string& name)
        : m_ws(ws), m_name(name), m_queue(std::make_unique<ControlledQueue<json::Value>>()) {
        ws->on_message = [this](const std::string& msg) {
            try {
                json::Value v = json::parse(msg);
                std::string event;
                if (v.contains("event")) event = v["event"].as_string();
                if (event == "discard" || event == "decision" || event == "ready" || event == "quit") {
                    m_queue->put(v);
                }
            } catch (...) {}
        };
        ws->on_close = [this]() {
            json::Value quit_msg;
            quit_msg["event"] = "quit";
            m_queue->put(quit_msg);
        };
    }

    ~WSGameClient() override = default;

    void send_json(const json::Value& msg) override {
        if (m_ws && !m_ws->closed) {
            std::string data = json::serialize(msg);
            m_ws->send_text(data);
        }
    }

    json::Value fetch_message() override {
        return m_queue->get();
    }

    void set_queue_allow_put() override {
        m_queue->set_allow_put();
    }

    void clear_queue() override {
        m_queue->clear();
    }

    bool is_connected() const override {
        return m_ws && !m_ws->closed;
    }

    bool is_ai() const override {
        return false; // WS clients are human players
    }

    std::string username() const override {
        return m_name;
    }

    int seat() const override { return m_seat; }
    void set_seat(int s) override { m_seat = s; }

    std::shared_ptr<WSConnection> ws() const { return m_ws; }

private:
    std::shared_ptr<WSConnection> m_ws;
    std::string m_name;
    std::unique_ptr<ControlledQueue<json::Value>> m_queue;
    int m_seat = -1;
};

// ----------------------------------------------------------------
// AIClient — AI player that uses the AIProxy or built-in random AI
// ----------------------------------------------------------------
class AIClient : public GameClient {
public:
    AIClient(const std::string& name, int idx, GameEnvironment* env = nullptr)
        : m_name(name), m_idx(idx), m_env(env),
          m_queue(std::make_unique<ControlledQueue<json::Value>>()) {}

    void send_json(const json::Value& msg) override {
        // AI doesn't need to receive messages, but we log them
        LOG_DEBUG("AI %s recv: %s", m_name.c_str(), json::serialize(msg).c_str());
    }

    json::Value fetch_message() override {
        // AI generates a decision automatically
        // For now, the game loop handles AI by calling ai_discard directly
        // This method shouldn't be called for AI clients
        json::Value v;
        v["event"] = "discard";
        v["tile_id"] = 0;
        return v;
    }

    void set_queue_allow_put() override {}
    void clear_queue() override {}

    bool is_connected() const override { return true; }
    bool is_ai() const override { return true; }
    std::string username() const override { return m_name; }
    int seat() const override { return m_seat; }
    void set_seat(int s) override { m_seat = s; }

private:
    std::string m_name;
    int m_idx = 0;
    int m_seat = -1;
    GameEnvironment* m_env = nullptr;
    std::unique_ptr<ControlledQueue<json::Value>> m_queue;
};

// ----------------------------------------------------------------
// MahJongServer — main server class
// ----------------------------------------------------------------
class MahJongServer {
public:
    MahJongServer(const std::string& host, int port, int ai_count,
                  int min_score, bool fast, bool allow_observe)
        : m_host(host), m_port(port),
          m_game_env(true, ai_count, min_score, fast, allow_observe),
          m_ai_count(ai_count) {

        // Set up send callbacks
        m_game_env.send_personal = [this](GameClient& client, const json::Value& msg) {
            client.send_json(msg);
        };

        m_game_env.send_multiply = [this](GameEnvironment& env, const json::Value& msg,
                                           int except, int except_ob) {
            std::string data = json::serialize(msg);
            for (int i = 0; i < 4; i++) {
                if (i == except || !env.clients[i]->is_connected()) continue;
                env.clients[i]->send_json(msg);
            }
        };

        // Create AI clients
        for (int i = 0; i < ai_count; i++) {
            std::string name = "一姬" + std::to_string(i + 1) + "(简单)";
            auto ai = std::make_unique<AIClient>(name, i, &m_game_env);
            m_game_env.clients.push_back(std::move(ai));
        }
    }

    ~MahJongServer() {
        stop();
    }

    bool start() {
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            LOG_ERROR("WSAStartup failed");
            return false;
        }
#endif

        m_listen_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (m_listen_sock == INVALID_SOCKET) {
            LOG_ERROR("Failed to create socket");
            return false;
        }

        int opt = 1;
        setsockopt(m_listen_sock, SOL_SOCKET, SO_REUSEADDR,
                   (const char*)&opt, sizeof(opt));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(m_port));
        addr.sin_addr.s_addr = (m_host == "0.0.0.0") ? INADDR_ANY : inet_addr(m_host.c_str());

        if (bind(m_listen_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            LOG_ERROR("Bind failed on %s:%d", m_host.c_str(), m_port);
            return false;
        }

        if (listen(m_listen_sock, 5) == SOCKET_ERROR) {
            LOG_ERROR("Listen failed");
            return false;
        }

        int flag = 1;
        setsockopt(m_listen_sock, IPPROTO_TCP, TCP_NODELAY,
                   (const char*)&flag, sizeof(flag));

        LOG_INFO("Server listening on %s:%d", m_host.c_str(), m_port);

        m_running = true;
        m_accept_thread = std::thread(&MahJongServer::accept_loop, this);

        // Main loop: check if game can start
        m_main_thread = std::thread([this]() {
            while (m_running) {
                {
                    std::lock_guard<std::mutex> lk(m_mutex);
                    if (m_game_env.clients.size() >= 4 && !m_game_env.game_start) {
                        start_game();
                        // After game ends, loop back to accept more players
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        });

        return true;
    }

    void stop() {
        m_running = false;
        if (m_listen_sock != INVALID_SOCKET) {
            closesocket(m_listen_sock);
            m_listen_sock = INVALID_SOCKET;
        }
        if (m_accept_thread.joinable()) m_accept_thread.join();
        if (m_main_thread.joinable()) m_main_thread.join();
        for (auto& client : m_game_env.clients) {
            if (auto* wsgc = dynamic_cast<WSGameClient*>(client.get())) {
                wsgc->ws()->close();
            }
        }
#ifdef _WIN32
        WSACleanup();
#endif
    }

    void wait() {
        if (m_main_thread.joinable()) m_main_thread.join();
    }

private:
    std::string m_host;
    int m_port;
    SOCKET m_listen_sock = INVALID_SOCKET;
    std::atomic<bool> m_running{false};

    GameEnvironment m_game_env;
    int m_ai_count;
    std::mutex m_mutex;
    std::thread m_accept_thread;
    std::thread m_main_thread;

    // Pending handshake buffer for WebSocket connections
    std::map<SOCKET, std::string> m_handshake_buf;

    void start_game() {
        LOG_INFO("Starting game with %zu players", m_game_env.clients.size());
        std::thread game_thread([this]() {
            game_main_loop(m_game_env);
            // After game ends: re-add AI clients for next game
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                // Keep human WS clients that are still connected
                std::vector<std::unique_ptr<GameClient>> human_clients;
                for (auto& c : m_game_env.clients) {
                    if (!c->is_ai() && c->is_connected()) {
                        human_clients.push_back(std::move(c));
                    }
                }
                m_game_env.clients.clear();
                // Re-add human clients
                for (auto& hc : human_clients)
                    m_game_env.clients.push_back(std::move(hc));
                // Re-add AI clients
                int existing_humans = static_cast<int>(m_game_env.clients.size());
                for (int i = 0; i < m_ai_count; i++) {
                    std::string name = "一姬" + std::to_string(i + 1) + "(简单)";
                    auto ai = std::make_unique<AIClient>(name, i, &m_game_env);
                    m_game_env.clients.push_back(std::move(ai));
                }
                LOG_INFO("Game ended, %d humans + %d AI ready",
                    existing_humans, m_ai_count);
            }
        });
        game_thread.detach();
    }

    void accept_loop() {
        while (m_running) {
            // Check for new connections with timeout
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(m_listen_sock, &readfds);
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 200000; // 200ms

            int ret = select(static_cast<int>(m_listen_sock) + 1,
                             &readfds, nullptr, nullptr, &tv);
            if (ret <= 0) continue;

            SOCKET client_sock = accept(m_listen_sock, nullptr, nullptr);
            if (client_sock == INVALID_SOCKET) continue;

            LOG_DEBUG("New connection accepted");

            // Read HTTP upgrade request (WebSocket handshake)
            std::string http_req;
            char buf[4096];
            int n = recv(client_sock, buf, sizeof(buf) - 1, 0);
            if (n <= 0) {
                closesocket(client_sock);
                continue;
            }
            buf[n] = '\0';
            http_req = buf;

            // Check if it looks like a WebSocket upgrade request
            if (http_req.find("Upgrade: websocket") == std::string::npos &&
                http_req.find("upgrade: websocket") == std::string::npos) {
                LOG_ERROR("Non-WebSocket connection attempt, closing");
                closesocket(client_sock);
                continue;
            }

            // Perform WebSocket handshake
            auto ws = std::make_shared<WSConnection>(client_sock);
            try {
                std::string path = ws->perform_handshake(http_req);
                LOG_DEBUG("WebSocket handshake done, path: %s", path.c_str());

                // Now wait for the join message (first JSON after handshake)
                // Read it synchronously
                WSFrame frame;
                if (!ws->read_frame(frame, 5000000)) { // 5s timeout
                    LOG_ERROR("Timeout waiting for join message");
                    ws->close();
                    continue;
                }

                if (frame.opcode != WS_Opcode::Text || frame.payload.empty()) {
                    LOG_ERROR("Expected text frame for join");
                    ws->close();
                    continue;
                }

                json::Value join_msg = json::parse(frame.payload);
                std::string username;
                if (join_msg.contains("username")) {
                    username = join_msg["username"].as_string();
                }
                bool observe = false;
                if (join_msg.contains("observe")) {
                    observe = join_msg["observe"].as_bool();
                }

                LOG_INFO("Player join: %s (observe=%d)", username.c_str(), observe);

                if (observe) {
                    // Observer mode (simplified)
                    json::Value resp;
                    resp["event"] = "join";
                    resp["status"] = -1;
                    resp["message"] = "已加入观战";
                    ws->send_text(json::serialize(resp));
                    // TODO: Implement full observer support
                    continue;
                }

                std::lock_guard<std::mutex> lk(m_mutex);

                // Check name collision / reconnection
                bool name_taken = false;
                for (auto& c : m_game_env.clients) {
                    if (c->username() == username && c->is_connected()) {
                        name_taken = true;
                        break;
                    }
                }

                if (name_taken) {
                    json::Value resp;
                    resp["event"] = "join";
                    resp["status"] = 0;
                    resp["message"] = "用户名已被占用";
                    ws->send_text(json::serialize(resp));
                    ws->close();
                    continue;
                }

                if (m_game_env.clients.size() >= 4) {
                    // Room full — can demote to observer in future
                    json::Value resp;
                    resp["event"] = "join";
                    resp["status"] = 0;
                    resp["message"] = "房间人数已满";
                    ws->send_text(json::serialize(resp));
                    ws->close();
                    continue;
                }

                // Add player
                auto game_client = std::make_unique<WSGameClient>(ws, username);
                auto* raw_ptr = game_client.get();
                m_game_env.clients.push_back(std::move(game_client));

                json::Value resp;
                resp["event"] = "join";
                resp["status"] = 1;
                resp["message"] = "成功加入房间, 您的用户名为「" + username + "」";
                ws->send_text(json::serialize(resp));

                // Start reading messages from this client in a separate thread
                std::thread(&MahJongServer::client_read_loop, this, raw_ptr).detach();

                // Broadcast player count
                json::Value broadcast;
                broadcast["event"] = "join";
                broadcast["message"] = "当前人数:" + std::to_string(m_game_env.clients.size()) + ", 等待其他玩家加入...";
                for (auto& c : m_game_env.clients) {
                    c->send_json(broadcast);
                }

            } catch (const std::exception& e) {
                LOG_ERROR("Handshake error: %s", e.what());
                ws->close();
            }
        }
    }

    void client_read_loop(WSGameClient* client) {
        auto ws = client->ws();
        while (m_running && ws && !ws->closed) {
            if (!ws->process_read()) {
                // Connection closed
                LOG_INFO("Player %s disconnected", client->username().c_str());
                break;
            }
        }
    }
};
