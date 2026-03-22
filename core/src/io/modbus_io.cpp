#include "modbus_io.h"

#include <algorithm>
#include <cstring>
#include <vector>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    using socket_t = SOCKET;
    static constexpr socket_t kInvalidSocket = INVALID_SOCKET;
    #define CLOSE_SOCKET(s) closesocket(s)
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    using socket_t = int;
    static constexpr socket_t kInvalidSocket = -1;
    #define CLOSE_SOCKET(s) ::close(s)
#endif

namespace vxl {

// ---------------------------------------------------------------------------
// Modbus TCP MBAP header layout (7 bytes):
//   [0-1] Transaction ID   (uint16 big-endian)
//   [2-3] Protocol ID      (0x0000 for Modbus)
//   [4-5] Length            (uint16 big-endian, bytes following)
//   [6]   Unit ID           (0x01 default)
// Then PDU: function code + data
// ---------------------------------------------------------------------------

static constexpr int kConnectTimeoutMs = 3000;
static constexpr int kRecvTimeoutMs    = 3000;
static constexpr uint8_t kUnitId       = 0x01;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
ModbusIO::ModbusIO(const std::string& host, int port)
    : host_(host), port_(port) {}

ModbusIO::~ModbusIO() {
    close();
}

// ---------------------------------------------------------------------------
// IIO interface
// ---------------------------------------------------------------------------
std::string ModbusIO::device_id() const {
    return "modbus-tcp://" + host_ + ":" + std::to_string(port_);
}

Result<void> ModbusIO::open() {
    if (socket_fd_ != kInvalidSocket) {
        return Result<void>::success();  // already open
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return Result<void>::failure(ErrorCode::IO_CONNECTION_FAILED,
                                    "WSAStartup failed");
    }
#endif

    socket_t sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == kInvalidSocket) {
        return Result<void>::failure(ErrorCode::IO_CONNECTION_FAILED,
                                    "Failed to create socket");
    }

    // Set non-blocking for connect timeout
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
        CLOSE_SOCKET(sock);
        return Result<void>::failure(ErrorCode::IO_CONNECTION_FAILED,
                                    "Invalid IP address: " + host_);
    }

    int rc = ::connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));

    if (rc != 0) {
#ifdef _WIN32
        int err = WSAGetLastError();
        bool in_progress = (err == WSAEWOULDBLOCK);
#else
        bool in_progress = (errno == EINPROGRESS);
#endif
        if (!in_progress) {
            CLOSE_SOCKET(sock);
            return Result<void>::failure(ErrorCode::IO_CONNECTION_FAILED,
                                        "Connect failed to " + host_ + ":" + std::to_string(port_));
        }

        // Wait for connection with timeout
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(sock, &wfds);

        struct timeval tv{};
        tv.tv_sec  = kConnectTimeoutMs / 1000;
        tv.tv_usec = (kConnectTimeoutMs % 1000) * 1000;

        rc = ::select(static_cast<int>(sock) + 1, nullptr, &wfds, nullptr, &tv);
        if (rc <= 0) {
            CLOSE_SOCKET(sock);
            return Result<void>::failure(ErrorCode::DEVICE_TIMEOUT,
                                        "Connection timed out to " + host_ + ":" + std::to_string(port_));
        }

        // Check for connect error
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &len);
        if (so_error != 0) {
            CLOSE_SOCKET(sock);
            return Result<void>::failure(ErrorCode::IO_CONNECTION_FAILED,
                                        "Connect failed: " + std::string(strerror(so_error)));
        }
    }

    // Set back to blocking mode
#ifdef _WIN32
    mode = 0;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    fcntl(sock, F_SETFL, flags);
#endif

    socket_fd_ = static_cast<int>(sock);
    return Result<void>::success();
}

void ModbusIO::close() {
    if (socket_fd_ != kInvalidSocket) {
        CLOSE_SOCKET(socket_fd_);
        socket_fd_ = kInvalidSocket;
    }
}

bool ModbusIO::is_open() const {
    return socket_fd_ != kInvalidSocket;
}

