// VxlStudio -- VLMAssistant implementation
// SPDX-License-Identifier: MIT

#include "vxl/vlm.h"
#include "base64.h"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>
#include <opencv2/imgcodecs.hpp>

namespace vxl {

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct VLMAssistant::Impl {
    VLMConfig config;
    bool configured = false;
    HttpCaller http_caller;
    mutable std::mutex mutex;

    // Encode an Image to base64-encoded PNG
    std::string encode_image_base64(const Image& image);

    // Resolve the API URL based on provider + config
    std::string api_url() const;

    // Build request headers as JSON string
    std::string build_headers_json() const;

    // Build a chat-completion request body
    std::string build_request_body(const std::string& system_prompt,
                                    const std::string& user_prompt,
                                    const Image* image) const;

    // Execute the HTTP call (using http_caller or curl fallback)
    Result<std::string> do_http_post(const std::string& url,
                                      const std::string& headers_json,
                                      const std::string& body);

    // Parse VLM API response JSON into VLMResponse
    Result<VLMResponse> parse_response(const std::string& json_str, double latency_ms);

    // Execute a full VLM query
    Result<VLMResponse> execute(const std::string& system_prompt,
                                 const std::string& user_prompt,
                                 const Image* image);
};

// ---------------------------------------------------------------------------
// Image encoding
// ---------------------------------------------------------------------------
std::string VLMAssistant::Impl::encode_image_base64(const Image& image) {
    cv::Mat mat = image.to_cv_mat();
    std::vector<uint8_t> png_buf;
    cv::imencode(".png", mat, png_buf);
    return detail::base64_encode(png_buf.data(), png_buf.size());
}

// ---------------------------------------------------------------------------
// API URL resolution
// ---------------------------------------------------------------------------
std::string VLMAssistant::Impl::api_url() const {
    if (!config.base_url.empty()) {
        // For custom / ollama endpoints
        if (config.provider == "ollama") {
            return config.base_url + "/api/chat";
        }
        // Custom endpoint: assume OpenAI-compatible
        return config.base_url + "/v1/chat/completions";
    }

    if (config.provider == "openai") {
        return "https://api.openai.com/v1/chat/completions";
    } else if (config.provider == "anthropic") {
        return "https://api.anthropic.com/v1/messages";
    } else if (config.provider == "ollama") {
        return "http://localhost:11434/api/chat";
    }

    // Default to OpenAI-compatible
    return "https://api.openai.com/v1/chat/completions";
}

// ---------------------------------------------------------------------------
// Headers
// ---------------------------------------------------------------------------
std::string VLMAssistant::Impl::build_headers_json() const {
    nlohmann::json headers;
    headers["Content-Type"] = "application/json";

    if (config.provider == "anthropic") {
        headers["x-api-key"] = config.api_key;
        headers["anthropic-version"] = "2023-06-01";
    } else if (config.provider != "ollama") {
        // OpenAI / custom
        headers["Authorization"] = "Bearer " + config.api_key;
    }

    return headers.dump();
}

// ---------------------------------------------------------------------------
// Request body
// ---------------------------------------------------------------------------
std::string VLMAssistant::Impl::build_request_body(const std::string& system_prompt,
                                                     const std::string& user_prompt,
                                                     const Image* image) const {
    nlohmann::json body;

    if (config.provider == "anthropic") {
        // Anthropic Messages API format
        body["model"] = config.model;
        body["max_tokens"] = config.max_tokens;
        body["system"] = system_prompt;

        nlohmann::json content = nlohmann::json::array();
        if (image && image->width > 0 && image->height > 0) {
            // Encode for Anthropic's image block
            // Note: encode_image_base64 is non-const because of cv::Mat, but we
            // cast away here since it doesn't modify logical state.
            std::string b64 = const_cast<Impl*>(this)->encode_image_base64(*image);
            nlohmann::json img_block;
            img_block["type"] = "image";
            img_block["source"]["type"] = "base64";
            img_block["source"]["media_type"] = "image/png";
            img_block["source"]["data"] = std::move(b64);
            content.push_back(std::move(img_block));
        }
        nlohmann::json text_block;
        text_block["type"] = "text";
        text_block["text"] = user_prompt;
        content.push_back(std::move(text_block));

        body["messages"] = nlohmann::json::array({
            {{"role", "user"}, {"content", std::move(content)}}
        });

    } else {
        // OpenAI / Ollama format
        body["model"] = config.model;
        body["max_tokens"] = config.max_tokens;

        nlohmann::json messages = nlohmann::json::array();
        messages.push_back({{"role", "system"}, {"content", system_prompt}});

        if (image && image->width > 0 && image->height > 0) {
            std::string b64 = const_cast<Impl*>(this)->encode_image_base64(*image);
            nlohmann::json content = nlohmann::json::array();
            nlohmann::json img_part;
            img_part["type"] = "image_url";
            img_part["image_url"]["url"] = "data:image/png;base64," + std::move(b64);
            content.push_back(std::move(img_part));

            nlohmann::json text_part;
            text_part["type"] = "text";
            text_part["text"] = user_prompt;
            content.push_back(std::move(text_part));

            messages.push_back({{"role", "user"}, {"content", std::move(content)}});
        } else {
            messages.push_back({{"role", "user"}, {"content", user_prompt}});
        }

        body["messages"] = std::move(messages);
    }

    return body.dump();
}

// ---------------------------------------------------------------------------
// HTTP POST execution
// ---------------------------------------------------------------------------
Result<std::string> VLMAssistant::Impl::do_http_post(const std::string& url,
                                                       const std::string& headers_json,
                                                       const std::string& body) {
    // 1. Try user-supplied http_caller
    if (http_caller) {
        try {
            std::string response = http_caller(url, headers_json, body);
            return Result<std::string>::success(std::move(response));
        } catch (const std::exception& e) {
            return Result<std::string>::failure(
                ErrorCode::IO_CONNECTION_FAILED,
                std::string("HTTP caller failed: ") + e.what());
        }
    }

    // 2. Fallback: try curl via popen
    //    Write the body to a temp file to avoid shell escaping issues
    std::string tmp_body = "/tmp/vxl_vlm_request.json";
    std::string tmp_resp = "/tmp/vxl_vlm_response.json";

    {
        FILE* f = std::fopen(tmp_body.c_str(), "w");
        if (!f) {
            return Result<std::string>::failure(
                ErrorCode::IO_WRITE_FAILED,
                "Failed to write VLM request to temp file");
        }
        std::fwrite(body.data(), 1, body.size(), f);
        std::fclose(f);
    }

    // Parse headers JSON to build curl -H flags
    std::string curl_headers;
    try {
        auto hdrs = nlohmann::json::parse(headers_json);
        for (auto& [key, val] : hdrs.items()) {
            curl_headers += " -H '" + key + ": " + val.get<std::string>() + "'";
        }
    } catch (...) {
        // Ignore header parse errors
    }

    std::string cmd = "curl -s -S --max-time " + std::to_string(config.timeout_seconds) +
                      curl_headers +
                      " -d @" + tmp_body +
                      " -o " + tmp_resp +
                      " '" + url + "' 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return Result<std::string>::failure(
            ErrorCode::IO_CONNECTION_FAILED,
            "VLM not configured - set http_caller or install curl");
    }

    // Read stderr output from curl
    char err_buf[512];
    std::string curl_stderr;
    while (std::fgets(err_buf, sizeof(err_buf), pipe)) {
        curl_stderr += err_buf;
    }
    int exit_code = pclose(pipe);

    if (exit_code != 0) {
        return Result<std::string>::failure(
            ErrorCode::IO_CONNECTION_FAILED,
            "curl failed (exit " + std::to_string(exit_code) + "): " + curl_stderr);
    }

    // Read response file
    FILE* rf = std::fopen(tmp_resp.c_str(), "r");
    if (!rf) {
        return Result<std::string>::failure(
            ErrorCode::IO_READ_FAILED,
            "Failed to read VLM response from temp file");
    }

    std::fseek(rf, 0, SEEK_END);
    long fsize = std::ftell(rf);
    std::fseek(rf, 0, SEEK_SET);
    std::string response(static_cast<size_t>(fsize), '\0');
    std::fread(&response[0], 1, static_cast<size_t>(fsize), rf);
    std::fclose(rf);

    // Clean up temp files
    std::remove(tmp_body.c_str());
    std::remove(tmp_resp.c_str());

    return Result<std::string>::success(std::move(response));
}

