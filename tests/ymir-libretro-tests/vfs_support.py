"""
Support code for the VFS tests (test_vfs.py).

VfsOnlyDriver is a frontend VFS that serves "vfstest://<root>/..." paths from real
directories. Those paths don't exist on the host file system, so the core can only
reach them through the VFS, exactly like the "saf://" URIs RetroArch hands out for
content picked through Android's Storage Access Framework (issue #2).

build_disc_fixtures() generates every disc image format the core supports from a
two-track (data + audio) CHD, using chdman, plus ffmpeg for the MP3/OGG audio tracks.
"""

import hashlib
import os
import shutil
import struct
import subprocess
import wave
from contextlib import contextmanager
from pathlib import Path

from libretro import DictOptionDriver, ExplicitPathDriver, Session
from libretro.drivers import DefaultFileSystemDriver, MultiVideoDriver

from conftest import BIOS_FILES, CORE_PATH, REPO_DIR

SCHEME = "vfstest://"

# Screens are compared after this many frames. By then the game has taken over from the
# BIOS; a disc whose data reads back wrong diverges from frame 600 on.
BOOT_FRAMES = 900

# Project-scoped scratch space for generated fixtures and save directories
SCRATCH_DIR = REPO_DIR / "tmp" / "ymir-libretro-tests"

SECTOR_SIZE = 2352
IPL_SIZE = 512 * 1024
CDB_ROM_SIZE = 64 * 1024
CART_SIZE = 2 * 1024 * 1024
SMPC_SIZE = 25
KOF95_CART = "mpr-18811-mx.ic1"


class VfsOnlyDriver(DefaultFileSystemDriver):
    """Serves vfstest://<root>/... from real directories; other paths pass through.

    Records every virtual path the core opened, listed, read from and wrote to.
    """

    def __init__(self, roots):
        super().__init__(3)
        self._roots = {name: Path(path) for name, path in roots.items()}
        self._names = {}
        self.opened = []
        self.listed = []
        self.bytes_read = {}
        self.bytes_written = {}

    def _resolve(self, path):
        text = os.fsdecode(path)
        if not text.startswith(SCHEME):
            return path
        root, _, rest = text[len(SCHEME):].partition("/")
        if root not in self._roots:
            return os.fsencode(f"/nonexistent-vfstest-root/{text}")
        return os.fsencode(self._roots[root] / rest)

    def open(self, path, mode, hints):
        try:
            handle = super().open(self._resolve(path), mode, hints)
        except OSError:
            return None
        if handle is not None:
            name = os.fsdecode(path)
            self._names[handle.id] = name
            self.opened.append(name)
        return handle

    def close(self, stream):
        self._names.pop(stream.id, None)
        return super().close(stream)

    def read(self, stream, buffer):
        count = super().read(stream, buffer)
        name = self._names.get(stream.id)
        if name is not None and count > 0:
            self.bytes_read[name] = self.bytes_read.get(name, 0) + count
        return count

    def write(self, stream, buffer):
        count = super().write(stream, buffer)
        name = self._names.get(stream.id)
        if name is not None and count > 0:
            self.bytes_written[name] = self.bytes_written.get(name, 0) + count
        return count

    def stat(self, path):
        return super().stat(self._resolve(path))

    def opendir(self, path, include_hidden):
        self.listed.append(os.fsdecode(path))
        resolved = self._resolve(path)
        if not os.path.isdir(resolved):
            return None
        return super().opendir(resolved, include_hidden)


@contextmanager
def core_session(game, system_dir, save_dir, vfs, options=None):
    """Session with explicit content path, system/save directories and VFS driver (None = no VFS)."""
    with Session(
        core=str(CORE_PATH),
        game=str(game),
        video=MultiVideoDriver(),
        options=DictOptionDriver(variables=options) if options else DictOptionDriver(),
        path=ExplicitPathDriver(corepath=str(CORE_PATH), system=str(system_dir), save=str(save_dir)),
        vfs=vfs,
    ) as s:
        yield s


def screen_hash(s, frames=BOOT_FRAMES):
    """Runs the given number of frames and hashes the final screen."""
    for _ in range(frames):
        s.run()
    shot = s.video.screenshot()
    assert shot is not None, "core produced no video frame"
    return hashlib.sha1(bytes(shot.data)).hexdigest()


