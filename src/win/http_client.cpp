#include "win/http_client.hpp"

#include <windows.h>
#include <winhttp.h>

#include <string>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef WINHTTP_OPTION_DECOMPRESSION
#define WINHTTP_OPTION_DECOMPRESSION 118
#endif
#ifndef WINHTTP_DECOMPRESSION_FLAG_ALL
#define WINHTTP_DECOMPRESSION_FLAG_ALL 0x00000003
#endif

namespace cub::win {
namespace {

constexpr std::size_t kMaxBody = 1024 * 1024;

struct HandleCloser {
    void operator()(HINTERNET h) const noexcept {
        if (h != nullptr) {
            WinHttpCloseHandle(h);
        }
    }
};
using Handle = std::unique_ptr<void, HandleCloser>;

std::string winhttp_error(const char* where) {
    const DWORD code = GetLastError();  // read before any allocation can clobber it
    return std::string(where) + " failed (WinHTTP " + std::to_string(code) + ")";
}

}  // namespace

HttpClient::HttpClient(std::wstring_view user_agent) {
    const std::wstring ua(user_agent);
    HINTERNET s = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (s == nullptr) {  // pre-8.1 fallback
        s = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                        WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (s != nullptr) {
        WinHttpSetTimeouts(s, 10'000, 10'000, 15'000, 15'000);
        DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;
        WinHttpSetOption(s, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof decompress);
    }
    session_ = s;
}

HttpClient::~HttpClient() {
    if (session_ != nullptr) {
        WinHttpCloseHandle(session_);
    }
}

std::optional<HttpResponse> HttpClient::request(std::wstring_view method, std::wstring_view host,
                                                std::wstring_view path, std::wstring_view headers,
                                                std::string_view body, std::string* error) const {
    auto fail = [&](std::string msg) -> std::optional<HttpResponse> {
        if (error != nullptr) {
            *error = std::move(msg);
        }
        return std::nullopt;
    };
    if (session_ == nullptr) {
        return fail(winhttp_error("WinHttpOpen"));
    }

    const std::wstring host_s(host);
    const std::wstring path_s(path);
    const std::wstring method_s(method);
    std::wstring headers_s(headers);

    Handle conn(WinHttpConnect(session_, host_s.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!conn) {
        return fail(winhttp_error("WinHttpConnect"));
    }
    Handle req(WinHttpOpenRequest(conn.get(), method_s.c_str(), path_s.c_str(), nullptr,
                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  WINHTTP_FLAG_SECURE));
    if (!req) {
        return fail(winhttp_error("WinHttpOpenRequest"));
    }

    const BOOL sent = WinHttpSendRequest(
        req.get(), headers_s.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers_s.c_str(),
        headers_s.empty() ? 0 : static_cast<DWORD>(-1L),
        body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    if (!sent) {
        return fail(winhttp_error("WinHttpSendRequest"));
    }
    if (!WinHttpReceiveResponse(req.get(), nullptr)) {
        return fail(winhttp_error("WinHttpReceiveResponse"));
    }

    HttpResponse resp;
    DWORD status = 0;
    DWORD len = sizeof status;
    WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
    resp.status = status;

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.get(), &avail)) {
            return fail(winhttp_error("WinHttpQueryDataAvailable"));
        }
        if (avail == 0) {
            break;
        }
        const std::size_t old = resp.body.size();
        if (old + avail > kMaxBody) {
            return fail("response too large");
        }
        resp.body.resize(old + avail);
        DWORD read = 0;
        if (!WinHttpReadData(req.get(), resp.body.data() + old, avail, &read)) {
            return fail(winhttp_error("WinHttpReadData"));
        }
        resp.body.resize(old + read);
    }
    return resp;
}

}  // namespace cub::win