// ---------------------------------------------------------------------------
// Response parsing
// ---------------------------------------------------------------------------
Result<VLMResponse> VLMAssistant::Impl::parse_response(const std::string& json_str,
                                                         double latency_ms) {
    try {
        auto j = nlohmann::json::parse(json_str);

        VLMResponse resp;
        resp.latency_ms = latency_ms;

        if (config.provider == "anthropic") {
            // Anthropic Messages API format
            if (j.contains("error")) {
                return Result<VLMResponse>::failure(
                    ErrorCode::INTERNAL_ERROR,
                    "VLM API error: " + j["error"].value("message", j["error"].dump()));
            }
            if (j.contains("content") && j["content"].is_array() && !j["content"].empty()) {
                resp.text = j["content"][0].value("text", "");
            }
            if (j.contains("usage")) {
                resp.tokens_used = j["usage"].value("input_tokens", 0) +
                                   j["usage"].value("output_tokens", 0);
            }
        } else {
            // OpenAI / Ollama format
            if (j.contains("error")) {
                std::string err_msg;
                if (j["error"].is_object()) {
                    err_msg = j["error"].value("message", j["error"].dump());
                } else {
                    err_msg = j["error"].dump();
                }
                return Result<VLMResponse>::failure(ErrorCode::INTERNAL_ERROR,
                                                     "VLM API error: " + err_msg);
            }

            if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
                resp.text = j["choices"][0]["message"].value("content", "");
            } else if (j.contains("message")) {
                // Ollama format
                resp.text = j["message"].value("content", "");
            }

            if (j.contains("usage")) {
                resp.tokens_used = j["usage"].value("total_tokens", 0);
            }
        }

        return Result<VLMResponse>::success(std::move(resp));

    } catch (const nlohmann::json::exception& e) {
        return Result<VLMResponse>::failure(
            ErrorCode::INTERNAL_ERROR,
            std::string("Failed to parse VLM response: ") + e.what());
    }
}

