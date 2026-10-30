// Stores the claude.ai sessionKey encrypted with DPAPI (current user scope).
// Only the same Windows account on the same machine can decrypt the file.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace cub::win::secret {

bool save(const std::filesystem::path& file, std::string_view plaintext);
[[nodiscard]] std::optional<std::string> load(const std::filesystem::path& file);
void remove(const std::filesystem::path& file);

}  // namespace cub::win::secret
