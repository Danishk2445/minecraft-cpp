#include "core/Fs.h"

#include <cstdio>
#include <system_error>

namespace mc::fs {
namespace {

template <class Container>
std::optional<Container> readAll(const std::filesystem::path& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return std::nullopt;
    Container out;
    if (std::fseek(f, 0, SEEK_END) == 0) {
        const long size = std::ftell(f);
        if (size > 0) out.resize(size_t(size));
        std::fseek(f, 0, SEEK_SET);
    }
    size_t got = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), f);
    // Files whose size the seek did not report (pipes, procfs) are read in chunks.
    if (out.empty()) {
        char buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
        got = out.size();
    }
    std::fclose(f);
    out.resize(got);
    return out;
}

} // namespace

std::optional<std::string> readText(const std::filesystem::path& path) { return readAll<std::string>(path); }

std::optional<std::vector<uint8_t>> readBytes(const std::filesystem::path& path) {
    return readAll<std::vector<uint8_t>>(path);
}

bool writeAtomic(const std::filesystem::path& path, std::span<const uint8_t> data) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    if (std::fclose(f) != 0 || !ok) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    std::filesystem::rename(tmp, path, ec);
    return !ec;
}

bool writeTextAtomic(const std::filesystem::path& path, std::string_view text) {
    return writeAtomic(path, std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}

} // namespace mc::fs