def build_system_dir(src, out):
    """Copies the BIOS dumps and CD block ROMs from src into out, plus a dummy KoF95 ROM cartridge."""
    out.mkdir(parents=True, exist_ok=True)
    for name in BIOS_FILES:
        if (src / name).is_file():
            shutil.copyfile(src / name, out / name)
    if (src / "cdb").is_dir():
        shutil.copytree(src / "cdb", out / "cdb", dirs_exist_ok=True)
    (out / KOF95_CART).write_bytes(b"\xff" * CART_SIZE)
    return out


def build_disc_fixtures(chd, out):
    """Generates every supported disc format from a data + audio CHD into out.

    Returns {format key: (file name, [companion file names])}. MP3/OGG entries are
    only present when ffmpeg is installed.
    """
    out.mkdir(parents=True, exist_ok=True)
    built = {}

    shutil.copyfile(chd, out / "gf.chd")
    built["chd"] = ("gf.chd", [])

    subprocess.run(["chdman", "extractcd", "-i", str(chd), "-o", str(out / "gf.cue"), "-ob", str(out / "gf.bin")],
                   check=True, capture_output=True)
    built["cue"] = ("gf.cue", ["gf.bin"])

    subprocess.run(["chdman", "extractcd", "-i", str(chd), "-o", str(out / "gf_split.cue"), "-sb"],
                   check=True, capture_output=True)
    track1, track2 = "gf_split (Track 1).bin", "gf_split (Track 2).bin"
    built["cue_split"] = ("gf_split.cue", [track1, track2])

    data_frames = (out / track1).stat().st_size // SECTOR_SIZE
    total_frames = (out / "gf.bin").stat().st_size // SECTOR_SIZE
    pregap = 150
    track2_lba = data_frames + pregap  # track 2 INDEX 01 within the single-file image

    with wave.open(str(out / "gf_track2.wav"), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes((out / track2).read_bytes())
    audio_tracks = [("cue_wave", "gf_track2.wav", "WAVE")]
    if shutil.which("ffmpeg"):
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(out / "gf_track2.wav"),
                        "-c:a", "libmp3lame", "-b:a", "128k", str(out / "gf_track2.mp3")], check=True)
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(out / "gf_track2.wav"),
                        "-c:a", "vorbis", "-strict", "-2", str(out / "gf_track2.ogg")], check=True)
        audio_tracks += [("cue_mp3", "gf_track2.mp3", "MP3"), ("cue_ogg", "gf_track2.ogg", "OGG")]
    for key, audio, file_type in audio_tracks:
        cue = f"gf_{key.removeprefix('cue_')}.cue"
        _write_cue(out / cue, [
            (track1, "BINARY", [(1, "MODE1/2352", [(1, 0)])]),
            (audio, file_type, [(2, "AUDIO", [(0, 0), (1, pregap)])]),
        ])
        built[key] = (cue, [track1, audio])

    shutil.copyfile(out / track1, out / "gf_2352.iso")
    built["iso_2352"] = ("gf_2352.iso", [])
    raw = (out / track1).read_bytes()
    (out / "gf_2048.iso").write_bytes(b"".join(raw[o + 16:o + 16 + 2048] for o in range(0, len(raw), SECTOR_SIZE)))
    built["iso_2048"] = ("gf_2048.iso", [])

    shutil.copyfile(out / "gf.bin", out / "gf.img")
    (out / "gf.ccd").write_text(_ccd(track2_lba, total_frames))
    built["ccd"] = ("gf.ccd", ["gf.img"])

    shutil.copyfile(out / "gf.bin", out / "gf.mdf")
    (out / "gf.mds").write_bytes(_mds(track2_lba, total_frames))
    built["mds"] = ("gf.mds", ["gf.mdf"])

    (out / "gf.m3u").write_text("gf.cue\n")
    built["m3u"] = ("gf.m3u", ["gf.cue", "gf.bin"])
    return built


def _msf(frames):
    return frames // 4500, (frames // 75) % 60, frames % 75


