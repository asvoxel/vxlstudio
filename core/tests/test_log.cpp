#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "vxl/log.h"
#include "vxl/types.h"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helper: RAII temp directory
// ---------------------------------------------------------------------------
class TempDir {
public:
    TempDir() {
        path_ = fs::temp_directory_path() / ("vxl_test_log_" +
            std::to_string(std::chrono::steady_clock::now()
                .time_since_epoch().count()));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
};

// ---------------------------------------------------------------------------
// Helper: Callback sink collector
// ---------------------------------------------------------------------------
struct LogCapture {
    std::mutex mtx;
    std::vector<std::pair<vxl::log::Level, std::string>> entries;

    void callback(vxl::log::Level lvl, const std::string& msg) {
        std::lock_guard<std::mutex> lock(mtx);
        entries.emplace_back(lvl, msg);
    }

    size_t size() {
        std::lock_guard<std::mutex> lock(mtx);
        return entries.size();
    }

    bool has_level(vxl::log::Level lvl) {
        std::lock_guard<std::mutex> lock(mtx);
        for (const auto& e : entries) {
            if (e.first == lvl) return true;
        }
        return false;
    }

    bool has_text(const std::string& text) {
        std::lock_guard<std::mutex> lock(mtx);
        for (const auto& e : entries) {
            if (e.second.find(text) != std::string::npos) return true;
        }
        return false;
    }

    void clear_entries() {
        std::lock_guard<std::mutex> lock(mtx);
        entries.clear();
    }
};

// ---------------------------------------------------------------------------
// Test: init() doesn't crash when called multiple times
// ---------------------------------------------------------------------------
TEST(LogModule, InitMultipleTimes) {
    // Calling init() several times should be safe (idempotent).
    EXPECT_NO_THROW(vxl::log::init());
    EXPECT_NO_THROW(vxl::log::init());
    EXPECT_NO_THROW(vxl::log::init());
}

// ---------------------------------------------------------------------------
// Test: add_callback_sink receives messages with correct level
// ---------------------------------------------------------------------------
TEST(LogModule, AddCallbackSinkReceivesMessages) {
    LogCapture capture;

    vxl::log::add_callback_sink(
        [&](vxl::log::Level lvl, const std::string& msg) {
            capture.callback(lvl, msg);
        });

    // Ensure logger is initialised (may already be from previous test).
    vxl::log::init();

    // Set level to TRACE so all messages pass through.
    vxl::log::set_level(vxl::log::Level::TRACE);

    capture.clear_entries();

    vxl::log::info("callback_test_info");
    vxl::log::warn("callback_test_warn");
    vxl::log::error("callback_test_error");

    // The callback should have received at least these 3 messages.
    EXPECT_GE(capture.size(), 3u);

    EXPECT_TRUE(capture.has_level(vxl::log::Level::INFO));
    EXPECT_TRUE(capture.has_level(vxl::log::Level::WARN));
    EXPECT_TRUE(capture.has_level(vxl::log::Level::ERROR));

    EXPECT_TRUE(capture.has_text("callback_test_info"));
    EXPECT_TRUE(capture.has_text("callback_test_warn"));
    EXPECT_TRUE(capture.has_text("callback_test_error"));
}

// ---------------------------------------------------------------------------
// Test: info / warn / error produce output via callback sink
// ---------------------------------------------------------------------------
TEST(LogModule, InfoWarnErrorProduceOutput) {
    LogCapture capture;

    vxl::log::add_callback_sink(
        [&](vxl::log::Level lvl, const std::string& msg) {
            capture.callback(lvl, msg);
        });
    vxl::log::init();
    vxl::log::set_level(vxl::log::Level::TRACE);

    capture.clear_entries();

    vxl::log::info("msg_info_test");
    EXPECT_TRUE(capture.has_text("msg_info_test"));

    vxl::log::warn("msg_warn_test");
    EXPECT_TRUE(capture.has_text("msg_warn_test"));

    vxl::log::error("msg_error_test");
    EXPECT_TRUE(capture.has_text("msg_error_test"));
}

// ---------------------------------------------------------------------------
// Test: set_level suppresses messages below the configured level
// ---------------------------------------------------------------------------
TEST(LogModule, SetLevelSuppressesBelowLevel) {
    LogCapture capture;

    vxl::log::add_callback_sink(
        [&](vxl::log::Level lvl, const std::string& msg) {
            capture.callback(lvl, msg);
        });
    vxl::log::init();

    // Set level to WARN -- INFO and below should be suppressed.
    vxl::log::set_level(vxl::log::Level::WARN);

    capture.clear_entries();

    vxl::log::trace("suppressed_trace");
    vxl::log::debug("suppressed_debug");
    vxl::log::info("suppressed_info");

    // These should NOT appear.
    EXPECT_FALSE(capture.has_text("suppressed_trace"));
    EXPECT_FALSE(capture.has_text("suppressed_debug"));
    EXPECT_FALSE(capture.has_text("suppressed_info"));

    // WARN and above should appear.
    vxl::log::warn("visible_warn");
    vxl::log::error("visible_error");

    EXPECT_TRUE(capture.has_text("visible_warn"));
    EXPECT_TRUE(capture.has_text("visible_error"));

    // Restore default level for other tests.
    vxl::log::set_level(vxl::log::Level::TRACE);
}

// ---------------------------------------------------------------------------
// Test: set_log_dir with a temp directory
// ---------------------------------------------------------------------------
TEST(LogModule, SetLogDir) {
    TempDir tmp;

    // Should not throw.
    EXPECT_NO_THROW(vxl::log::set_log_dir(tmp.path().string()));

    // Re-initialise so subdirectories are created under the new log_dir.
    // NOTE: init() is idempotent (only runs once because of g_initialized).
    // But set_log_dir itself should work regardless.
}

// ---------------------------------------------------------------------------
// Test: save_result writes a JSON file to log_dir
// ---------------------------------------------------------------------------
TEST(LogModule, SaveResultWritesJson) {
    TempDir tmp;

    // Set log dir to our temp directory.
    vxl::log::set_log_dir(tmp.path().string());

    // Manually create the results subdirectory (save_result does this too,
    // but we want to be sure the dir exists for our test).
    fs::create_directories(tmp.path() / "results");

    // Build a test InspectionResult.
    vxl::InspectionResult result;
    result.ok = false;
    result.recipe_name = "test_recipe";
    result.timestamp = "2026-03-21T00:00:00Z";

    vxl::DefectRegion defect;
    defect.type = "bump";
    defect.area_mm2 = 1.5f;
    defect.max_height = 0.3f;
    defect.avg_height = 0.2f;
    defect.bounding_box = {10, 20, 30, 40};
    result.defects.push_back(defect);

    // Ensure logger is ready.
    vxl::log::init();

    // save_result should write a JSON file into <log_dir>/results/.
    vxl::log::save_result(result);

    // Check that at least one JSON file was created.
    bool found_json = false;
    for (const auto& entry : fs::directory_iterator(tmp.path() / "results")) {
        if (entry.path().extension() == ".json") {
            found_json = true;

            // Read and do a basic sanity check.
            std::ifstream ifs(entry.path());
            std::string content((std::istreambuf_iterator<char>(ifs)),
                                std::istreambuf_iterator<char>());
            EXPECT_FALSE(content.empty());
            // The JSON should mention our recipe name.
            EXPECT_NE(content.find("test_recipe"), std::string::npos);
            break;
        }
    }
    EXPECT_TRUE(found_json) << "Expected a JSON result file in "
                            << (tmp.path() / "results").string();
}
