#include "core/process_kind.hpp"

#include <string>

namespace cub::process {
namespace {

std::wstring lower_ascii(std::wstring_view s) {
    std::wstring out(s);
    for (auto& c : out) {
        if (c >= L'A' && c <= L'Z') {
            c = static_cast<wchar_t>(c - L'A' + L'a');
        }
    }
    return out;
}

bool contains(std::wstring_view hay, std::wstring_view needle) {
    return !needle.empty() && hay.find(needle) != std::wstring_view::npos;
}

}  // namespace

Kind classify(std::wstring_view path, std::wstring_view extra_desktop_hint) {
    const std::wstring p = lower_ascii(path);
    const auto slash = p.find_last_of(L"\\/");
    const std::wstring_view file =
        slash == std::wstring::npos ? std::wstring_view(p) : std::wstring_view(p).substr(slash + 1);
    if (file != L"claude.exe") {
        return Kind::Other;
    }
    if (contains(p, L"\\anthropicclaude\\") || contains(p, L"\\windowsapps\\claude_") ||
        contains(p, L"\\programs\\claude\\") || contains(p, lower_ascii(extra_desktop_hint))) {
        return Kind::ClaudeDesktop;
    }
    return Kind::ClaudeCli;
}

}  // namespace cub::process
