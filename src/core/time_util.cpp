#include "core/time_util.hpp"

#include <cstdio>

namespace cub::time {
namespace {

/// Reads exactly `n` decimal digits starting at `pos`.
std::optional<int> digits(std::string_view s, std::size_t pos, std::size_t n) noexcept {
    if (pos + n > s.size()) {
        return std::nullopt;
    }
    int v = 0;
    for (std::size_t i = pos; i < pos + n; ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        v = v * 10 + (c - '0');
    }
    return v;
}

bool expect(std::string_view s, std::size_t pos, char c) noexcept {
    return pos < s.size() && s[pos] == c;
}

}  // namespace

std::optional<std::int64_t> parse_iso8601(std::string_view s) noexcept {
    // 0123456789012345678
    // YYYY-MM-DDTHH:MM:SS
    const auto year = digits(s, 0, 4);
    const auto month = digits(s, 5, 2);
    const auto day = digits(s, 8, 2);
    const auto hour = digits(s, 11, 2);
    const auto minute = digits(s, 14, 2);
    const auto second = digits(s, 17, 2);
    if (!year || !month || !day || !hour || !minute || !second || !expect(s, 4, '-') ||
        !expect(s, 7, '-') || !(expect(s, 10, 'T') || expect(s, 10, ' ')) ||
        !expect(s, 13, ':') || !expect(s, 16, ':')) {
        return std::nullopt;
    }
    if (*month < 1 || *month > 12 || *day < 1 || *day > 31 || *hour > 23 || *minute > 59 ||
        *second > 60) {
        return std::nullopt;
    }

    std::size_t pos = 19;
    if (expect(s, pos, '.')) {  // fractional seconds are ignored
        ++pos;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
            ++pos;
        }
    }

    std::int64_t offset = 0;
    if (pos < s.size()) {
        const char z = s[pos];
        if (z == 'Z' || z == 'z') {
            ++pos;
        } else if (z == '+' || z == '-') {
            const auto oh = digits(s, pos + 1, 2);
            std::size_t mpos = pos + 3;
            if (expect(s, mpos, ':')) {
                ++mpos;
            }
            const auto om = digits(s, mpos, 2);
            if (!oh || !om) {
                return std::nullopt;
            }
            offset = (static_cast<std::int64_t>(*oh) * 3600 + *om * 60) * (z == '+' ? 1 : -1);
            pos = mpos + 2;
        } else {
            return std::nullopt;
        }
    }
    if (pos != s.size()) {
        return std::nullopt;
    }

    const std::int64_t days =
        days_from_civil(*year, static_cast<unsigned>(*month), static_cast<unsigned>(*day));
    return days * 86400 + static_cast<std::int64_t>(*hour) * 3600 + *minute * 60 + *second -
           offset;
}

std::string format_remaining(std::int64_t seconds) {
    if (seconds < 0) {
        seconds = 0;
    }
    const std::int64_t d = seconds / 86400;
    const std::int64_t h = (seconds % 86400) / 3600;
    const std::int64_t m = (seconds % 3600) / 60;

    char buf[32];
    if (d > 0) {
        std::snprintf(buf, sizeof buf, "%lldg%02llds", static_cast<long long>(d),
                      static_cast<long long>(h));
    } else if (h > 0) {
        std::snprintf(buf, sizeof buf, "%llds%02llddk", static_cast<long long>(h),
                      static_cast<long long>(m));
    } else {
        std::snprintf(buf, sizeof buf, "%llddk", static_cast<long long>(m));
    }
    return buf;
}

}  // namespace cub::time
