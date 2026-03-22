#pragma once

#include "vxl/export.h"
#include "vxl/error.h"
#include "vxl/types.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace vxl {

// ---------------------------------------------------------------------------
// VLM provider configuration
// ---------------------------------------------------------------------------
struct VXL_EXPORT VLMConfig {
    std::string provider = "openai";     // "openai", "anthropic", "ollama", "custom"
    std::string api_key;                  // API key (not needed for ollama)
    std::string model = "gpt-4o";        // Model name
    std::string base_url;                 // Override API endpoint (for ollama: "http://localhost:11434")
    int timeout_seconds = 30;
    int max_tokens = 1024;
};

// ---------------------------------------------------------------------------
// VLM response
// ---------------------------------------------------------------------------
struct VXL_EXPORT VLMResponse {
    std::string text;
    float confidence = 0.0f;   // 0-1, estimated by model if available
    int tokens_used = 0;
    double latency_ms = 0.0;
};

// ---------------------------------------------------------------------------
// HTTP caller function type
// url, headers_json, body -> response_body
// ---------------------------------------------------------------------------
using HttpCaller = std::function<std::string(const std::string& url,
                                              const std::string& headers_json,
                                              const std::string& body)>;

// ---------------------------------------------------------------------------
// VLM Assistant - Copilot role (offline assist, NOT real-time inspection)
// ---------------------------------------------------------------------------
class VXL_EXPORT VLMAssistant {
public:
    VLMAssistant();
    ~VLMAssistant();

    VLMAssistant(VLMAssistant&&) noexcept;
    VLMAssistant& operator=(VLMAssistant&&) noexcept;

    // Non-copyable
    VLMAssistant(const VLMAssistant&) = delete;
    VLMAssistant& operator=(const VLMAssistant&) = delete;

    Result<void> configure(const VLMConfig& config);
    bool is_configured() const;
    const VLMConfig& config() const;

    // Set custom HTTP transport (e.g., from Python requests)
    void set_http_caller(HttpCaller caller);

    // Describe a defect from image + defect region
    Result<VLMResponse> describe_defect(const Image& image, const DefectRegion& defect);

    // Suggest inspection parameters from sample image
    Result<VLMResponse> suggest_parameters(const Image& sample_image,
                                            const std::string& context = "");

    // Generate quality report from inspection results
    Result<VLMResponse> generate_report(const std::vector<InspectionResult>& results,
                                         const std::string& template_text = "");

    // Free-form query with optional image
    Result<VLMResponse> query(const std::string& prompt, const Image* image = nullptr);

    // Batch label suggestions for unlabeled samples
    Result<std::vector<std::pair<std::string, float>>> suggest_labels(
        const Image& image, const std::vector<std::string>& candidate_labels);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Global VLM assistant singleton
VXL_EXPORT VLMAssistant& vlm_assistant();

} // namespace vxl
