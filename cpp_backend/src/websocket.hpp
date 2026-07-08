#pragma once
// WebSocket (RFC 6455) server implementation
// Minimal, self-contained, no external dependencies.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
#include <sstream>
#include <array>
#include <functional>

#ifdef _WIN32
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600
    #endif
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <signal.h>
    #define SOCKET int
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR (-1)
    #define closesocket close
#endif

// ----------------------------------------------------------------
// SHA-1 implementation for WebSocket handshake
// ----------------------------------------------------------------
namespace sha1 {

// Rotate left
inline uint32_t rotl(uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

inline std::string digest(const std::string& input) {
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    // Pre-processing
    uint64_t ml = input.size() * 8;
    std::vector<uint8_t> msg(input.begin(), input.end());
    msg.push_back(0x80);
    while ((msg.size() * 8) % 512 != 448) msg.push_back(0x00);
    for (int i = 7; i >= 0; i--) msg.push_back(static_cast<uint8_t>((ml >> (i * 8)) & 0xFF));

    // Process chunks
    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = (msg[chunk + i * 4] << 24) | (msg[chunk + i * 4 + 1] << 16) |
                   (msg[chunk + i * 4 + 2] << 8) | msg[chunk + i * 4 + 3];
        }
        for (int i = 16; i < 80; i++)
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t temp = rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl(b, 30); b = a; a = temp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    // Output as raw bytes (big-endian)
    std::string result;
    for (uint32_t val : {h0, h1, h2, h3, h4}) {
        result.push_back(static_cast<char>((val >> 24) & 0xFF));
        result.push_back(static_cast<char>((val >> 16) & 0xFF));
        result.push_back(static_cast<char>((val >> 8) & 0xFF));
        result.push_back(static_cast<char>(val & 0xFF));
    }
    return result;
}
} // namespace sha1

// ----------------------------------------------------------------
// Base64 for WebSocket handshake
// ----------------------------------------------------------------
namespace base64 {
inline std::string encode(const std::string& in) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        out.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (out.size() % 4) out.push_back('=');
    return out;
}
} // namespace base64

// ----------------------------------------------------------------
// WebSocket opcodes
// ----------------------------------------------------------------
enum class WS_Opcode : uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA
};

// ----------------------------------------------------------------
// WebSocket frame
// ----------------------------------------------------------------
struct WSFrame {
    bool fin = true;
    WS_Opcode opcode = WS_Opcode::Text;
    bool masked = false;
    uint8_t mask_key[4] = {0};
    std::string payload;

    // Encode frame for sending (server → client: no mask)
    std::string encode() const {
        std::string header;
        header.push_back(static_cast<uint8_t>((fin ? 0x80 : 0x00) | static_cast<uint8_t>(opcode)));

        size_t len = payload.size();
        if (len < 126) {
            header.push_back(static_cast<uint8_t>(len)); // no mask for server frames
        } else if (len <= 0xFFFF) {
            header.push_back(126);
            header.push_back(static_cast<char>((len >> 8) & 0xFF));
            header.push_back(static_cast<char>(len & 0xFF));
        } else {
            header.push_back(127);
            for (int i = 7; i >= 0; i--)
                header.push_back(static_cast<char>((len >> (i * 8)) & 0xFF));
        }

        return header + payload;
    }
};

// ----------------------------------------------------------------
// WebSocket connection handler
// ----------------------------------------------------------------
class WSConnection {
public:
    SOCKET sock = INVALID_SOCKET;
    bool handshake_done = false;
    bool closed = false;

    // Internal read buffer
    std::string pending_buf;

    // Callbacks
    std::function<void(const std::string&)> on_message;
    std::function<void()> on_close;

    WSConnection() = default;
    explicit WSConnection(SOCKET s) : sock(s) {}
    ~WSConnection() { close(); }

    WSConnection(WSConnection&& other) noexcept
        : sock(other.sock), handshake_done(other.handshake_done),
          closed(other.closed),
          pending_buf(std::move(other.pending_buf)),
          on_message(std::move(other.on_message)), on_close(std::move(other.on_close)) {
        other.sock = INVALID_SOCKET;
        other.closed = true;
    }

    WSConnection& operator=(WSConnection&& other) noexcept {
        if (this != &other) {
            close();
            sock = other.sock;
            handshake_done = other.handshake_done;
            closed = other.closed;
            pending_buf = std::move(other.pending_buf);
            on_message = std::move(other.on_message);
            on_close = std::move(other.on_close);
            other.sock = INVALID_SOCKET;
            other.closed = true;
        }
        return *this;
    }

    WSConnection(const WSConnection&) = delete;
    WSConnection& operator=(const WSConnection&) = delete;

    void close() {
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
        closed = true;
    }

