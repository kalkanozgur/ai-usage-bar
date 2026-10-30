#include "core/usage_math.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <system_error>

#include "core/time_util.hpp"

namespace cub::usage {
namespace {

void append_pct(std::string& out, const std::optional<double>& v) {
    if (!v) {
        return;
    }
    std::array<char, 32> buf{};
    const auto r = std::to_chars(buf.data(), buf.data() + buf.size(), *v,
                                 std::chars_format::fixed, 1);
    out.append(buf.data(), r.ptr);
}

void append_int(std::string& out, const std::optional<std::int64_t>& v) {
    if (!v) {
        return;
    }
    std::array<char, 24> buf{};
    const auto r = std::to_chars(buf.data(), buf.data() + buf.size(), *v);
    out.append(buf.data(), r.ptr);
}

template <class T>
bool parse_num(std::string_view f, T& out) noexcept {
    if (f.empty()) {
        return false;
    }
    const auto r = std::from_chars(f.data(), f.data() + f.size(), out);
    return r.ec == std::errc{} && r.ptr == f.data() + f.size();
}

/// Empty → nullopt (ok); malformed → false.
template <class T>
bool parse_opt(std::string_view f, std::optional<T>& out) noexcept {
    if (f.empty()) {
        out.reset();
        return true;
    }
    T v{};
    if (!parse_num(f, v)) {
        return false;
    }
    out = v;
    return true;
}

bool same_window(const std::optional<std::int64_t>& a,
                 const std::optional<std::int64_t>& b) noexcept {
    if (!a || !b) {
        return !a && !b;
    }
    const std::int64_t d = *a - *b;
    return (d < 0 ? -d : d) <= kResetToleranceSec;
}

const std::optional<double>& pct_of(const Sample& s, Window w) noexcept {
    return w == Window::Session ? s.session_pct : s.week_pct;
}

const std::optional<std::int64_t>& reset_of(const Sample& s, Window w) noexcept {
    return w == Window::Session ? s.session_reset : s.week_reset;
}

}  // namespace

Sample from_snapshot(std::int64_t now, const api::UsageSnapshot& snap) noexcept {
    Sample s;
    s.ts = now;
    if (snap.five_hour) {
        s.session_pct = snap.five_hour->utilization;
        s.session_reset = snap.five_hour->resets_at;
    }
    if (snap.seven_day) {
        s.week_pct = snap.seven_day->utilization;
        s.week_reset = snap.seven_day->resets_at;
    }
    return s;
}

std::string_view csv_header() noexcept {
    return "ts,session_pct,session_reset,week_pct,week_reset";
}

std::string to_csv_row(const Sample& s) {
    std::string out;
    out.reserve(64);
    append_int(out, s.ts);
    out += ',';
    append_pct(out, s.session_pct);
    out += ',';
    append_int(out, s.session_reset);
    out += ',';
    append_pct(out, s.week_pct);
    out += ',';
    append_int(out, s.week_reset);
    return out;
}

std::optional<Sample> parse_csv_row(std::string_view line) {
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    std::array<std::string_view, 5> f{};
    std::size_t n = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == ',') {
            if (n == f.size()) {
                return std::nullopt;  // too many fields
            }
            f[n++] = line.substr(start, i - start);
            start = i + 1;
        }
    }
    if (n != f.size()) {
        return std::nullopt;
    }
    Sample s;
    if (!parse_num(f[0], s.ts) || !parse_opt(f[1], s.session_pct) ||
        !parse_opt(f[2], s.session_reset) || !parse_opt(f[3], s.week_pct) ||
        !parse_opt(f[4], s.week_reset)) {
        return std::nullopt;
    }
    return s;
}

std::string csv_file_name(std::int64_t ts) {
    std::int64_t days = ts / 86400;
    if (ts % 86400 < 0) {
        --days;
    }
    const time::CivilDate c = time::civil_from_days(days);
    std::string out = "usage-";
    append_int(out, c.year);
    out += '-';
    if (c.month < 10) {
        out += '0';
    }
    append_int(out, static_cast<std::int64_t>(c.month));
    out += ".csv";
    return out;
}

