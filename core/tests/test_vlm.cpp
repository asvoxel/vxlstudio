// VxlStudio -- VLM module unit tests
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "vxl/vlm.h"
#include "vxl/types.h"

// ---------------------------------------------------------------------------
// Default-constructed VLMAssistant is not configured
// ---------------------------------------------------------------------------
TEST(VLM, DefaultNotConfigured) {
    vxl::VLMAssistant assistant;
    EXPECT_FALSE(assistant.is_configured());
}

// ---------------------------------------------------------------------------
// configure() with valid OpenAI config succeeds
// ---------------------------------------------------------------------------
TEST(VLM, ConfigureOpenAI) {
    vxl::VLMAssistant assistant;
    vxl::VLMConfig config;
    config.provider = "openai";
    config.api_key = "test-key-12345";
    config.model = "gpt-4o";

    auto r = assistant.configure(config);
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(assistant.is_configured());
    EXPECT_EQ(assistant.config().provider, "openai");
    EXPECT_EQ(assistant.config().model, "gpt-4o");
}

// ---------------------------------------------------------------------------
// configure() with valid Anthropic config succeeds
// ---------------------------------------------------------------------------
TEST(VLM, ConfigureAnthropic) {
    vxl::VLMAssistant assistant;
    vxl::VLMConfig config;
    config.provider = "anthropic";
    config.api_key = "sk-ant-test";
    config.model = "claude-sonnet-4-20250514";

    auto r = assistant.configure(config);
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(assistant.is_configured());
}

// ---------------------------------------------------------------------------
// configure() with Ollama (no API key needed) succeeds
// ---------------------------------------------------------------------------
TEST(VLM, ConfigureOllama) {
    vxl::VLMAssistant assistant;
    vxl::VLMConfig config;
    config.provider = "ollama";
    config.model = "llava";
    config.base_url = "http://localhost:11434";

    auto r = assistant.configure(config);
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(assistant.is_configured());
}

// ---------------------------------------------------------------------------
// configure() with unsupported provider fails
// ---------------------------------------------------------------------------
TEST(VLM, ConfigureUnsupportedProvider) {
    vxl::VLMAssistant assistant;
    vxl::VLMConfig config;
    config.provider = "unsupported_provider";
    config.api_key = "key";

    auto r = assistant.configure(config);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.code, vxl::ErrorCode::INVALID_PARAMETER);
    EXPECT_FALSE(assistant.is_configured());
}

// ---------------------------------------------------------------------------
// configure() with missing API key (non-ollama) fails
// ---------------------------------------------------------------------------
TEST(VLM, ConfigureMissingApiKey) {
    vxl::VLMAssistant assistant;
    vxl::VLMConfig config;
    config.provider = "openai";
    config.api_key = "";  // missing

    auto r = assistant.configure(config);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.code, vxl::ErrorCode::INVALID_PARAMETER);
}

// ---------------------------------------------------------------------------
// configure() with invalid max_tokens fails
// ---------------------------------------------------------------------------
TEST(VLM, ConfigureInvalidMaxTokens) {
    vxl::VLMAssistant assistant;
    vxl::VLMConfig config;
    config.provider = "openai";
    config.api_key = "key";
    config.max_tokens = 0;

    auto r = assistant.configure(config);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.code, vxl::ErrorCode::INVALID_PARAMETER);
}

// ---------------------------------------------------------------------------
// describe_defect without configure -> INVALID_PARAMETER
// ---------------------------------------------------------------------------
TEST(VLM, DescribeDefectNotConfigured) {
    vxl::VLMAssistant assistant;
    vxl::Image img = vxl::Image::create(64, 64, vxl::PixelFormat::GRAY8);
    vxl::DefectRegion defect;
    defect.type = "scratch";
    defect.area_mm2 = 1.5f;

    auto r = assistant.describe_defect(img, defect);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.code, vxl::ErrorCode::INVALID_PARAMETER);
}

