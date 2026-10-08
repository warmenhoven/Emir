#include "vfs.hpp"

#include "libretro.h"

#include <ymir/media/file_access.hpp>

#include <algorithm>
#include <bit>
#include <fstream>
#include <iterator>
#include <memory>
#include <system_error>

namespace ymir_libretro::vfs {

namespace {

const retro_vfs_interface *g_vfs = nullptr;

// Strict UTF-8 check (RFC 3629): rejects overlong encodings, UTF-16 surrogates and code points above U+10FFFF.
bool IsValidUtf8(const std::string &str) {
    static constexpr uint32_t kMinCodePoint[] = {0, 0, 0x80, 0x800, 0x10000};
    for (size_t i = 0; i < str.size();) {
        const auto lead = static_cast<uint8_t>(str[i]);
        const size_t len = lead < 0x80 ? 1 : std::countl_one(lead);
        if ((lead >= 0x80 && len == 1) || len > 4 || i + len > str.size()) {
            return false;
        }
        uint32_t codePoint = len == 1 ? lead : lead & (0xFF >> (len + 1));
        for (size_t j = 1; j < len; j++) {
            const auto cont = static_cast<uint8_t>(str[i + j]);
            if ((cont & 0xC0) != 0x80) {
                return false;
            }
            codePoint = (codePoint << 6) | (cont & 0x3F);
        }
        if (codePoint < kMinCodePoint[len] || codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
            return false;
        }
        i += len;
    }
    return true;
}

bool IsFile(const std::string &path) {
    if (g_vfs == nullptr) {
        std::error_code err;
        return std::filesystem::is_regular_file(ToPath(path), err);
    }
    // RetroArch sets IS_VALID for directories too; libretro.py only for regular files. Sizes come from open + size, so
    // don't ask for one here (RetroArch's SAF backend reports a document it can't size as invalid when asked).
    const int flags = g_vfs->stat(path.c_str(), nullptr);
    return (flags & RETRO_VFS_STAT_IS_VALID) != 0 && (flags & RETRO_VFS_STAT_IS_DIRECTORY) == 0;
}

// Opens a file for reading through the frontend VFS and gets its 64-bit size (retro_vfs_stat_t only reports 32 bits).
retro_vfs_file_handle *OpenForRead(const std::string &path, int64_t &size) {
    retro_vfs_file_handle *handle =
        g_vfs->open(path.c_str(), RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);
    if (handle == nullptr) {
        return nullptr;
    }
    size = g_vfs->size(handle);
    if (size < 0) {
        g_vfs->close(handle);
        return nullptr;
    }
    return handle;
}

// Routes ymir-core's disc image loaders through the frontend VFS
class VfsFileProvider final : public ymir::media::io::IFileProvider {
public:
    bool IsRegularFile(const std::filesystem::path &path) final {
        return IsFile(FromPath(path));
    }