bool append_csv(const std::filesystem::path& file, const Sample& s) {
    std::error_code ec;
    const bool fresh = !std::filesystem::exists(file, ec) || std::filesystem::file_size(file, ec) == 0;
    std::ofstream out(file, std::ios::binary | std::ios::app);
    if (!out) {
        return false;
    }
    if (fresh) {
        out << csv_header() << "\r\n";
    }
    out << to_csv_row(s) << "\r\n";
    return static_cast<bool>(out);
}

std::optional<Pace> pace(double utilization, std::int64_t resets_at, std::int64_t window_sec,
                         std::int64_t now) noexcept {
    if (window_sec <= 0) {
        return std::nullopt;
    }
    const std::int64_t remaining = std::clamp<std::int64_t>(resets_at - now, 0, window_sec);
    const auto elapsed = static_cast<double>(window_sec - remaining);
    Pace p;
    p.expected_pct = 100.0 * elapsed / static_cast<double>(window_sec);
    p.delta_pct = utilization - p.expected_pct;
    return p;
}

std::optional<std::int64_t> projected_full(std::span<const Sample> samples, Window w) noexcept {
    // Latest sample carrying this window decides which window we are in.
    std::size_t last = samples.size();
    while (last > 0 && !pct_of(samples[last - 1], w)) {
        --last;
    }
    if (last == 0) {
        return std::nullopt;
    }
    const Sample& tail = samples[last - 1];
    const auto reset = reset_of(tail, w);

    // Walk back over the contiguous run of samples in the same window.
    std::size_t first = last - 1;
    while (first > 0) {
        const Sample& prev = samples[first - 1];
        if (!pct_of(prev, w)) {
            --first;  // a gap in this field does not end the window
            continue;
        }
        if (!same_window(reset_of(prev, w), reset) || *pct_of(prev, w) > *pct_of(tail, w)) {
            break;
        }
        --first;
    }

    const std::int64_t t0 = samples[first].ts;
    double n = 0.0;
    double sum_t = 0.0;
    double sum_y = 0.0;
    for (std::size_t i = first; i < last; ++i) {
        if (const auto& y = pct_of(samples[i], w)) {
            n += 1.0;
            sum_t += static_cast<double>(samples[i].ts - t0);
            sum_y += *y;
        }
    }
    if (n < 2.0 || tail.ts - t0 < 60) {
        return std::nullopt;
    }
    const double mean_t = sum_t / n;
    const double mean_y = sum_y / n;
    double sxy = 0.0;
    double sxx = 0.0;
    for (std::size_t i = first; i < last; ++i) {
        if (const auto& y = pct_of(samples[i], w)) {
            const double dt = static_cast<double>(samples[i].ts - t0) - mean_t;
            sxy += dt * (*y - mean_y);
            sxx += dt * dt;
        }
    }
    if (sxx <= 0.0) {
        return std::nullopt;
    }
    const double slope = sxy / sxx;  // percent per second
    const double y_last = *pct_of(tail, w);
    if (y_last >= 100.0) {
        return tail.ts;
    }
    if (slope <= 1e-9) {
        return std::nullopt;
    }
    const auto full = tail.ts + static_cast<std::int64_t>(std::llround((100.0 - y_last) / slope));
    if (reset && full >= *reset) {
        return std::nullopt;
    }
    return full;
}

std::optional<double> session_to_week_rate(std::span<const Sample> samples) noexcept {
    double sum_ds = 0.0;
    double sum_dw = 0.0;
    const Sample* prev = nullptr;
    for (const Sample& s : samples) {
        if (!s.session_pct || !s.week_pct) {
            continue;
        }
        if (prev != nullptr && same_window(prev->session_reset, s.session_reset) &&
            same_window(prev->week_reset, s.week_reset)) {
            const double ds = *s.session_pct - *prev->session_pct;
            const double dw = *s.week_pct - *prev->week_pct;
            if (ds > 0.0 && dw >= 0.0) {
                sum_ds += ds;
                sum_dw += dw;
            }
        }
        prev = &s;
    }
    if (sum_ds < 1.0) {
        return std::nullopt;
    }
    return sum_dw / sum_ds;
}

}  // namespace cub::usage
