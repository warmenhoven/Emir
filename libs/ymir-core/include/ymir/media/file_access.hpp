#pragma once

/**
@file
@brief Pluggable file access for the disc image loaders.

By default, files are accessed directly through the host file system, exactly like the std:: facilities these
functions mirror. A frontend that cannot rely on host paths (e.g. a libretro frontend handing out Android Storage
Access Framework URIs) can install an IFileProvider to route all disc image file access through its own file system.
*/

#include <ymir/media/binary_reader/binary_reader.hpp>

#include <libchdr/chd.h>

#include <filesystem>
#include <istream>
#include <memory>
#include <span>
#include <streambuf>
#include <system_error>
#include <vector>

namespace ymir::media::io {

/// @brief Read-only file system used by the disc image loaders.
struct IFileProvider {
    virtual ~IFileProvider() = default;

    /// @brief Determines if `path` refers to an existing regular file.
    virtual bool IsRegularFile(const std::filesystem::path &path) = 0;

    /// @brief Opens `path` for random-access reading.
    /// @return the reader, or `nullptr` on failure, in which case `err` is set
    virtual std::unique_ptr<IBinaryReader> Open(const std::filesystem::path &path, std::error_code &err) = 0;
};

/// @brief Installs the file provider used by the disc image loaders.
/// `nullptr` restores direct host file system access. Not thread-safe; install the provider before loading discs.
/// The provider must outlive every file opened through it.
void SetProvider(IFileProvider *provider);

/// @brief Drop-in replacement for `std::filesystem::is_regular_file`.
bool is_regular_file(const std::filesystem::path &path);

/// @brief Drop-in replacement for `std::filesystem::file_size`.
/// Under a provider, returns 0 if the file can't be opened, and the loader's own subsequent open reports the error.
uintmax_t file_size(const std::filesystem::path &path);

/// @brief Reads an entire file. Returns an empty vector on failure.
std::vector<uint8> ReadFile(const std::filesystem::path &path);

/// @brief Drop-in replacement for `std::ifstream` (read-only).
class ifstream : public std::istream {
public:
    explicit ifstream(const std::filesystem::path &path, std::ios::openmode mode = std::ios::in);

private:
    std::unique_ptr<std::streambuf> m_buf;
};

/// @brief Drop-in replacement for `MemoryMappedBinaryReader`: memory-maps the file from the host file system, or opens
/// it through the installed provider.
class FileReader final : public IBinaryReader {
public:
    FileReader(const std::filesystem::path &path, std::error_code &err);

    uintmax_t Size() const final;
    uintmax_t Read(uintmax_t offset, uintmax_t size, std::span<uint8> output) const final;

private:
    std::unique_ptr<IBinaryReader> m_reader;
};

/// @brief Replacement for libchdr's `chd_open` that takes a path.
chd_error chd_open(const std::filesystem::path &path, int mode, chd_file *parent, chd_file **chd);

} // namespace ymir::media::io
