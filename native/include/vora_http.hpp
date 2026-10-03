#pragma once
#include "vora_runtime.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif
#include <memory>

namespace vora {
#ifdef _WIN32
struct HttpHandle {
    HINTERNET value = nullptr;
    explicit HttpHandle(HINTERNET handle) : value(handle) { if (!value) throw TransientError("HTTP initialization failed."); }
    ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
    HttpHandle(const HttpHandle&) = delete;
    HttpHandle& operator=(const HttpHandle&) = delete;
};

#endif

class LocalProvider {
    unsigned short port_ = 18080;
    std::string path_;
    std::string model_;
    int tokens_;
    bool ollama_;
    int reasoning_;
    bool critique_;
public:
    LocalProvider(const std::string& endpoint, std::string model, int tokens = 256, int reasoning = 0, bool critique = false)
        : model_(std::move(model)), tokens_(tokens), ollama_(false), reasoning_(reasoning), critique_(critique) {
        std::smatch match;
        const std::regex allowed(R"(^http://(?:127\.0\.0\.1|localhost):([0-9]{1,5})(/v1/chat/completions|/api/chat)$)");
        if (!std::regex_match(endpoint, match, allowed))
            throw std::runtime_error("Endpoint must be loopback HTTP with explicit port and /v1/chat/completions or /api/chat.");
        const int port = std::stoi(match[1].str());
        if (port < 1 || port > 65535 || tokens < 1 || tokens > 8192 || !has_text(model_))
            throw std::runtime_error("Invalid model, port or max-tokens (1..8192).");
        if (reasoning < 0 || reasoning > 2048 || reasoning >= tokens)
            throw std::runtime_error("Reasoning budget must be 0..2048 and below max-tokens.");
        port_ = static_cast<unsigned short>(port);
        const auto path = match[2].str();
        path_ = path;
        ollama_ = path == "/api/chat";
        if (ollama_ && reasoning_ > 0) throw std::runtime_error("Bounded reasoning is supported only by the llama.cpp endpoint.");
    }