def _write_cue(path, files):
    """files: [(file name, file type, [(track no, track type, [(index no, file-relative frame)])])]"""
    lines = []
    for file_name, file_type, tracks in files:
        lines.append(f'FILE "{file_name}" {file_type}')
        for track_no, track_type, indices in tracks:
            lines.append(f"  TRACK {track_no:02d} {track_type}")
            for index_no, frames in indices:
                m, s, f = _msf(frames)
                lines.append(f"    INDEX {index_no:02d} {m:02d}:{s:02d}:{f:02d}")
    path.write_text("\n".join(lines) + "\n")


def _ccd(track2_lba, total_frames):
    """CloneCD control file: data track at LBA 0, audio track at track2_lba, one session."""
    def entry(n, point, control, msf, plba):
        m, s, f = msf
        return (f"[Entry {n}]\nSession=1\nPoint=0x{point:02x}\nADR=0x01\nControl=0x{control:02x}\n"
                f"TrackNo=0\nAMin=0\nASec=0\nAFrame=0\nALBA=-150\nZero=0\n"
                f"PMin={m}\nPSec={s}\nPFrame={f}\nPLBA={plba}\n")
    return (
        "[CloneCD]\nVersion=3\n"
        "[Disc]\nTocEntries=5\nSessions=1\nDataTracksScrambled=0\nCDTextLength=0\n"
        "[Session 1]\nPreGapMode=1\nPreGapSubC=0\n"
        + entry(0, 0xA0, 4, (1, 0, 0), 4350)  # first track number in PMin
        + entry(1, 0xA1, 0, (2, 0, 0), 8850)  # last track number in PMin
        + entry(2, 0xA2, 0, _msf(total_frames + 150), total_frames)  # lead-out
        + entry(3, 1, 4, _msf(150), 0)
        + entry(4, 2, 0, _msf(track2_lba + 150), track2_lba)
    )


def _mds(track2_lba, total_frames):
    """Alcohol 120% MDS v1 (0x0301) descriptor for gf.mdf: one session, data + audio track.

    Layout matches the packed structs in libs/ymir-core/src/ymir/media/loader/loader_mdf_mds.cpp.
    The file name is "*.mdf", which the loader resolves to the .mds path with an .mdf extension.
    """
    header_size, session_size, track_size, footer_size = 0x58, 0x18, 0x50, 0x10
    session_offset = header_size
    tracks_offset = session_offset + session_size
    footer_offset = tracks_offset + 3 * track_size
    filename_offset = footer_offset + footer_size

    header = bytearray(header_size)
    header[0x00:0x10] = b"MEDIA DESCRIPTOR"
    struct.pack_into("<HHH", header, 0x10, 0x0301, 0, 1)  # version, medium type (CD-ROM), sessions
    struct.pack_into("<I", header, 0x50, session_offset)

    # session start/end, number, total blocks, lead-in blocks, first/last track, (unused), track blocks offset
    session = struct.pack("<iiHBBHHII", -150, total_frames, 1, 3, 1, 1, 2, 0, tracks_offset)

    def track(track_no, adr_ctl, msf, start_sector, start_offset, footer):
        block = bytearray(track_size)
        m, s, f = msf
        struct.pack_into("<BBBBB", block, 0x00, 0xAA if adr_ctl == 0x14 else 0xA9, 0, adr_ctl, 0, track_no)
        struct.pack_into("<BBB", block, 0x09, m, s, f)
        struct.pack_into("<H", block, 0x10, SECTOR_SIZE if track_no <= 99 else 0)
        struct.pack_into("<IQB", block, 0x24, start_sector, start_offset, 1)
        struct.pack_into("<I", block, 0x34, footer)
        return bytes(block)

    tracks = (
        track(0xA2, 0x14, _msf(total_frames + 150), 0, 0, 0)  # lead-out
        + track(1, 0x14, _msf(150), 0, 0, footer_offset)
        + track(2, 0x10, _msf(track2_lba + 150), track2_lba, track2_lba * SECTOR_SIZE, footer_offset)
    )
    footer = struct.pack("<IIII", filename_offset, 0, 0, 0)
    return bytes(header) + session + tracks + footer + b"*.mdf\x00"