// ---------------------------------------------------------------------------
// query with empty prompt -> INVALID_PARAMETER
// ---------------------------------------------------------------------------
TEST(VLM, QueryEmptyPrompt) {
    vxl::VLMAssistant assistant;
    vxl::VLMConfig config;
    config.provider = "openai";
    config.api_key = "test-key";
    assistant.configure(config);

    auto r = assistant.query("");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.code, vxl::ErrorCode::INVALID_PARAMETER);
}

// ---------------------------------------------------------------------------
// suggest_labels with empty candidates -> INVALID_PARAMETER
// ---------------------------------------------------------------------------
TEST(VLM, SuggestLabelsEmptyCandidates) {
    vxl::VLMAssistant assistant;
    vxl::VLMConfig config;
    config.provider = "openai";
    config.api_key = "test-key";
    assistant.configure(config);

    vxl::Image img = vxl::Image::create(32, 32, vxl::PixelFormat::GRAY8);
    auto r = assistant.suggest_labels(img, {});
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.code, vxl::ErrorCode::INVALID_PARAMETER);
}

// ---------------------------------------------------------------------------
// describe_defect with mock http_caller -> parses response
// ---------------------------------------------------------------------------
TEST(VLM, DescribeDefectWithMockCaller) {
    vxl::VLMAssistant assistant;
    vxl::VLMConfig config;
    config.provider = "openai";
    config.api_key = "test-key";
    config.model = "gpt-4o";
    assistant.configure(config);

    // Set a mock HTTP caller that returns a fake OpenAI response
    assistant.set_http_caller(
        [](const std::string& /*url*/, const std::string& /*headers*/,
           const std::string& /*body*/) -> std::string {
            return R"({
                "choices": [{
                    "message": {
                        "content": "Surface scratch detected, approximately 1.5mm long."
                    }
                }],
                "usage": {"total_tokens": 42}
            })";
        });

    vxl::Image img = vxl::Image::create(64, 64, vxl::PixelFormat::GRAY8);
    vxl::DefectRegion defect;
    defect.type = "scratch";
    defect.area_mm2 = 1.5f;
    defect.bounding_box = {10, 20, 30, 40};

    auto r = assistant.describe_defect(img, defect);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_FALSE(r.value.text.empty());
    EXPECT_NE(r.value.text.find("scratch"), std::string::npos);
    EXPECT_EQ(r.value.tokens_used, 42);
    EXPECT_GT(r.value.latency_ms, 0.0);
}

// ---------------------------------------------------------------------------
// query with mock Anthropic caller -> parses Messages API response
// ---------------------------------------------------------------------------
TEST(VLM, QueryWithAnthropicMock) {
    vxl::VLMAssistant assistant;
    vxl::VLMConfig config;
    config.provider = "anthropic";
    config.api_key = "sk-ant-test";
    config.model = "claude-sonnet-4-20250514";
    assistant.configure(config);

    assistant.set_http_caller(
        [](const std::string& /*url*/, const std::string& /*headers*/,
           const std::string& /*body*/) -> std::string {
            return R"({
                "content": [{
                    "type": "text",
                    "text": "The inspection looks nominal."
                }],
                "usage": {"input_tokens": 10, "output_tokens": 8}
            })";
        });

    auto r = assistant.query("Is this part OK?");
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(r.value.text, "The inspection looks nominal.");
    EXPECT_EQ(r.value.tokens_used, 18);
}

