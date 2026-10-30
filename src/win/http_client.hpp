// Thin RAII wrapper over WinHTTP. One session is kept for the app lifetime, so
// TLS connections to the same host are pooled and reused between polls.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace cub::win {

struct HttpResponse {
    unsigned long status = 0;
    std::string body;
};

class HttpClient {
public:
    explicit HttpClient(std::wstring_view user_agent);
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    /// HTTPS request. `headers` are CRLF-separated "Name: value" lines.
    /// Returns nullopt on transport failure and fills `error`.
    [[nodiscard]] std::optional<HttpResponse> request(std::wstring_view method,
                                                      std::wstring_view host,
                                                      std::wstring_view path,
                                                      std::wstring_view headers,
                                                      std::string_view body,
                                                      std::string* error = nullptr) const;

private:
    void* session_ = nullptr;  // HINTERNET
};

}  // namespace cub::win
