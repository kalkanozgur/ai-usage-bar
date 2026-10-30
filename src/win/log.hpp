#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace cub::log {

/// Opens the log file; truncates it first if it grew past `max_bytes`.
void init(const std::filesystem::path& file, std::uintmax_t max_bytes = 256 * 1024);

/// Thread-safe, timestamped line. Never throws. Never pass secrets here.
void write(std::string_view msg) noexcept;

}  // namespace cub::log
