// Wire protocol helpers for JSON-over-TCP transport.
// SPDX-License-Identifier: MIT
//
// Wire format:
//   [4-byte big-endian length][JSON payload of that length]
//
// JSON format:
//   {"type":"request","method":"inspect","id":1,"payload":{...}}
//
// No compression, no TLS (can be added later).

#pragma once

#include "vxl/transport.h"
#include "vxl/error.h"

#include <string>
#include <cstdint>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    using socket_t = SOCKET;
    #define VXL_INVALID_SOCKET INVALID_SOCKET
    #define VXL_SOCKET_ERROR   SOCKET_ERROR
    inline int vxl_close_socket(socket_t s) { return closesocket(s); }
    inline int vxl_socket_errno() { return WSAGetLastError(); }
#else
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    using socket_t = int;
    #define VXL_INVALID_SOCKET (-1)
    #define VXL_SOCKET_ERROR   (-1)
    inline int vxl_close_socket(socket_t s) { return ::close(s); }
    inline int vxl_socket_errno() { return errno; }
#endif

namespace vxl {
namespace proto {

// ---- Socket helpers --------------------------------------------------------

inline bool set_nonblocking(socket_t sock) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

inline bool set_blocking(socket_t sock) {
#ifdef _WIN32
    u_long mode = 0;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(sock, F_SETFL, flags & ~O_NONBLOCK) == 0;
#endif
}

inline void set_reuse_addr(socket_t sock) {
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));
}

inline void set_tcp_nodelay(socket_t sock) {
    int opt = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&opt), sizeof(opt));
}

// ---- Low-level send/recv with timeout (blocking) ---------------------------

// Send exactly `len` bytes. Returns true on success.
inline bool send_all(socket_t sock, const void* data, size_t len) {
    const char* p = static_cast<const char*>(data);
    size_t remaining = len;
    while (remaining > 0) {
        auto n = ::send(sock, p, static_cast<int>(remaining), 0);
        if (n <= 0) return false;
        p += n;
        remaining -= static_cast<size_t>(n);
    }
    return true;
}

// Receive exactly `len` bytes with timeout_ms (-1 = infinite).
// Returns true on success, false on timeout/error/disconnect.
inline bool recv_all(socket_t sock, void* data, size_t len, int timeout_ms) {
    char* p = static_cast<char*>(data);
    size_t remaining = len;
    while (remaining > 0) {
        if (timeout_ms >= 0) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(sock, &fds);
            struct timeval tv;
            tv.tv_sec  = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            int sel = ::select(static_cast<int>(sock) + 1, &fds, nullptr, nullptr, &tv);
            if (sel <= 0) return false; // timeout or error
        }
        auto n = ::recv(sock, p, static_cast<int>(remaining), 0);
        if (n <= 0) return false;
        p += n;
        remaining -= static_cast<size_t>(n);
    }
    return true;
}

// ---- Message serialization -------------------------------------------------

// Serialize TransportMessage to JSON string (minimal, no external dependency).
inline std::string escape_json_string(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

inline std::string serialize_message(const TransportMessage& msg) {
    // Build JSON manually to avoid nlohmann_json dependency in the header.
    std::string json;
    json += "{\"type\":\"";
    json += escape_json_string(msg.type);
    json += "\",\"method\":\"";
    json += escape_json_string(msg.method);
    json += "\",\"id\":";
    json += std::to_string(msg.id);
    json += ",\"payload\":";
    // payload is already a JSON string -- embed it directly
    if (msg.payload.empty() || msg.payload == "{}") {
        json += "{}";
    } else {
        json += msg.payload;
    }
    json += "}";
    return json;
}

// Minimal JSON token extraction (good enough for our simple protocol).
// Returns the value for a given key in a flat JSON object.
inline std::string json_get_string(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            char c = json[pos + 1];
            switch (c) {
                case '"':  result += '"';  break;
                case '\\': result += '\\'; break;
                case 'n':  result += '\n'; break;
                case 't':  result += '\t'; break;
                case 'r':  result += '\r'; break;
                case 'b':  result += '\b'; break;
                case 'f':  result += '\f'; break;
                default:   result += c;    break;
            }
            pos += 2;
        } else {
            result += json[pos];
            pos++;
        }
    }
    return result;
}

inline int64_t json_get_int(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    pos += needle.size();
    // skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    return std::stoll(json.substr(pos));
}

inline std::string json_get_object(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "{}";
    pos += needle.size();
    // skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size() || json[pos] != '{') return "{}";
    // Count braces to find matching close
    int depth = 0;
    size_t start = pos;
    for (; pos < json.size(); ++pos) {
        if (json[pos] == '{') depth++;
        else if (json[pos] == '}') {
            depth--;
            if (depth == 0) return json.substr(start, pos - start + 1);
        }
        // Skip strings (avoid counting braces inside strings)
        else if (json[pos] == '"') {
            pos++;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\') pos++;
                pos++;
            }
        }
    }
    return "{}";
}

inline TransportMessage deserialize_message(const std::string& json) {
    TransportMessage msg;
    msg.type    = json_get_string(json, "type");
    msg.method  = json_get_string(json, "method");
    msg.id      = json_get_int(json, "id");
    msg.payload = json_get_object(json, "payload");
    return msg;
}

// ---- Framed send/recv (4-byte length prefix + JSON) ------------------------

inline bool send_message(socket_t sock, const TransportMessage& msg) {
    std::string json = serialize_message(msg);
    uint32_t len = htonl(static_cast<uint32_t>(json.size()));
    if (!send_all(sock, &len, 4)) return false;
    if (!send_all(sock, json.data(), json.size())) return false;
    return true;
}

// Receive one framed message. Returns empty-type message on failure.
inline Result<TransportMessage> recv_message(socket_t sock, int timeout_ms = -1) {
    uint32_t net_len = 0;
    if (!recv_all(sock, &net_len, 4, timeout_ms)) {
        return Result<TransportMessage>::failure(ErrorCode::IO_READ_FAILED, "recv header failed or timed out");
    }
    uint32_t len = ntohl(net_len);
    if (len == 0 || len > 16 * 1024 * 1024) { // sanity: max 16 MB
        return Result<TransportMessage>::failure(ErrorCode::IO_READ_FAILED, "invalid message length");
    }
    std::string json(len, '\0');
    if (!recv_all(sock, &json[0], len, timeout_ms)) {
        return Result<TransportMessage>::failure(ErrorCode::IO_READ_FAILED, "recv body failed or timed out");
    }
    return Result<TransportMessage>::success(deserialize_message(json));
}

} // namespace proto
} // namespace vxl