// ---------------------------------------------------------------------------
// Full query execution
// ---------------------------------------------------------------------------
Result<VLMResponse> VLMAssistant::Impl::execute(const std::string& system_prompt,
                                                  const std::string& user_prompt,
                                                  const Image* image) {
    if (!configured) {
        return Result<VLMResponse>::failure(
            ErrorCode::INVALID_PARAMETER,
            "VLM assistant is not configured. Call configure() first.");
    }

    std::string url = api_url();
    std::string headers = build_headers_json();
    std::string body = build_request_body(system_prompt, user_prompt, image);

    auto start = std::chrono::steady_clock::now();
    auto http_result = do_http_post(url, headers, body);
    auto end = std::chrono::steady_clock::now();
    double latency_ms = std::chrono::duration<double, std::milli>(end - start).count();

    if (!http_result.ok()) {
        return Result<VLMResponse>::failure(http_result.code, http_result.message);
    }

    return parse_response(http_result.value, latency_ms);
}

// ---------------------------------------------------------------------------
// VLMAssistant public API
// ---------------------------------------------------------------------------
VLMAssistant::VLMAssistant() : impl_(std::make_unique<Impl>()) {}
VLMAssistant::~VLMAssistant() = default;

VLMAssistant::VLMAssistant(VLMAssistant&&) noexcept = default;
VLMAssistant& VLMAssistant::operator=(VLMAssistant&&) noexcept = default;

Result<void> VLMAssistant::configure(const VLMConfig& config) {
    if (config.provider != "openai" &&
        config.provider != "anthropic" &&
        config.provider != "ollama" &&
        config.provider != "custom") {
        return Result<void>::failure(
            ErrorCode::INVALID_PARAMETER,
            "Unsupported VLM provider: " + config.provider +
            ". Supported: openai, anthropic, ollama, custom");
    }

    if (config.provider != "ollama" && config.api_key.empty()) {
        return Result<void>::failure(
            ErrorCode::INVALID_PARAMETER,
            "API key is required for provider: " + config.provider);
    }

    if (config.max_tokens <= 0) {
        return Result<void>::failure(
            ErrorCode::INVALID_PARAMETER,
            "max_tokens must be positive");
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->config = config;
    impl_->configured = true;
    return Result<void>::success();
}

bool VLMAssistant::is_configured() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->configured;
}

const VLMConfig& VLMAssistant::config() const {
    return impl_->config;
}

void VLMAssistant::set_http_caller(HttpCaller caller) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->http_caller = std::move(caller);
}

Result<VLMResponse> VLMAssistant::describe_defect(const Image& image,
                                                    const DefectRegion& defect) {
    std::string system_prompt =
        "You are a quality inspection assistant analyzing manufacturing defects. "
        "Describe the defect in technical terms suitable for a quality report. "
        "Include: defect type, severity assessment, likely cause, and recommended action.";

    std::ostringstream user_prompt;
    user_prompt << "Analyze the defect in this image.\n"
                << "Defect region: bounding box (" << defect.bounding_box.x << ", "
                << defect.bounding_box.y << ", " << defect.bounding_box.w << "x"
                << defect.bounding_box.h << ")\n"
                << "Type: " << (defect.type.empty() ? "unknown" : defect.type) << "\n"
                << "Area: " << defect.area_mm2 << " mm^2\n"
                << "Max height deviation: " << defect.max_height << " mm\n"
                << "Avg height deviation: " << defect.avg_height << " mm\n"
                << "Centroid: (" << defect.centroid.x << ", " << defect.centroid.y << ")";

    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->execute(system_prompt, user_prompt.str(), &image);
}