// ---------------------------------------------------------------------------
// Pin name parsing: "Y0" -> 0, "X12" -> 12, "0" -> 0
// ---------------------------------------------------------------------------
Result<int> ModbusIO::parse_pin(const std::string& pin) {
    if (pin.empty()) {
        return Result<int>::failure(ErrorCode::INVALID_PARAMETER, "Empty pin name");
    }

    size_t start = 0;
    // Skip optional prefix letter (X, Y, Q, I, etc.)
    if (!std::isdigit(static_cast<unsigned char>(pin[0]))) {
        start = 1;
    }
    if (start >= pin.size()) {
        return Result<int>::failure(ErrorCode::INVALID_PARAMETER,
                                   "Invalid pin name: " + pin);
    }

    try {
        int addr = std::stoi(pin.substr(start));
        return Result<int>::success(addr);
    } catch (...) {
        return Result<int>::failure(ErrorCode::INVALID_PARAMETER,
                                   "Cannot parse pin address: " + pin);
    }
}

// ---------------------------------------------------------------------------
// Low-level Modbus TCP frame exchange
// ---------------------------------------------------------------------------
Result<std::vector<uint8_t>> ModbusIO::send_request(uint8_t function_code,
                                                     const std::vector<uint8_t>& payload) {
    if (socket_fd_ == kInvalidSocket) {
        return Result<std::vector<uint8_t>>::failure(
            ErrorCode::DEVICE_OPEN_FAILED, "IO device is not open");
    }

    uint16_t tid = transaction_id_++;
    uint16_t pdu_len = static_cast<uint16_t>(1 + payload.size());  // FC + payload
    uint16_t mbap_length = pdu_len + 1;  // pdu_len + unit ID

    // Build MBAP header + PDU
    std::vector<uint8_t> frame;
    frame.reserve(7 + pdu_len);

    // Transaction ID (big-endian)
    frame.push_back(static_cast<uint8_t>(tid >> 8));
    frame.push_back(static_cast<uint8_t>(tid & 0xFF));
    // Protocol ID (0x0000)
    frame.push_back(0x00);
    frame.push_back(0x00);
    // Length (big-endian)
    frame.push_back(static_cast<uint8_t>(mbap_length >> 8));
    frame.push_back(static_cast<uint8_t>(mbap_length & 0xFF));
    // Unit ID
    frame.push_back(kUnitId);
    // Function code
    frame.push_back(function_code);
    // Payload
    frame.insert(frame.end(), payload.begin(), payload.end());

    // Send
    auto total = static_cast<int>(frame.size());
    int sent = 0;
    while (sent < total) {
        int n = static_cast<int>(::send(socket_fd_, reinterpret_cast<const char*>(frame.data() + sent),
                                        total - sent, 0));
        if (n <= 0) {
            return Result<std::vector<uint8_t>>::failure(
                ErrorCode::IO_WRITE_FAILED, "Failed to send Modbus request");
        }
        sent += n;
    }

    // Receive response with timeout
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(socket_fd_, &rfds);
    struct timeval tv{};
    tv.tv_sec  = kRecvTimeoutMs / 1000;
    tv.tv_usec = (kRecvTimeoutMs % 1000) * 1000;

    int rc = ::select(socket_fd_ + 1, &rfds, nullptr, nullptr, &tv);
    if (rc <= 0) {
        return Result<std::vector<uint8_t>>::failure(
            ErrorCode::DEVICE_TIMEOUT, "Modbus response timed out");
    }

    // Read MBAP header first (7 bytes)
    uint8_t hdr[7];
    int received = 0;
    while (received < 7) {
        int n = static_cast<int>(::recv(socket_fd_, reinterpret_cast<char*>(hdr + received),
                                        7 - received, 0));
        if (n <= 0) {
            return Result<std::vector<uint8_t>>::failure(
                ErrorCode::IO_READ_FAILED, "Failed to receive Modbus response header");
        }
        received += n;
    }

    // Parse response length
    uint16_t resp_length = static_cast<uint16_t>((hdr[4] << 8) | hdr[5]);
    if (resp_length < 2 || resp_length > 256) {
        return Result<std::vector<uint8_t>>::failure(
            ErrorCode::IO_READ_FAILED, "Invalid Modbus response length");
    }

    // Read remaining bytes (unit ID already counted, need resp_length - 1 more for PDU)
    int remaining = resp_length - 1;  // minus unit ID already in header
    std::vector<uint8_t> pdu(remaining);
    received = 0;
    while (received < remaining) {
        int n = static_cast<int>(::recv(socket_fd_, reinterpret_cast<char*>(pdu.data() + received),
                                        remaining - received, 0));
        if (n <= 0) {
            return Result<std::vector<uint8_t>>::failure(
                ErrorCode::IO_READ_FAILED, "Failed to receive Modbus response data");
        }
        received += n;
    }

    // Check for exception response
    if (!pdu.empty() && (pdu[0] & 0x80)) {
        uint8_t exception_code = pdu.size() > 1 ? pdu[1] : 0;
        return Result<std::vector<uint8_t>>::failure(
            ErrorCode::IO_READ_FAILED,
            "Modbus exception: FC=0x" + std::to_string(pdu[0]) +
            " code=" + std::to_string(exception_code));
    }

    return Result<std::vector<uint8_t>>::success(std::move(pdu));
}

