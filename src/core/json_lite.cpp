#include "core/json_lite.hpp"

#include <charconv>
#include <cstdint>

namespace cub::json {
namespace {

constexpr bool is_ws(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/// One past the closing quote of the string starting at `pos` (which must be '"').
std::size_t string_end(std::string_view doc, std::size_t pos) noexcept {
    for (std::size_t i = pos + 1; i < doc.size(); ++i) {
        if (doc[i] == '\\') {
            ++i;  // skip escaped char
        } else if (doc[i] == '"') {
            return i + 1;
        }
    }
    return npos;
}

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::optional<std::uint32_t> parse_hex4(std::string_view s) noexcept {
    if (s.size() < 4) {
        return std::nullopt;
    }
    std::uint32_t v = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + 4, v, 16);
    if (ec != std::errc{} || ptr != s.data() + 4) {
        return std::nullopt;
    }
    return v;
}

}  // namespace

std::size_t skip_ws(std::string_view doc, std::size_t pos) noexcept {
    while (pos < doc.size() && is_ws(doc[pos])) {
        ++pos;
    }
    return pos;
}

std::size_t value_end(std::string_view doc, std::size_t pos) noexcept {
    pos = skip_ws(doc, pos);
    if (pos >= doc.size()) {
        return npos;
    }
    const char c = doc[pos];
    if (c == '"') {
        return string_end(doc, pos);
    }
    if (c == '{' || c == '[') {
        int depth = 0;
        std::size_t i = pos;
        while (i < doc.size()) {
            const char ch = doc[i];
            if (ch == '"') {
                i = string_end(doc, i);
                if (i == npos) {
                    return npos;
                }
                continue;
            }
            if (ch == '{' || ch == '[') {
                ++depth;
            } else if (ch == '}' || ch == ']') {
                if (--depth == 0) {
                    return i + 1;
                }
            }
            ++i;
        }
        return npos;
    }
    // number / true / false / null
    std::size_t i = pos;
    while (i < doc.size()) {
        const char ch = doc[i];
        if (ch == ',' || ch == '}' || ch == ']' || is_ws(ch)) {
            break;
        }
        ++i;
    }
    return i == pos ? npos : i;
}

std::optional<Span> root(std::string_view doc) noexcept {
    const std::size_t b = skip_ws(doc, 0);
    const std::size_t e = value_end(doc, b);
    if (e == npos) {
        return std::nullopt;
    }
    return Span{b, e};
}

bool is_object(std::string_view doc, Span v) noexcept {
    return v.size() >= 2 && doc[v.begin] == '{';
}

bool is_array(std::string_view doc, Span v) noexcept {
    return v.size() >= 2 && doc[v.begin] == '[';
}

bool is_null(std::string_view doc, Span v) noexcept {
    return doc.substr(v.begin, v.size()) == "null";
}

std::optional<Span> member(std::string_view doc, Span obj, std::string_view key) noexcept {
    if (!is_object(doc, obj)) {
        return std::nullopt;
    }
    std::size_t i = skip_ws(doc, obj.begin + 1);
    while (i < obj.end && doc[i] == '"') {
        const std::size_t key_end = string_end(doc, i);
        if (key_end == npos || key_end > obj.end) {
            return std::nullopt;
        }
        const std::string_view raw_key = doc.substr(i + 1, key_end - i - 2);
        i = skip_ws(doc, key_end);
        if (i >= obj.end || doc[i] != ':') {
            return std::nullopt;
        }
        i = skip_ws(doc, i + 1);
        const std::size_t v_end = value_end(doc, i);
        if (v_end == npos || v_end > obj.end) {
            return std::nullopt;
        }
        if (raw_key == key) {
            return Span{i, v_end};
        }
        i = skip_ws(doc, v_end);
        if (i < obj.end && doc[i] == ',') {
            i = skip_ws(doc, i + 1);
        } else {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::string> as_string(std::string_view doc, Span v) {
    if (v.size() < 2 || doc[v.begin] != '"' || doc[v.end - 1] != '"') {
        return std::nullopt;
    }
    const std::string_view s = doc.substr(v.begin + 1, v.size() - 2);
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (++i >= s.size()) {
            return std::nullopt;
        }
        switch (s[i]) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                auto hi = parse_hex4(s.substr(i + 1));
                if (!hi) {
                    return std::nullopt;
                }
                i += 4;
                std::uint32_t cp = *hi;
                // Combine a UTF-16 surrogate pair: 😀
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < s.size() &&
                    s.substr(i + 1, 2) == "\\u") {
                    if (auto lo = parse_hex4(s.substr(i + 3)); lo && *lo >= 0xDC00 && *lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (*lo - 0xDC00);
                        i += 6;
                    }
                }
                append_utf8(out, cp);
                break;
            }
            default: return std::nullopt;
        }
    }
    return out;
}

std::optional<double> as_number(std::string_view doc, Span v) noexcept {
    const std::string_view s = doc.substr(v.begin, v.size());
    if (s.empty()) {
        return std::nullopt;
    }
    double value = 0.0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return value;
}

std::string quote(std::string_view s) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += "\\u00";
                    out.push_back(kHex[(c >> 4) & 0xF]);
                    out.push_back(kHex[c & 0xF]);
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
    return out;
}

}  // namespace cub::json
