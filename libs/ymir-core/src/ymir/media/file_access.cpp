#include <ymir/media/file_access.hpp>

#include <ymir/media/binary_reader/binary_reader_mmap.hpp>

#include <array>
#include <cstdio>
#include <fstream>

namespace ymir::media::io {

namespace {

IFileProvider *g_provider = nullptr;

// Opens a file through the provider, guaranteeing that err is set on failure.
std::unique_ptr<IBinaryReader> OpenWithProvider(const std::filesystem::path &path, std::error_code &err) {
    err.clear();
    auto reader = g_provider->Open(path, err);
    if (!reader && !err) {
        err = std::make_error_code(std::errc::io_error);
    }
    return reader;
}

// Read-only, seekable stream buffer over an IBinaryReader.
class ReaderStreamBuf final : public std::streambuf {
public:
    explicit ReaderStreamBuf(std::unique_ptr<IBinaryReader> reader)
        : m_reader(std::move(reader)) {}

protected:
    int_type underflow() final {
        if (gptr() == egptr()) {
            const uintmax_t pos = m_bufferPos + (gptr() - eback());
            const std::span<uint8> buffer{reinterpret_cast<uint8 *>(m_buffer.data()), m_buffer.size()};
            const uintmax_t count = m_reader->Read(pos, buffer.size(), buffer);
            m_bufferPos = pos;
            setg(m_buffer.data(), m_buffer.data(), m_buffer.data() + count);
        }
        return gptr() == egptr() ? traits_type::eof() : traits_type::to_int_type(*gptr());
    }

    pos_type seekoff(off_type off, std::ios::seekdir dir, std::ios::openmode which) final {
        off_type base;
        switch (dir) {
        case std::ios::beg: base = 0; break;
        case std::ios::cur: base = static_cast<off_type>(m_bufferPos + (gptr() - eback())); break;
        case std::ios::end: base = static_cast<off_type>(m_reader->Size()); break;
        default: return pos_type(off_type(-1));
        }
        return seekpos(pos_type(base + off), which);
    }

    pos_type seekpos(pos_type pos, std::ios::openmode which) final {
        if (!(which & std::ios::in) || off_type(pos) < 0) {
            return pos_type(off_type(-1));
        }
        // Drop the buffer; the next read refills it from the new position
        m_bufferPos = static_cast<uintmax_t>(off_type(pos));
        setg(m_buffer.data(), m_buffer.data(), m_buffer.data());
        return pos;
    }

private:
    std::unique_ptr<IBinaryReader> m_reader;
    std::array<char, 4096> m_buffer{};
    uintmax_t m_bufferPos = 0; // file offset of m_buffer[0]
};

// libchdr core file over a reader opened through the provider
struct CHDCoreFile {
    std::unique_ptr<IBinaryReader> reader;
    uint64_t pos = 0;
};

uint64_t CHDCoreFileSize(void *file) {
    return static_cast<CHDCoreFile *>(file)->reader->Size();
}

size_t CHDCoreFileRead(void *buffer, size_t size, size_t count, void *file) {
    auto &f = *static_cast<CHDCoreFile *>(file);
    if (size == 0) {
        return 0;
    }
    const uintmax_t read = f.reader->Read(f.pos, size * count, std::span{static_cast<uint8 *>(buffer), size * count});
    f.pos += read;
    return read / size;
}

int CHDCoreFileClose(void *file) {
    delete static_cast<CHDCoreFile *>(file);
    return 0;
}

int CHDCoreFileSeek(void *file, int64_t offset, int whence) {
    auto &f = *static_cast<CHDCoreFile *>(file);
    int64_t base;
    switch (whence) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = static_cast<int64_t>(f.pos); break;
    case SEEK_END: base = static_cast<int64_t>(f.reader->Size()); break;
    default: return -1;
    }
    if (base + offset < 0) {
        return -1;
    }
    f.pos = static_cast<uint64_t>(base + offset);
    return 0;
}

const core_file_callbacks kCHDCoreFileCallbacks{CHDCoreFileSize, CHDCoreFileRead, CHDCoreFileClose, CHDCoreFileSeek};

} // namespace

void SetProvider(IFileProvider *provider) {
    g_provider = provider;
}

bool is_regular_file(const std::filesystem::path &path) {
    if (g_provider == nullptr) {
        return std::filesystem::is_regular_file(path);
    }
    return g_provider->IsRegularFile(path);
}

uintmax_t file_size(const std::filesystem::path &path) {
    if (g_provider == nullptr) {
        return std::filesystem::file_size(path);
    }
    std::error_code err{};
    auto reader = OpenWithProvider(path, err);
    return reader ? reader->Size() : 0;
}

std::vector<uint8> ReadFile(const std::filesystem::path &path) {
    std::error_code err{};
    FileReader reader{path, err};
    if (err) {
        return {};
    }
    std::vector<uint8> data(reader.Size());
    data.resize(reader.Read(0, data.size(), data));
    return data;
}

ifstream::ifstream(const std::filesystem::path &path, std::ios::openmode mode)
    : std::istream(nullptr) {
    bool opened;
    if (g_provider == nullptr) {
        auto buf = std::make_unique<std::filebuf>();
        opened = buf->open(path, mode | std::ios::in) != nullptr;
        m_buf = std::move(buf);
    } else {
        std::error_code err{};
        auto reader = OpenWithProvider(path, err);
        opened = reader != nullptr;
        if (opened) {
            m_buf = std::make_unique<ReaderStreamBuf>(std::move(reader));
        } else {
            m_buf = std::make_unique<std::filebuf>(); // unopened, like a std::ifstream that failed to open
        }
    }
    rdbuf(m_buf.get()); // also clears the stream state
    if (!opened) {
        setstate(std::ios::failbit);
    }
}

FileReader::FileReader(const std::filesystem::path &path, std::error_code &err) {
    err.clear();
    if (g_provider == nullptr) {
        auto reader = std::make_unique<MemoryMappedBinaryReader>(path, err);
        if (!err) {
            m_reader = std::move(reader);
        }
    } else {
        m_reader = OpenWithProvider(path, err);
    }
}

uintmax_t FileReader::Size() const {
    return m_reader ? m_reader->Size() : 0;
}

uintmax_t FileReader::Read(uintmax_t offset, uintmax_t size, std::span<uint8> output) const {
    return m_reader ? m_reader->Read(offset, size, output) : 0;
}

chd_error chd_open(const std::filesystem::path &path, int mode, chd_file *parent, chd_file **chd) {
    if (g_provider == nullptr) {
        return ::chd_open(path.string().c_str(), mode, parent, chd);
    }
    std::error_code err{};
    auto reader = OpenWithProvider(path, err);
    if (!reader) {
        return CHDERR_FILE_NOT_FOUND;
    }
    // libchdr takes ownership: chd_close calls the fclose callback, which deletes the file -- including when the open
    // fails after libchdr allocated its chd_file
    return chd_open_core_file_callbacks(&kCHDCoreFileCallbacks, new CHDCoreFile{std::move(reader)}, mode, parent, chd);
}

} // namespace ymir::media::io