    std::unique_ptr<ymir::media::IBinaryReader> Open(const std::filesystem::path &path, std::error_code &err) final {
        int64_t size = 0;
        retro_vfs_file_handle *handle = OpenForRead(FromPath(path), size);
        if (handle == nullptr) {
            err = std::make_error_code(std::errc::no_such_file_or_directory);
            return nullptr;
        }
        err.clear();
        return std::make_unique<VfsBinaryReader>(g_vfs, handle, static_cast<uintmax_t>(size));
    }
};

VfsFileProvider g_provider;

} // namespace

void Init(const retro_vfs_interface *iface) {
    g_vfs = iface;
    ymir::media::io::SetProvider(iface != nullptr ? &g_provider : nullptr);
}

const retro_vfs_interface *Interface() {
    return g_vfs;
}

std::filesystem::path ToPath(const std::string &utf8) {
    // Converting invalid UTF-8 fails (with an exception) on Windows. Use the native narrow encoding for such strings
    // instead, e.g. a legacy-encoded M3U.
    if (!IsValidUtf8(utf8)) {
        return std::filesystem::path{utf8};
    }
    return std::filesystem::path{std::u8string{utf8.begin(), utf8.end()}};
}

std::string FromPath(const std::filesystem::path &path) {
    const std::u8string utf8 = path.u8string();
    return std::string{utf8.begin(), utf8.end()};
}

std::string Join(const std::string &dir, const std::string &name) {
    if (dir.empty() || dir.back() == '/' || dir.back() == '\\') {
        return dir + name;
    }
    return dir + '/' + name;
}

std::optional<std::vector<uint8_t>> ReadFile(const std::string &path) {
    if (g_vfs == nullptr) {
        std::ifstream in{ToPath(path), std::ios::binary};
        if (!in) {
            return std::nullopt;
        }
        return std::vector<uint8_t>(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
    }
    int64_t size = 0;
    retro_vfs_file_handle *handle = OpenForRead(path, size);
    if (handle == nullptr) {
        return std::nullopt;
    }
    std::vector<uint8_t> data(static_cast<size_t>(size));
    size_t total = 0;
    while (total < data.size()) {
        const int64_t got = g_vfs->read(handle, data.data() + total, data.size() - total);
        if (got <= 0) {
            break;
        }
        total += static_cast<size_t>(got);
    }
    g_vfs->close(handle);
    if (total != data.size()) {
        return std::nullopt;
    }
    return data;
}

std::optional<uint64_t> FileSize(const std::string &path) {
    if (!IsFile(path)) {
        return std::nullopt;
    }
    if (g_vfs == nullptr) {
        std::error_code err;
        const auto size = std::filesystem::file_size(ToPath(path), err);
        if (err) {
            return std::nullopt;
        }
        return size;
    }
    int64_t size = 0;
    retro_vfs_file_handle *handle = OpenForRead(path, size);
    if (handle == nullptr) {
        return std::nullopt;
    }
    g_vfs->close(handle);
    return static_cast<uint64_t>(size);
}

bool WriteFile(const std::string &path, std::span<const uint8_t> data) {
    if (g_vfs == nullptr) {
        std::ofstream out{ToPath(path), std::ios::binary | std::ios::trunc};
        out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
        return static_cast<bool>(out);
    }
    retro_vfs_file_handle *handle =
        g_vfs->open(path.c_str(), RETRO_VFS_FILE_ACCESS_WRITE, RETRO_VFS_FILE_ACCESS_HINT_NONE);
    if (handle == nullptr) {
        return false;
    }
    const int64_t written = g_vfs->write(handle, data.data(), data.size());
    g_vfs->close(handle);
    return written == static_cast<int64_t>(data.size());
}

std::vector<DirEntry> ListDir(const std::string &path) {
    std::vector<DirEntry> entries;
    if (g_vfs == nullptr) {
        std::error_code err;
        for (const auto &entry : std::filesystem::directory_iterator(ToPath(path), err)) {
            entries.push_back({FromPath(entry.path().filename()), entry.is_directory(err)});
        }
        return entries;
    }
    retro_vfs_dir_handle *dir = g_vfs->opendir(path.c_str(), false);
    if (dir == nullptr) {
        return entries;
    }
    while (g_vfs->readdir(dir)) {
        if (const char *name = g_vfs->dirent_get_name(dir)) {
            entries.push_back({name, g_vfs->dirent_is_dir(dir)});
        }
    }
    g_vfs->closedir(dir);
    return entries;
}

VfsBinaryReader::VfsBinaryReader(const retro_vfs_interface *vfs, retro_vfs_file_handle *handle, uintmax_t size)
    : m_vfs(vfs)
    , m_handle(handle)
    , m_size(size) {}

VfsBinaryReader::~VfsBinaryReader() {
    if (m_handle != nullptr) {
        m_vfs->close(m_handle);
    }
}

uintmax_t VfsBinaryReader::Size() const {
    return m_size;
}

// Mirrors FileBinaryReader::Read semantics: reads up to size bytes at offset, clamped to the file size and output
// buffer, returning the bytes actually read. Loops because frontends may return short reads.
uintmax_t VfsBinaryReader::Read(uintmax_t offset, uintmax_t size, std::span<uint8> output) const {
    if (m_handle == nullptr || offset >= m_size) {
        return 0;
    }
    size = std::min(size, m_size - offset);
    size = std::min<uintmax_t>(size, output.size());
    if (size == 0) {
        return 0;
    }
    if (m_vfs->seek(m_handle, static_cast<int64_t>(offset), RETRO_VFS_SEEK_POSITION_START) < 0) {
        return 0;
    }
    uintmax_t total = 0;
    while (total < size) {
        const int64_t got = m_vfs->read(m_handle, output.data() + total, size - total);
        if (got <= 0) {
            break;
        }
        total += static_cast<uintmax_t>(got);
    }
    return total;
}

} // namespace ymir_libretro::vfs