// ---------------------------------------------------------------------------
// Modbus function implementations
// ---------------------------------------------------------------------------

// FC 01: Read Coils
Result<bool> ModbusIO::read_coil(int address) {
    std::vector<uint8_t> payload = {
        static_cast<uint8_t>(address >> 8),
        static_cast<uint8_t>(address & 0xFF),
        0x00, 0x01  // quantity = 1
    };

    auto r = send_request(0x01, payload);
    if (!r.ok()) return Result<bool>::failure(r.code, r.message);

    // Response: FC(1) + byte_count(1) + data(1)
    if (r.value.size() < 3) {
        return Result<bool>::failure(ErrorCode::IO_READ_FAILED,
                                    "Truncated read coil response");
    }
    bool val = (r.value[2] & 0x01) != 0;
    return Result<bool>::success(val);
}

// FC 05: Write Single Coil
Result<void> ModbusIO::write_coil(int address, bool value) {
    uint16_t coil_value = value ? 0xFF00 : 0x0000;
    std::vector<uint8_t> payload = {
        static_cast<uint8_t>(address >> 8),
        static_cast<uint8_t>(address & 0xFF),
        static_cast<uint8_t>(coil_value >> 8),
        static_cast<uint8_t>(coil_value & 0xFF),
    };

    auto r = send_request(0x05, payload);
    if (!r.ok()) return Result<void>::failure(r.code, r.message);
    return Result<void>::success();
}

// FC 03: Read Holding Registers
Result<uint16_t> ModbusIO::read_holding_register(int address) {
    std::vector<uint8_t> payload = {
        static_cast<uint8_t>(address >> 8),
        static_cast<uint8_t>(address & 0xFF),
        0x00, 0x01  // quantity = 1
    };

    auto r = send_request(0x03, payload);
    if (!r.ok()) return Result<uint16_t>::failure(r.code, r.message);

    // Response: FC(1) + byte_count(1) + data(2)
    if (r.value.size() < 4) {
        return Result<uint16_t>::failure(ErrorCode::IO_READ_FAILED,
                                        "Truncated read register response");
    }
    uint16_t val = static_cast<uint16_t>((r.value[2] << 8) | r.value[3]);
    return Result<uint16_t>::success(val);
}

// FC 06: Write Single Register
Result<void> ModbusIO::write_holding_register(int address, uint16_t value) {
    std::vector<uint8_t> payload = {
        static_cast<uint8_t>(address >> 8),
        static_cast<uint8_t>(address & 0xFF),
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value & 0xFF),
    };

    auto r = send_request(0x06, payload);
    if (!r.ok()) return Result<void>::failure(r.code, r.message);
    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// IIO high-level methods
// ---------------------------------------------------------------------------
Result<void> ModbusIO::set_output(const std::string& pin, bool value) {
    auto addr = parse_pin(pin);
    if (!addr.ok()) return Result<void>::failure(addr.code, addr.message);
    return write_coil(addr.value, value);
}

Result<bool> ModbusIO::get_input(const std::string& pin) {
    auto addr = parse_pin(pin);
    if (!addr.ok()) return Result<bool>::failure(addr.code, addr.message);
    return read_coil(addr.value);
}

Result<void> ModbusIO::write_register(int address, uint16_t value) {
    return write_holding_register(address, value);
}

Result<uint16_t> ModbusIO::read_register(int address) {
    return read_holding_register(address);
}

} // namespace vxl
