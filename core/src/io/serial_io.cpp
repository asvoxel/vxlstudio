#include "serial_io.h"

#include <cstring>

#ifdef _WIN32
    // Windows serial stub -- not yet implemented
#else
    #include <fcntl.h>
    #include <termios.h>
    #include <unistd.h>
    #include <sys/select.h>
#endif

namespace vxl {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
SerialIO::SerialIO(const std::string& port_path, int baud_rate)
    : port_path_(port_path), baud_rate_(baud_rate) {}

SerialIO::~SerialIO() {
    close();
}

// ---------------------------------------------------------------------------
// IIO interface
// ---------------------------------------------------------------------------
std::string SerialIO::device_id() const {
    return "serial://" + port_path_ + ":" + std::to_string(baud_rate_);
}

Result<void> SerialIO::open() {
    if (fd_ >= 0) {
        return Result<void>::success();  // already open
    }

#ifdef _WIN32
    return Result<void>::failure(ErrorCode::IO_NOT_SUPPORTED,
                                "Serial IO not yet supported on Windows");
#else
    fd_ = ::open(port_path_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        return Result<void>::failure(ErrorCode::IO_CONNECTION_FAILED,
                                    "Failed to open serial port: " + port_path_ +
                                    " (" + std::strerror(errno) + ")");
    }

    // Configure terminal
    struct termios tty{};
    if (tcgetattr(fd_, &tty) != 0) {
        ::close(fd_);
        fd_ = -1;
        return Result<void>::failure(ErrorCode::IO_CONNECTION_FAILED,
                                    "tcgetattr failed");
    }

    // Map baud rate
    speed_t speed = B9600;
    switch (baud_rate_) {
        case 1200:   speed = B1200;   break;
        case 2400:   speed = B2400;   break;
        case 4800:   speed = B4800;   break;
        case 9600:   speed = B9600;   break;
        case 19200:  speed = B19200;  break;
        case 38400:  speed = B38400;  break;
        case 57600:  speed = B57600;  break;
        case 115200: speed = B115200; break;
        default:     speed = B9600;   break;
    }

    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    // 8N1, no flow control
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    // Raw mode
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag &= ~OPOST;

    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 10;  // 1 second timeout in tenths

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        ::close(fd_);
        fd_ = -1;
        return Result<void>::failure(ErrorCode::IO_CONNECTION_FAILED,
                                    "tcsetattr failed");
    }

    return Result<void>::success();
#endif
}

void SerialIO::close() {
#ifndef _WIN32
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
#endif
}

bool SerialIO::is_open() const {
    return fd_ >= 0;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
Result<void> SerialIO::write_line(const std::string& line) {
#ifdef _WIN32
    return Result<void>::failure(ErrorCode::IO_NOT_SUPPORTED,
                                "Serial IO not yet supported on Windows");
#else
    if (fd_ < 0) {
        return Result<void>::failure(ErrorCode::DEVICE_OPEN_FAILED,
                                    "Serial port is not open");
    }

    std::string data = line + "\n";
    const char* ptr = data.c_str();
    size_t remaining = data.size();

    while (remaining > 0) {
        ssize_t n = ::write(fd_, ptr, remaining);
        if (n < 0) {
            return Result<void>::failure(ErrorCode::IO_WRITE_FAILED,
                                        "Failed to write to serial port");
        }
        ptr += n;
        remaining -= static_cast<size_t>(n);
    }
    return Result<void>::success();
#endif
}

Result<std::string> SerialIO::read_line(int timeout_ms) {
#ifdef _WIN32
    return Result<std::string>::failure(ErrorCode::IO_NOT_SUPPORTED,
                                       "Serial IO not yet supported on Windows");
#else
    if (fd_ < 0) {
        return Result<std::string>::failure(ErrorCode::DEVICE_OPEN_FAILED,
                                           "Serial port is not open");
    }

    std::string result;
    result.reserve(64);

    fd_set rfds;
    struct timeval tv{};
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    while (true) {
        FD_ZERO(&rfds);
        FD_SET(fd_, &rfds);

        int rc = ::select(fd_ + 1, &rfds, nullptr, nullptr, &tv);
        if (rc < 0) {
            return Result<std::string>::failure(ErrorCode::IO_READ_FAILED,
                                               "select() failed on serial port");
        }
        if (rc == 0) {
            return Result<std::string>::failure(ErrorCode::DEVICE_TIMEOUT,
                                               "Serial read timed out");
        }

        char c;
        ssize_t n = ::read(fd_, &c, 1);
        if (n < 0) {
            return Result<std::string>::failure(ErrorCode::IO_READ_FAILED,
                                               "Failed to read from serial port");
        }
        if (n == 0) {
            continue;  // no data yet
        }

        if (c == '\n' || c == '\r') {
            if (!result.empty()) {
                break;  // end of line
            }
            continue;  // skip leading CR/LF
        }
        result.push_back(c);
    }

    return Result<std::string>::success(std::move(result));
#endif
}

// ---------------------------------------------------------------------------
// IIO high-level methods
// ---------------------------------------------------------------------------
Result<void> SerialIO::set_output(const std::string& pin, bool value) {
    std::string cmd = "SET " + pin + " " + (value ? "1" : "0");
    return write_line(cmd);
}

Result<bool> SerialIO::get_input(const std::string& pin) {
    auto wr = write_line("GET " + pin);
    if (!wr.ok()) return Result<bool>::failure(wr.code, wr.message);

    auto resp = read_line();
    if (!resp.ok()) return Result<bool>::failure(resp.code, resp.message);

    bool val = (resp.value == "1");
    return Result<bool>::success(val);
}

Result<void> SerialIO::write_register(int /*address*/, uint16_t /*value*/) {
    return Result<void>::failure(ErrorCode::IO_NOT_SUPPORTED,
                                "Register read/write not supported on serial IO");
}

Result<uint16_t> SerialIO::read_register(int /*address*/) {
    return Result<uint16_t>::failure(ErrorCode::IO_NOT_SUPPORTED,
                                    "Register read/write not supported on serial IO");
}

} // namespace vxl