// ---------------------------------------------------------------------------
// generate_report with mock caller
// ---------------------------------------------------------------------------
TEST(VLM, GenerateReportMock) {
    vxl::VLMAssistant assistant;
    vxl::VLMConfig config;
    config.provider = "openai";
    config.api_key = "test-key";
    assistant.configure(config);

    assistant.set_http_caller(
        [](const std::string& /*url*/, const std::string& /*headers*/,
           const std::string& /*body*/) -> std::string {
            return R"({
                "choices": [{
                    "message": {"content": "Quality Report: 2 inspections, 100% pass rate."}
                }],
                "usage": {"total_tokens": 50}
            })";
        });

    std::vector<vxl::InspectionResult> results;
    vxl::InspectionResult r1;
    r1.ok = true;
    r1.recipe_name = "solder_check";
    r1.timestamp = "2026-01-01T00:00:00Z";
    results.push_back(r1);

    vxl::InspectionResult r2;
    r2.ok = true;
    r2.recipe_name = "solder_check";
    r2.timestamp = "2026-01-01T00:01:00Z";
    results.push_back(r2);

    auto r = assistant.generate_report(results);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_NE(r.value.text.find("Quality Report"), std::string::npos);
}

// ---------------------------------------------------------------------------
// API error in response is properly propagated
// ---------------------------------------------------------------------------
TEST(VLM, ApiErrorResponse) {
    vxl::VLMAssistant assistant;
    vxl::VLMConfig config;
    config.provider = "openai";
    config.api_key = "invalid-key";
    assistant.configure(config);

    assistant.set_http_caller(
        [](const std::string& /*url*/, const std::string& /*headers*/,
           const std::string& /*body*/) -> std::string {
            return R"({"error": {"message": "Invalid API key", "type": "auth_error"}})";
        });

    auto r = assistant.query("test");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.code, vxl::ErrorCode::INTERNAL_ERROR);
    EXPECT_NE(r.message.find("Invalid API key"), std::string::npos);
}

// ---------------------------------------------------------------------------
// HTTP caller exception is caught and returned as error
// ---------------------------------------------------------------------------
TEST(VLM, HttpCallerException) {
    vxl::VLMAssistant assistant;
    vxl::VLMConfig config;
    config.provider = "openai";
    config.api_key = "test-key";
    assistant.configure(config);

    assistant.set_http_caller(
        [](const std::string&, const std::string&,
           const std::string&) -> std::string {
            throw std::runtime_error("Connection refused");
        });

    auto r = assistant.query("test");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.code, vxl::ErrorCode::IO_CONNECTION_FAILED);
    EXPECT_NE(r.message.find("Connection refused"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Move semantics compile and don't crash
// ---------------------------------------------------------------------------
TEST(VLM, MoveSemantics) {
    vxl::VLMAssistant a;
    vxl::VLMConfig config;
    config.provider = "ollama";
    config.model = "llava";
    a.configure(config);
    EXPECT_TRUE(a.is_configured());

    vxl::VLMAssistant b(std::move(a));
    EXPECT_TRUE(b.is_configured());

    vxl::VLMAssistant c;
    c = std::move(b);
    EXPECT_TRUE(c.is_configured());
}

// ---------------------------------------------------------------------------
// Global singleton returns the same instance
// ---------------------------------------------------------------------------
TEST(VLM, GlobalSingleton) {
    auto& a = vxl::vlm_assistant();
    auto& b = vxl::vlm_assistant();
    EXPECT_EQ(&a, &b);
}

// ---------------------------------------------------------------------------
// VLMConfig defaults
// ---------------------------------------------------------------------------
TEST(VLM, ConfigDefaults) {
    vxl::VLMConfig config;
    EXPECT_EQ(config.provider, "openai");
    EXPECT_EQ(config.model, "gpt-4o");
    EXPECT_TRUE(config.api_key.empty());
    EXPECT_TRUE(config.base_url.empty());
    EXPECT_EQ(config.timeout_seconds, 30);
    EXPECT_EQ(config.max_tokens, 1024);
}

// ---------------------------------------------------------------------------
// VLMResponse defaults
// ---------------------------------------------------------------------------
TEST(VLM, ResponseDefaults) {
    vxl::VLMResponse resp;
    EXPECT_TRUE(resp.text.empty());
    EXPECT_FLOAT_EQ(resp.confidence, 0.0f);
    EXPECT_EQ(resp.tokens_used, 0);
    EXPECT_DOUBLE_EQ(resp.latency_ms, 0.0);
}