Result<VLMResponse> VLMAssistant::suggest_parameters(const Image& sample_image,
                                                       const std::string& context) {
    std::string system_prompt =
        "You are a vision inspection setup assistant. Given a sample image of a part or "
        "surface to inspect, suggest optimal inspection parameters including: "
        "appropriate ROI, threshold values, expected feature sizes, and recommended "
        "inspection algorithms (height threshold, flatness, coplanarity, template compare).";

    std::string user_prompt = "Analyze this sample image and suggest inspection parameters.";
    if (!context.empty()) {
        user_prompt += "\nAdditional context: " + context;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->execute(system_prompt, user_prompt, &sample_image);
}

Result<VLMResponse> VLMAssistant::generate_report(const std::vector<InspectionResult>& results,
                                                    const std::string& template_text) {
    std::string system_prompt =
        "You are a quality report generator. Create a professional quality inspection "
        "report from the provided inspection results. Include summary statistics, "
        "pass/fail rates, defect categorization, and trend observations.";

    std::ostringstream user_prompt;
    user_prompt << "Generate a quality inspection report from these results:\n\n";

    int pass_count = 0;
    int fail_count = 0;
    int total_defects = 0;

    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        user_prompt << "--- Inspection " << (i + 1) << " ---\n";
        user_prompt << "Result: " << (r.ok ? "PASS" : "FAIL") << "\n";
        user_prompt << "Timestamp: " << r.timestamp << "\n";
        user_prompt << "Recipe: " << r.recipe_name << "\n";
        user_prompt << "Defects: " << r.defects.size() << "\n";

        for (const auto& d : r.defects) {
            user_prompt << "  - Type: " << d.type
                        << ", Area: " << d.area_mm2 << " mm^2"
                        << ", MaxH: " << d.max_height << " mm\n";
        }

        user_prompt << "Measurements: " << r.measures.size() << "\n";
        for (const auto& m : r.measures) {
            user_prompt << "  - Min: " << m.min_height
                        << ", Max: " << m.max_height
                        << ", Avg: " << m.avg_height
                        << ", Std: " << m.std_height << "\n";
        }

        if (r.ok) ++pass_count; else ++fail_count;
        total_defects += static_cast<int>(r.defects.size());
        user_prompt << "\n";
    }

    user_prompt << "Summary: " << pass_count << " passed, " << fail_count << " failed, "
                << total_defects << " total defects.\n";

    if (!template_text.empty()) {
        user_prompt << "\nUse the following template format:\n" << template_text;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->execute(system_prompt, user_prompt.str(), nullptr);
}

Result<VLMResponse> VLMAssistant::query(const std::string& prompt, const Image* image) {
    if (prompt.empty()) {
        return Result<VLMResponse>::failure(
            ErrorCode::INVALID_PARAMETER, "Prompt cannot be empty");
    }

    std::string system_prompt =
        "You are a vision inspection assistant for VxlStudio, a 3D structured-light "
        "inspection system. Answer questions about inspection, quality control, "
        "and manufacturing processes.";

    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->execute(system_prompt, prompt, image);
}

Result<std::vector<std::pair<std::string, float>>> VLMAssistant::suggest_labels(
    const Image& image, const std::vector<std::string>& candidate_labels) {
    if (candidate_labels.empty()) {
        return Result<std::vector<std::pair<std::string, float>>>::failure(
            ErrorCode::INVALID_PARAMETER, "Candidate labels cannot be empty");
    }

    std::string system_prompt =
        "You are an image classification assistant. Given an image and candidate labels, "
        "return a JSON array of objects with 'label' and 'confidence' (0-1) fields, "
        "sorted by confidence descending. Only include labels that reasonably match. "
        "Respond ONLY with the JSON array, no other text.";

    std::ostringstream user_prompt;
    user_prompt << "Classify this image using these candidate labels:\n";
    for (const auto& label : candidate_labels) {
        user_prompt << "- " << label << "\n";
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto resp = impl_->execute(system_prompt, user_prompt.str(), &image);
    if (!resp.ok()) {
        return Result<std::vector<std::pair<std::string, float>>>::failure(
            resp.code, resp.message);
    }

    // Parse the JSON array from the response text
    try {
        auto j = nlohmann::json::parse(resp.value.text);
        std::vector<std::pair<std::string, float>> labels;
        if (j.is_array()) {
            for (const auto& item : j) {
                std::string label = item.value("label", "");
                float conf = item.value("confidence", 0.0f);
                if (!label.empty()) {
                    labels.emplace_back(std::move(label), conf);
                }
            }
        }
        return Result<std::vector<std::pair<std::string, float>>>::success(std::move(labels));
    } catch (const nlohmann::json::exception&) {
        // If the model didn't return valid JSON, return the text as a single label
        std::vector<std::pair<std::string, float>> fallback;
        fallback.emplace_back(resp.value.text, 0.0f);
        return Result<std::vector<std::pair<std::string, float>>>::success(std::move(fallback));
    }
}

// ---------------------------------------------------------------------------
// Global singleton
// ---------------------------------------------------------------------------
VLMAssistant& vlm_assistant() {
    static VLMAssistant instance;
    return instance;
}

} // namespace vxl