    // Perform WebSocket handshake from raw HTTP upgrade request
    // Returns the HTTP payload (first line after headers) if any
    std::string perform_handshake(const std::string& http_request) {
        // Parse the HTTP request
        std::string key;
        std::string path;
        std::istringstream stream(http_request);
        std::string line;

        // Request line
        std::getline(stream, line);
        {
            auto sp1 = line.find(' ');
            if (sp1 == std::string::npos) return {};
            auto sp2 = line.find(' ', sp1 + 1);
            if (sp2 == std::string::npos) return {};
            path = line.substr(sp1 + 1, sp2 - sp1 - 1);
        }

        // Headers
        while (std::getline(stream, line) && !line.empty() && line != "\r") {
            if (line.back() == '\r') line.pop_back();
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string hdr_name = line.substr(0, colon);
            std::string hdr_val = line.substr(colon + 1);
            // Trim leading spaces
            while (!hdr_val.empty() && hdr_val[0] == ' ') hdr_val.erase(0, 1);

            if (hdr_name == "Sec-WebSocket-Key") {
                key = hdr_val;
            }
        }

        if (key.empty()) {
            throw std::runtime_error("Missing Sec-WebSocket-Key in handshake");
        }

        // Compute accept key
        std::string accept_key = base64::encode(
            sha1::digest(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));

        // Send upgrade response
        std::string response =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + accept_key + "\r\n"
            "\r\n";

        ::send(sock, response.c_str(), static_cast<int>(response.size()), 0);
        handshake_done = true;

        // Return path for routing
        return path;
    }

    // Read and decode one WebSocket frame from socket (non-blocking check + blocking read)
    // Returns true if a complete frame was read, false if would block
    bool read_frame(WSFrame& frame, int timeout_us = 100000) {
        if (closed || sock == INVALID_SOCKET) return false;

        // Check if data available
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = timeout_us;
        int ret = select(static_cast<int>(sock) + 1, &readfds, nullptr, nullptr, &tv);
        if (ret <= 0) return false;

        // Read first 2 bytes (header)
        uint8_t hdr[2];
        int n = recv(sock, reinterpret_cast<char*>(hdr), 2, MSG_WAITALL);
        if (n != 2) { closed = true; return false; }

        frame.fin = (hdr[0] & 0x80) != 0;
        frame.opcode = static_cast<WS_Opcode>(hdr[0] & 0x0F);
        frame.masked = (hdr[1] & 0x80) != 0;

        size_t len = hdr[1] & 0x7F;
        if (len == 126) {
            uint8_t ext[2];
            if (recv(sock, reinterpret_cast<char*>(ext), 2, MSG_WAITALL) != 2) return false;
            len = (static_cast<size_t>(ext[0]) << 8) | ext[1];
        } else if (len == 127) {
            uint8_t ext[8];
            if (recv(sock, reinterpret_cast<char*>(ext), 8, MSG_WAITALL) != 8) return false;
            len = 0;
            for (int i = 0; i < 8; i++) len = (len << 8) | ext[i];
        }

        // Read mask key
        if (frame.masked) {
            if (recv(sock, reinterpret_cast<char*>(frame.mask_key), 4, MSG_WAITALL) != 4) return false;
        }

        // Read payload
        frame.payload.resize(len);
        if (len > 0) {
            size_t total_read = 0;
            while (total_read < len) {
                int n = recv(sock, frame.payload.data() + total_read,
                             static_cast<int>(len - total_read), 0);
                if (n <= 0) { closed = true; return false; }
                total_read += n;
            }
        }

        // Unmask if needed
        if (frame.masked) {
            for (size_t i = 0; i < len; i++)
                frame.payload[i] ^= frame.mask_key[i % 4];
        }

        // Handle control frames
        if (frame.opcode == WS_Opcode::Close) {
            closed = true;
            if (on_close) on_close();
            return false;
        }
        if (frame.opcode == WS_Opcode::Ping) {
            send_frame(WSFrame{true, WS_Opcode::Pong, false, {0}, frame.payload});
            return false; // Don't return as a message; continue reading
        }
        if (frame.opcode == WS_Opcode::Pong) {
            return false; // Just ignore
        }

        return true;
    }

    // Send a WebSocket frame
    void send_frame(const WSFrame& frame) {
        if (closed || sock == INVALID_SOCKET) return;
        std::string data = frame.encode();
        ::send(sock, data.data(), static_cast<int>(data.size()), 0);
    }

    // Convenience: send text message
    void send_text(const std::string& text) {
        WSFrame frame;
        frame.fin = true;
        frame.opcode = WS_Opcode::Text;
        frame.payload = text;
        send_frame(frame);
    }

    // Convenience: send binary message
    void send_binary(const std::string& data) {
        WSFrame frame;
        frame.fin = true;
        frame.opcode = WS_Opcode::Binary;
        frame.payload = data;
        send_frame(frame);
    }

    // Process incoming data: reads frames and calls callbacks
    // Returns false if connection should be closed
    bool process_read() {
        if (closed) return false;

        WSFrame frame;
        if (!read_frame(frame, 10000)) {
            return !closed; // if closed, return false; otherwise true (continue)
        }

        // Handle based on opcode
        if (frame.opcode == WS_Opcode::Text) {
            if (on_message) on_message(frame.payload);
        } else if (frame.opcode == WS_Opcode::Binary) {
            if (on_message) on_message(frame.payload);
        }

        return true;
    }

private:
};
