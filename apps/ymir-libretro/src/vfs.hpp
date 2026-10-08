#pragma once

// File access for the libretro core.
//
// When the frontend provides a VFS interface (v3+), every file the core touches goes through it, including disc
// images read by ymir-core's loaders. That is the only way to reach content the host file system can't open, such as
// Android Storage Access Framework URIs ("saf://...") or physical CD-ROM drives ("cdrom://..."). Without a frontend
// VFS, the host file system is used directly.

#include <ymir/media/binary_reader/binary_reader.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct retro_vfs_interface;
struct retro_vfs_file_handle;

namespace ymir_libretro::vfs {

// Installs the frontend VFS interface for this module and for ymir-core's disc image loaders.
// nullptr selects the host file system.
void Init(const retro_vfs_interface *iface);

// Returns the frontend VFS interface, or nullptr if the host file system is in use.
const retro_vfs_interface *Interface();

// Converts between libretro's UTF-8 path strings and std::filesystem::path.
std::filesystem::path ToPath(const std::string &utf8);
std::string FromPath(const std::filesystem::path &path);

// Appends a file name to a directory path.
std::string Join(const std::string &dir, const std::string &name);

// Reads an entire file. Returns std::nullopt if it can't be opened or fully read.
std::optional<std::vector<uint8_t>> ReadFile(const std::string &path);

// Returns the size of a regular file, or std::nullopt if the path isn't an existing regular file.
std::optional<uint64_t> FileSize(const std::string &path);

// Creates or replaces a file. Returns false on failure.
bool WriteFile(const std::string &path, std::span<const uint8_t> data);

struct DirEntry {
    std::string name;
    bool isDirectory;
};

// Lists a directory. Returns an empty list if it doesn't exist.
std::vector<DirEntry> ListDir(const std::string &path);

// IBinaryReader over a frontend VFS file handle, which it owns and closes.
class VfsBinaryReader final : public ymir::media::IBinaryReader {
public:
    VfsBinaryReader(const retro_vfs_interface *vfs, retro_vfs_file_handle *handle, uintmax_t size);
    ~VfsBinaryReader() override;

    VfsBinaryReader(const VfsBinaryReader &) = delete;
    VfsBinaryReader &operator=(const VfsBinaryReader &) = delete;

    uintmax_t Size() const final;
    uintmax_t Read(uintmax_t offset, uintmax_t size, std::span<uint8> output) const final;

private:
    const retro_vfs_interface *m_vfs;
    retro_vfs_file_handle *m_handle;
    uintmax_t m_size;
};

} // namespace ymir_libretro::vfs