    std::string operator()(const std::string& role, const std::string& prompt) const {
        nlohmann::json body = {{"model", model_}, {"stream", false}, {"messages", {
            {{"role", "system"}, {"content", role}}, {{"role", "user"}, {"content", prompt}}
        }}};
        if (ollama_) body["options"] = {{"num_predict", tokens_}};
        else {
            body["max_tokens"] = tokens_;
            body["temperature"] = 0.2;
            body["chat_template_kwargs"] = {{"enable_thinking", reasoning_ > 0}};
            if (reasoning_ > 0) {
                body["reasoning_format"] = "deepseek";
                body["reasoning_budget_tokens"] = reasoning_;
            }
        }
        if (critique_) {
            const nlohmann::json schema = {
                {"type", "object"}, {"additionalProperties", false},
                {"properties", {
                    {"verdict", {{"type", "string"}, {"enum", {"keep", "revise"}}}},
                    {"issues", {{"type", "array"}, {"items", {{"type", "string"}, {"minLength", 1}}}}},
                    {"instruction", {{"type", "string"}, {"minLength", 1}}}
                }}, {"required", {"issues", "verdict", "instruction"}}
            };
            if (ollama_) body["format"] = schema;
            else body["response_format"] = {{"type", "json_schema"}, {"json_schema", {
                {"name", "critique"}, {"strict", true}, {"schema", schema}
            }}};
        }
        const auto payload = body.dump();
        if (payload.size() > 4 * 1024 * 1024) throw std::runtime_error("Request too large.");
        std::string raw;
        long status = 0;
#ifdef _WIN32
        const std::wstring wide_path(path_.begin(), path_.end());
        HttpHandle session(WinHttpOpen(L"VoraNative/0.3.1", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!WinHttpSetTimeouts(session.value, 10000, 10000, 120000, 120000)) throw TransientError("HTTP timeout setup failed.");
        HttpHandle connection(WinHttpConnect(session.value, L"127.0.0.1", port_, 0));
        HttpHandle request(WinHttpOpenRequest(connection.value, L"POST", wide_path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
        DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)))
            throw std::runtime_error("Could not disable redirects.");
        if (!WinHttpSendRequest(request.value, L"Content-Type: application/json\r\n", static_cast<DWORD>(-1),
                                const_cast<char*>(payload.data()), static_cast<DWORD>(payload.size()), static_cast<DWORD>(payload.size()), 0)
                || !WinHttpReceiveResponse(request.value, nullptr)) throw TransientError("Local model connection failed.");
        DWORD windows_status = 0, size = sizeof(windows_status);
        if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &windows_status, &size, WINHTTP_NO_HEADER_INDEX))
            throw TransientError("Missing HTTP status.");
        status = windows_status;
        char buffer[8192];
        DWORD bytes = 0;
        do {
            if (!WinHttpReadData(request.value, buffer, sizeof(buffer), &bytes)) throw TransientError("Incomplete HTTP response.");
            raw.append(buffer, bytes);
            if (raw.size() > 4 * 1024 * 1024) throw std::runtime_error("Response exceeds 4 MiB.");
        } while (bytes);
#else
        static const bool initialized = [] { return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK; }();
        if (!initialized) throw TransientError("HTTP initialization failed.");
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), curl_easy_cleanup);
        if (!handle) throw TransientError("HTTP initialization failed.");
        curl_slist* list = curl_slist_append(nullptr, "Content-Type: application/json");
        if (!list) throw std::runtime_error("HTTP header allocation failed.");
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(list, curl_slist_free_all);
        const auto url = "http://127.0.0.1:" + std::to_string(port_) + path_;
        struct ResponseBuffer { std::string* text; bool too_large = false; } buffer{&raw};
        auto write = +[](char* data, size_t size, size_t count, void* context) noexcept -> size_t {
            auto& output = *static_cast<ResponseBuffer*>(context);
            constexpr size_t limit = 4 * 1024 * 1024;
            if (size && count > (limit - output.text->size()) / size) {
                output.too_large = true;
                return 0;
            }
            const size_t bytes = size * count;
            try { output.text->append(data, bytes); } catch (...) { return 0; }
            return bytes;
        };
        auto option = [&](CURLoption key, auto value) {
            if (curl_easy_setopt(handle.get(), key, value) != CURLE_OK)
                throw std::runtime_error("HTTP option setup failed.");
        };
        option(CURLOPT_URL, url.c_str());
        option(CURLOPT_PROXY, "");
        option(CURLOPT_FOLLOWLOCATION, 0L);
        option(CURLOPT_NOSIGNAL, 1L);
        option(CURLOPT_CONNECTTIMEOUT_MS, 10000L);
        option(CURLOPT_TIMEOUT_MS, 120000L);
        option(CURLOPT_HTTPHEADER, headers.get());
        option(CURLOPT_POSTFIELDS, payload.c_str());
        option(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(payload.size()));
        option(CURLOPT_WRITEFUNCTION, write);
        option(CURLOPT_WRITEDATA, &buffer);
        const auto code = curl_easy_perform(handle.get());
        if (buffer.too_large) throw std::runtime_error("Response exceeds 4 MiB.");
        if (code != CURLE_OK) throw TransientError("Local model connection failed.");
        if (curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status) != CURLE_OK)
            throw TransientError("Missing HTTP status.");
#endif
        if (status == 429 || status >= 500) throw TransientError("Local model is temporarily unavailable.");
        if (status != 200) throw std::runtime_error("Local model rejected the request.");
        const auto response = nlohmann::json::parse(raw);
        nlohmann::json message;
        if (ollama_) {
            if (!response.value("done", false)) throw std::runtime_error("Incomplete model response.");
            message = response.at("message");
        } else {
            const auto& choices = response.at("choices");
            if (!choices.is_array() || choices.empty()) throw std::runtime_error("Missing choices.");
            const auto& choice = choices.at(0);
            if (!choice.contains("finish_reason") || !choice["finish_reason"].is_string()
                || choice["finish_reason"].get<std::string>() != "stop")
                throw std::runtime_error("Incomplete, truncated or unsupported model completion.");
            message = choices.at(0).at("message");
        }
        if (message.contains("tool_calls") && !message["tool_calls"].empty()) throw std::runtime_error("Tool calls are not supported.");
        const auto content = message.at("content").get<std::string>();
        if (!has_text(content)) throw std::runtime_error("Empty model output.");
        return content;
    }
};
}
