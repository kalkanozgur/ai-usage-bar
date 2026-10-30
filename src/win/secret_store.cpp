#include "win/secret_store.hpp"

#include <windows.h>
#include <wincrypt.h>  // CryptProtectData / CryptUnprotectData

#include "win/util.hpp"

namespace cub::win::secret {
namespace {

// Extra entropy so other DPAPI consumers in the same account cannot trivially reuse the blob.
constexpr char kEntropy[] = "ClaudeUsageBar.sessionKey.v1";

DATA_BLOB entropy_blob() {
    DATA_BLOB b{};
    b.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy));
    b.cbData = sizeof(kEntropy) - 1;
    return b;
}

}  // namespace

bool save(const std::filesystem::path& file, std::string_view plaintext) {
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data()));
    in.cbData = static_cast<DWORD>(plaintext.size());
    DATA_BLOB ent = entropy_blob();
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"ClaudeUsageBar", &ent, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const bool ok = write_file_atomic(
        file, std::string_view(reinterpret_cast<const char*>(out.pbData), out.cbData));
    LocalFree(out.pbData);
    return ok;
}

std::optional<std::string> load(const std::filesystem::path& file) {
    auto blob = read_file(file);
    if (!blob || blob->empty()) {
        return std::nullopt;
    }
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE*>(blob->data());
    in.cbData = static_cast<DWORD>(blob->size());
    DATA_BLOB ent = entropy_blob();
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                            &out)) {
        return std::nullopt;
    }
    std::string result(reinterpret_cast<const char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}

void remove(const std::filesystem::path& file) {
    std::error_code ec;
    std::filesystem::remove(file, ec);
}

}  // namespace cub::win::secret
