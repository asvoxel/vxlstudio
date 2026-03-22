#include "vxl/log.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/base_sink.h>

#include <opencv2/imgcodecs.hpp>

namespace vxl {
namespace log {

// ===========================================================================
// Callback sink for spdlog
// ===========================================================================

template <typename Mutex>
class CallbackSink : public spdlog::sinks::base_sink<Mutex> {
public:
    using CallbackFn = std::function<void(Level, const std::string&)>;

    explicit CallbackSink(CallbackFn fn) : callback_(std::move(fn)) {}

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);
        std::string text(formatted.data(), formatted.size());

        Level lvl = Level::INFO;
        switch (msg.level) {
            case spdlog::level::trace:    lvl = Level::TRACE; break;
            case spdlog::level::debug:    lvl = Level::DEBUG; break;
            case spdlog::level::info:     lvl = Level::INFO;  break;
            case spdlog::level::warn:     lvl = Level::WARN;  break;
            case spdlog::level::err:      lvl = Level::ERROR; break;
            case spdlog::level::critical: lvl = Level::FATAL; break;
            default: break;
        }
        callback_(lvl, text);
    }

    void flush_() override {}

private:
    CallbackFn callback_;
};

using CallbackSinkMt = CallbackSink<std::mutex>;

// ===========================================================================
// Internal state
// ===========================================================================

static std::shared_ptr<spdlog::logger> g_logger;
static std::vector<spdlog::sink_ptr>   g_sinks;
static std::mutex                      g_mutex;
static std::string                     g_log_dir  = "logs";
static int                             g_max_days = 30;
static int                             g_max_size_mb = 100;
static bool                            g_initialized = false;

static spdlog::level::level_enum to_spdlog_level(Level lvl) {
    switch (lvl) {
        case Level::TRACE: return spdlog::level::trace;
        case Level::DEBUG: return spdlog::level::debug;
        case Level::INFO:  return spdlog::level::info;
        case Level::WARN:  return spdlog::level::warn;
        case Level::ERROR: return spdlog::level::err;
        case Level::FATAL: return spdlog::level::critical;
    }
    return spdlog::level::info;
}

static void rebuild_logger() {
    g_logger = std::make_shared<spdlog::logger>("vxl",
        g_sinks.begin(), g_sinks.end());
    g_logger->set_level(spdlog::level::trace);
    g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_default_logger(g_logger);
}

static std::string timestamp_string() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&t));
    char result[80];
    std::snprintf(result, sizeof(result), "%s_%03d",
                  buf, static_cast<int>(ms.count()));
    return result;
}

// ===========================================================================
// Public API
// ===========================================================================

void init() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initialized) return;

    if (g_sinks.empty()) {
        // Default: add a console sink
        g_sinks.push_back(
            std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }
    rebuild_logger();
    g_initialized = true;

    // Create log subdirectories
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(g_log_dir) / "images");
    fs::create_directories(fs::path(g_log_dir) / "heightmaps");
    fs::create_directories(fs::path(g_log_dir) / "results");
}

void set_level(Level level) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_logger) {
        g_logger->set_level(to_spdlog_level(level));
    }
}

void trace(const std::string& msg) { if (g_logger) g_logger->trace(msg); }
void debug(const std::string& msg) { if (g_logger) g_logger->debug(msg); }
void info(const std::string& msg)  { if (g_logger) g_logger->info(msg); }
void warn(const std::string& msg)  { if (g_logger) g_logger->warn(msg); }
void error(const std::string& msg) { if (g_logger) g_logger->error(msg); }
void fatal(const std::string& msg) { if (g_logger) g_logger->critical(msg); }

void add_console_sink() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sinks.push_back(
        std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    if (g_initialized) rebuild_logger();
}

void add_file_sink(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    // rotating file: max_size_mb MB, max 5 rotated files
    size_t max_size = static_cast<size_t>(g_max_size_mb) * 1024 * 1024;
    g_sinks.push_back(
        std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            path, max_size, 5));
    if (g_initialized) rebuild_logger();
}

void add_callback_sink(std::function<void(Level, const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sinks.push_back(std::make_shared<CallbackSinkMt>(std::move(callback)));
    if (g_initialized) rebuild_logger();
}

void save_image(const Image& img, const std::string& tag) {
    namespace fs = std::filesystem;
    fs::path dir = fs::path(g_log_dir) / "images";
    fs::create_directories(dir);

    std::string filename = timestamp_string() + "_" + tag + ".png";
    fs::path filepath = dir / filename;

    cv::Mat mat = img.to_cv_mat();
    cv::imwrite(filepath.string(), mat);

    if (g_logger) {
        g_logger->info("Saved image: {}", filepath.string());
    }
}

void save_height_map(const HeightMap& hm, const std::string& tag) {
    namespace fs = std::filesystem;
    fs::path dir = fs::path(g_log_dir) / "heightmaps";
    fs::create_directories(dir);

    std::string ts = timestamp_string();
    std::string base = ts + "_" + tag;

    // Write raw float binary
    {
        fs::path bin_path = dir / (base + ".raw");
        std::ofstream ofs(bin_path, std::ios::binary);
        if (ofs.is_open()) {
            ofs.write(reinterpret_cast<const char*>(hm.buffer.data()),
                      static_cast<std::streamsize>(hm.buffer.size()));
        }
    }

    // Write metadata json
    {
        fs::path json_path = dir / (base + ".json");
        std::ofstream ofs(json_path);
        if (ofs.is_open()) {
            ofs << "{\n"
                << "  \"width\": "         << hm.width << ",\n"
                << "  \"height\": "        << hm.height << ",\n"
                << "  \"resolution_mm\": " << hm.resolution_mm << ",\n"
                << "  \"origin_x\": "      << hm.origin_x << ",\n"
                << "  \"origin_y\": "      << hm.origin_y << ",\n"
                << "  \"data_file\": \""   << (base + ".raw") << "\"\n"
                << "}\n";
        }
    }

    if (g_logger) {
        g_logger->info("Saved height map: {}", base);
    }
}

void save_result(const InspectionResult& result) {
    namespace fs = std::filesystem;
    fs::path dir = fs::path(g_log_dir) / "results";
    fs::create_directories(dir);

    std::string filename = timestamp_string() + "_result.json";
    fs::path filepath = dir / filename;

    std::ofstream ofs(filepath);
    if (ofs.is_open()) {
        ofs << result.to_json();
    }

    if (g_logger) {
        g_logger->info("Saved inspection result: {}", filepath.string());
    }
}

void set_log_dir(const std::string& dir) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_log_dir = dir;
}

void set_max_days(int days) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_max_days = days;
    (void)g_max_days; // Used for future log cleanup
}

void set_max_size_mb(int size_mb) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_max_size_mb = size_mb;
}

} // namespace log
} // namespace vxl
