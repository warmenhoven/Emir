#include "cdrom_loader.hpp"

#include "libretro.h"
#include "vfs.hpp"

#include <ymir/media/binary_reader/binary_reader.hpp>
#include <ymir/media/frame_address.hpp>

#include <ymir/util/scope_guard.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace ymir_libretro {

namespace {

struct ParsedIndex {
    uint32 number;
    uint32 posFrames; // relative to the start of the track's file
};

struct ParsedTrack {
    std::string binPath;
    uint32 number = 0;
    bool isData = false;
    bool mode2 = false;
    std::vector<ParsedIndex> indices;
};

// Extracts the path from a `FILE "<path>" BINARY` line (the quoted portion).
std::string ParseFilePath(const std::string &line) {
    const auto first = line.find('"');
    const auto last = line.find_last_of('"');
    if (first == std::string::npos || last == std::string::npos || last <= first) {
        return {};
    }
    return line.substr(first + 1, last - first - 1);
}

// Parses a MM:SS:FF token into a frame count.
bool ParseMSF(const std::string &msf, uint32 &outFrames) {
    if (msf.size() < 8 || msf[2] != ':' || msf[5] != ':') {
        return false;
    }
    try {
        const uint32 m = static_cast<uint32>(std::stoul(msf.substr(0, 2)));
        const uint32 s = static_cast<uint32>(std::stoul(msf.substr(3, 2)));
        const uint32 f = static_cast<uint32>(std::stoul(msf.substr(6, 2)));
        outFrames = ymir::media::TimestampToFrameAddress(m, s, f);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

bool IsCDROMPath(const std::string &path) {
    static constexpr char kScheme[] = "cdrom://";
    static constexpr size_t kLen = sizeof(kScheme) - 1;
    if (path.size() < kLen) {
        return false;
    }
    for (size_t i = 0; i < kLen; ++i) {
        if (std::tolower(static_cast<unsigned char>(path[i])) != kScheme[i]) {
            return false;
        }
    }
    return true;
}

bool LoadCDROMDisc(const retro_vfs_interface *vfs, const std::string &cuePath, ymir::media::Disc &disc,
                   ymir::media::CbLoaderMessage cbMsg) {
    auto errorMsg = [&](std::string message) { cbMsg(ymir::media::MessageType::Error, message); };
    auto debugMsg = [&](std::string message) { cbMsg(ymir::media::MessageType::Debug, message); };

    util::ScopeGuard sgInvalidateDisc{[&] { disc.Invalidate(); }};

    if (vfs == nullptr) {
        errorMsg("CD-ROM: frontend VFS interface (v3+) is required for physical disc access");
        return false;
    }

    // --- Read the synthesized cue sheet describing the physical disc ---
    struct retro_vfs_file_handle *cueHandle =
        vfs->open(cuePath.c_str(), RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);
    if (cueHandle == nullptr) {
        errorMsg("CD-ROM: could not open the physical disc (is a disc inserted?)");
        return false;
    }
    std::string cueText;
    {
        const int64_t cueSize = vfs->size(cueHandle);
        if (cueSize > 0) {
            cueText.resize(static_cast<size_t>(cueSize));
            vfs->seek(cueHandle, 0, RETRO_VFS_SEEK_POSITION_START);
            const int64_t got = vfs->read(cueHandle, cueText.data(), static_cast<uint64_t>(cueSize));
            cueText.resize(got < 0 ? 0 : static_cast<size_t>(got));
        }
        vfs->close(cueHandle);
    }
    if (cueText.empty()) {
        errorMsg("CD-ROM: could not read the disc table of contents");
        return false;
    }

    // --- Parse the cue sheet (RetroArch emits one FILE per track) ---
    std::vector<ParsedTrack> tracks;
    std::string pendingFile;
    {
        std::istringstream text{cueText};
        std::string line;
        while (std::getline(text, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            std::istringstream ins{line};
            std::string keyword;
            ins >> keyword;
            if (keyword == "FILE") {
                pendingFile = ParseFilePath(line);
            } else if (keyword == "TRACK") {
                uint32 number = 0;
                std::string format;
                ins >> number >> format;
                if (number < 1 || number > 99) {
                    errorMsg("CD-ROM: invalid track number in disc table of contents");
                    return false;
                }
                auto &track = tracks.emplace_back();
                track.binPath = pendingFile;
                track.number = number;
                track.isData = format.starts_with("MODE");
                track.mode2 = format.starts_with("MODE2");
            } else if (keyword == "INDEX") {
                if (tracks.empty()) {
                    continue;
                }
                uint32 number = 0;
                std::string msf;
                ins >> number >> msf;
                uint32 frames = 0;
                if (ParseMSF(msf, frames)) {
                    tracks.back().indices.push_back({number, frames});
                }
            }
        }
    }

    if (tracks.empty()) {
        errorMsg("CD-ROM: no tracks found on the disc");
        return false;
    }

    // --- Build the Disc (single session) ---
    disc.sessions.clear();
    auto &session = disc.sessions.emplace_back();
    session.startFrameAddress = 0;

    // Track 1 data begins at LBA 0 == FAD 150, matching the physical disc so
    // data-sector embedded headers stay aligned. Tracks are laid out gaplessly,
    // with audio pregaps (from INDEX 00/01) inserted as uncovered gaps. Reads are
    // always per-track and thus physically correct regardless of this layout;
    // only the TOC we hand the game depends on it, and the game sees only that TOC.
    uint32 fad = 150;
    bool first = true;
    for (const ParsedTrack &pt : tracks) {
        auto findIndex = [&](uint32 n) -> const ParsedIndex * {
            for (const ParsedIndex &idx : pt.indices) {
                if (idx.number == n) {
                    return &idx;
                }
            }
            return nullptr;
        };
        const ParsedIndex *idx00 = findIndex(0);
        const ParsedIndex *idx01 = findIndex(1);
        if (idx00 != nullptr && idx01 != nullptr && idx01->posFrames >= idx00->posFrames) {
            fad += idx01->posFrames - idx00->posFrames; // pregap gap before this track
        }

        struct retro_vfs_file_handle *binHandle =
            vfs->open(pt.binPath.c_str(), RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);
        if (binHandle == nullptr) {
            errorMsg("CD-ROM: could not open track data on the disc");
            return false;
        }
        const int64_t binSize = vfs->size(binHandle);
        const uint32 sectors = binSize > 0 ? static_cast<uint32>(binSize / 2352) : 0;
        if (sectors == 0) {
            vfs->close(binHandle);
            errorMsg("CD-ROM: track data is empty or unreadable");
            return false;
        }

        auto &track = session.tracks[pt.number - 1];
        track.SetSectorSize(2352);
        track.mode2 = pt.mode2;
        track.controlADR = pt.isData ? 0x41 : 0x01;
        track.startFrameAddress = fad;
        track.index01FrameAddress = fad;
        track.endFrameAddress = fad + sectors - 1;
        track.binaryReader =
            std::make_unique<ymir_libretro::vfs::VfsBinaryReader>(vfs, binHandle, static_cast<uintmax_t>(binSize));

        // indices[0] = dummy INDEX 00; indices[1] = INDEX 01 spanning the whole track.
        track.indices.clear();
        track.indices.emplace_back();
        {
            auto &i1 = track.indices.emplace_back();
            i1.startFrameAddress = track.startFrameAddress;
            i1.endFrameAddress = track.endFrameAddress;
        }

        if (first) {
            session.firstTrackIndex = pt.number - 1;
            first = false;
        }
        session.lastTrackIndex = pt.number - 1;
        ++session.numTracks;

        debugMsg("CD-ROM: track " + std::to_string(pt.number) + " (" + (pt.isData ? "data" : "audio") + "), " +
                 std::to_string(sectors) + " sectors, FAD " + std::to_string(track.startFrameAddress) + "-" +
                 std::to_string(track.endFrameAddress));

        fad = track.endFrameAddress + 1;
    }

    session.endFrameAddress = fad - 1;
    session.BuildTOC();

    // --- Read the Saturn header from the first (data) track ---
    auto &firstTrack = session.tracks[session.firstTrackIndex];
    const uintmax_t userDataOffset = firstTrack.mode2 ? 24 : 16;
    std::array<uint8, 256> header{};
    if (firstTrack.binaryReader->Read(userDataOffset, 256, header) < 256) {
        errorMsg("CD-ROM: could not read the Saturn disc header");
        return false;
    }
    disc.header.ReadFrom(header);

    sgInvalidateDisc.Cancel();
    return true;
}

} // namespace ymir_libretro
