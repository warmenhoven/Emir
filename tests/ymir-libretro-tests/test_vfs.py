"""
VFS tests: every file the core touches must go through the frontend VFS when one is
provided. RetroArch on Android hands out "saf://" content URIs that only its VFS can
open (https://github.com/warmenhoven/Emir/issues/2).

The tests run in a "VFS-only world": content, system and save directories live under
vfstest://, which doesn't exist on the host file system, so any access that bypasses
the VFS fails. Every test asserts a positive outcome, because some bypasses fail soft
(e.g. a CD block ROM that can't be found silently falls back to HLE).

Disc fixtures are generated from Guardian Force (data + audio track) and must all
produce the same screen after BOOT_FRAMES as the original CHD loaded natively.
"""

import shutil
from pathlib import Path
from tempfile import TemporaryDirectory

import pytest
from libretro.drivers import DefaultFileSystemDriver

from conftest import BIOS_FILES, SYSTEM_DIR, TEST_DIR, require_bios, require_rom
from vfs_support import (
    BOOT_FRAMES,
    CART_SIZE,
    CDB_ROM_SIZE,
    IPL_SIZE,
    KOF95_CART,
    SCRATCH_DIR,
    SMPC_SIZE,
    VfsOnlyDriver,
    build_disc_fixtures,
    build_system_dir,
    core_session,
    screen_hash,
)

DISC_SOURCE = "Guardian Force (Japan).chd"

DISC_FORMATS = [
    "chd", "cue", "cue_split", "cue_wave", "cue_mp3", "cue_ogg",
    "iso_2352", "iso_2048", "ccd", "mds", "m3u",
]


@pytest.fixture(scope="module")
def scratch():
    SCRATCH_DIR.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(dir=SCRATCH_DIR) as d:
        yield Path(d)


@pytest.fixture(scope="module")
def discs(scratch):
    """(roms dir, {format: (file name, companion file names)})"""
    require_bios()
    if shutil.which("chdman") is None:
        pytest.skip("chdman not installed")
    chd = require_rom(DISC_SOURCE)
    roms = scratch / "roms"
    return roms, build_disc_fixtures(chd, roms)


@pytest.fixture(scope="module")
def system_dir(scratch):
    require_bios()
    return build_system_dir(SYSTEM_DIR, scratch / "system")


@pytest.fixture
def save_dir(scratch):
    with TemporaryDirectory(dir=scratch) as d:
        yield Path(d)


@pytest.fixture(autouse=True)
def native_cwd(scratch, monkeypatch):
    """Runs each test from an empty directory and fails it if any file was written there.

    libretro.py creates the system/save directories it is given, so "vfstest://save" also exists natively as
    ./vfstest:/save; a write that bypasses the VFS would silently land there.
    """
    with TemporaryDirectory(dir=scratch) as d:
        monkeypatch.chdir(d)
        yield
        stray = [p for p in Path(d).rglob("*") if p.is_file()]
        assert not stray, f"files written to the host file system instead of the VFS: {stray}"


@pytest.fixture(scope="module")
def reference_hash(discs, scratch):
    """Screen after BOOT_FRAMES for the original CHD, loaded natively (no VFS)."""
    roms, formats = discs
    with TemporaryDirectory(dir=scratch) as save:
        seed_smpc(Path(save))
        with core_session(roms / formats["chd"][0], SYSTEM_DIR, save, vfs=None) as s:
            return screen_hash(s)


def seed_smpc(save_dir):
    """Seeds RTC/language data so the BIOS doesn't stop on the clock screen."""
    shutil.copyfile(TEST_DIR / "smpc.bin", save_dir / "smpc.bin")


def cdb_rom_or_skip():
    cdb = SYSTEM_DIR / "cdb"
    if not any(p.is_file() and p.stat().st_size == CDB_ROM_SIZE for p in cdb.glob("*")):
        pytest.skip(f"No CD block ROM in {cdb}")


def disc_or_skip(discs, fmt):
    roms, formats = discs
    if fmt not in formats:
        pytest.skip(f"{fmt} fixture needs ffmpeg")
    name, companions = formats[fmt]
    return roms, name, companions


class TestDiscFormatsNative:
    """No frontend VFS: validates the fixtures and the host file system path."""

    @pytest.mark.parametrize("fmt", DISC_FORMATS)
    def test_boots_like_chd(self, discs, reference_hash, save_dir, fmt):
        roms, name, _ = disc_or_skip(discs, fmt)
        seed_smpc(save_dir)
        with core_session(roms / name, SYSTEM_DIR, save_dir, vfs=None) as s:
            assert screen_hash(s) == reference_hash


class TestDiscFormatsViaVfs:
    """Disc images that only exist through the frontend VFS (issue #2)."""

    @pytest.mark.parametrize("fmt", DISC_FORMATS)
    def test_boots_like_chd(self, discs, reference_hash, save_dir, fmt):
        roms, name, companions = disc_or_skip(discs, fmt)
        seed_smpc(save_dir)
        vfs = VfsOnlyDriver({"roms": roms})
        with core_session(f"vfstest://roms/{name}", SYSTEM_DIR, save_dir, vfs) as s:
            assert screen_hash(s) == reference_hash
        for file in [name, *companions]:
            assert f"vfstest://roms/{file}" in vfs.opened, f"{file} was not opened through the VFS"


class TestSystemFilesViaVfs:
    """Content, system and save directories that only exist through the frontend VFS."""

    @pytest.fixture
    def vfs(self, discs, system_dir, save_dir):
        roms, _ = discs
        return VfsOnlyDriver({"roms": roms, "system": system_dir, "save": save_dir})

    @staticmethod
    def boot(vfs, options=None, frames=0):
        with core_session("vfstest://roms/gf.chd", "vfstest://system", "vfstest://save", vfs, options) as s:
            return screen_hash(s, frames) if frames else None

    def test_bios_read_via_vfs(self, reference_hash, save_dir, vfs):
        seed_smpc(save_dir)
        assert self.boot(vfs, frames=BOOT_FRAMES) == reference_hash
        bios_reads = {p: n for p, n in vfs.bytes_read.items() if p.removeprefix("vfstest://system/") in BIOS_FILES}
        assert IPL_SIZE in bios_reads.values(), f"no BIOS was read through the VFS: {bios_reads}"

    def test_cd_block_rom_read_via_vfs(self, save_dir, vfs):
        cdb_rom_or_skip()
        seed_smpc(save_dir)
        self.boot(vfs, options={"ymir_cdblock_lle": "enabled"})
        assert "vfstest://system/cdb" in vfs.listed
        cdb_reads = [n for p, n in vfs.bytes_read.items() if p.startswith("vfstest://system/cdb/")]
        assert CDB_ROM_SIZE in cdb_reads

    def test_rom_cartridge_read_via_vfs(self, save_dir, vfs):
        seed_smpc(save_dir)
        self.boot(vfs, options={"ymir_cartridge": "rom_kof95"})
        assert vfs.bytes_read.get(f"vfstest://system/{KOF95_CART}") == CART_SIZE

    def test_smpc_data_read_via_vfs(self, save_dir, vfs):
        seed_smpc(save_dir)
        self.boot(vfs)
        assert vfs.bytes_read.get("vfstest://save/smpc.bin") == SMPC_SIZE

    def test_smpc_data_written_via_vfs(self, save_dir, vfs):
        self.boot(vfs, frames=60)
        assert vfs.bytes_written.get("vfstest://save/smpc.bin") == SMPC_SIZE
        assert (save_dir / "smpc.bin").stat().st_size == SMPC_SIZE

    def test_cd_block_rom_found_among_decoys(self, discs, scratch, save_dir):
        """cdb/ may hold subdirectories and other files; only a 64 KiB ROM may be loaded."""
        cdb_rom_or_skip()
        roms, _ = discs
        system = build_system_dir(SYSTEM_DIR, scratch / "system-decoys")
        (system / "cdb" / "nested").mkdir(exist_ok=True)
        (system / "cdb" / "0-wrong-size.bin").write_bytes(bytes(CDB_ROM_SIZE + 1))
        seed_smpc(save_dir)
        vfs = VfsOnlyDriver({"roms": roms, "system": system, "save": save_dir})
        with core_session("vfstest://roms/gf.chd", "vfstest://system", "vfstest://save", vfs,
                          {"ymir_cdblock_lle": "enabled"}):
            pass
        cdb_reads = {p: n for p, n in vfs.bytes_read.items() if p.startswith("vfstest://system/cdb/")}
        assert CDB_ROM_SIZE in cdb_reads.values()
        assert "vfstest://system/cdb/0-wrong-size.bin" not in cdb_reads

    def test_missing_save_dir(self, discs, system_dir, scratch):
        """An unwritable save location must not break loading or unloading."""
        roms, _ = discs
        vfs = VfsOnlyDriver({"roms": roms, "system": system_dir, "save": scratch / "no-such-dir"})
        with core_session("vfstest://roms/gf.chd", "vfstest://system", "vfstest://save", vfs) as s:
            for _ in range(60):
                s.run()
        assert "vfstest://save/smpc.bin" not in vfs.bytes_written

    def test_vfs_survives_frontend_environment_probe(self, vfs):
        """RetroArch probes core info on the running core: it calls retro_set_environment with a probe callback, then
        with its real callback while ignoring every request (runloop.c, RUNLOOP_FLAG_IGNORE_ENVIRONMENT_CB). The failed
        GET_VFS_INTERFACE during that probe must not drop the frontend VFS."""
        with core_session("vfstest://roms/gf.chd", "vfstest://system", "vfstest://save", vfs) as s:
            s.run()
            ignoring = True

            def frontend_environment(cmd, data):
                return False if ignoring else s.environment(cmd, data)

            s.core.set_environment(lambda cmd, data: False)
            s.core.set_environment(frontend_environment)
            ignoring = False
            s.run()
        assert vfs.bytes_written.get("vfstest://save/smpc.bin") == SMPC_SIZE


class TestSystemFilesNative:
    """No frontend VFS: save data still goes to the host file system."""

    def test_smpc_data_written_natively(self, discs, save_dir):
        roms, formats = discs
        with core_session(roms / formats["chd"][0], SYSTEM_DIR, save_dir, vfs=None) as s:
            for _ in range(60):
                s.run()
        assert (save_dir / "smpc.bin").stat().st_size == SMPC_SIZE


# Issue #2's content path: the SAF tree root is percent-encoded, the rest are plain segments
SAF_TREE = "content:%2F%2Fcom.android.externalstorage.documents%2Ftree%2Fprimary%253AEmulador"


class TestContentPathsViaVfs:
    """Content path shapes real frontends hand out."""

    def test_saf_shaped_path(self, discs, reference_hash, scratch, save_dir):
        roms, _ = discs
        folder = scratch / "saf" / SAF_TREE / "Saturn (SEGA)"
        folder.mkdir(parents=True, exist_ok=True)
        for name in ("gf.cue", "gf.bin"):
            shutil.copyfile(roms / name, folder / name)
        seed_smpc(save_dir)
        vfs = VfsOnlyDriver({"saf": scratch / "saf"})
        game_dir = f"vfstest://saf/{SAF_TREE}/Saturn (SEGA)"
        with core_session(f"{game_dir}/gf.cue", SYSTEM_DIR, save_dir, vfs) as s:
            assert screen_hash(s) == reference_hash
        assert vfs.bytes_read.get(f"{game_dir}/gf.bin", 0) > 0

    def test_non_ascii_file_name(self, discs, reference_hash, save_dir):
        """Non-ASCII names round-trip through the UTF-8 VFS boundary. On POSIX that's byte pass-through; the Windows
        UTF-8 <-> UTF-16 conversion in vfs::ToPath/FromPath isn't exercised here."""
        roms, formats = discs
        name = "ガーディアンフォース (日本).chd"
        shutil.copyfile(roms / formats["chd"][0], roms / name)
        seed_smpc(save_dir)
        vfs = VfsOnlyDriver({"roms": roms})
        with core_session(f"vfstest://roms/{name}", SYSTEM_DIR, save_dir, vfs) as s:
            assert screen_hash(s) == reference_hash
        assert vfs.bytes_read.get(f"vfstest://roms/{name}", 0) > 0

    def test_provider_does_not_outlive_frontend_vfs(self, discs, save_dir):
        """After a VFS session, sessions without a v3 VFS must use the host file system, not the old VFS."""
        roms, _ = discs
        seed_smpc(save_dir)
        first = VfsOnlyDriver({"roms": roms})
        with core_session("vfstest://roms/gf.cue", SYSTEM_DIR, save_dir, first) as s:
            s.run()
        used = len(first.opened)
        with core_session(roms / "gf.cue", SYSTEM_DIR, save_dir, vfs=None) as s:
            s.run()
        with core_session(roms / "gf.cue", SYSTEM_DIR, save_dir, vfs=DefaultFileSystemDriver(2)) as s:
            s.run()
        assert len(first.opened) == used, f"later sessions went through the first session's VFS: {first.opened[used:]}"

    def test_unopenable_bin_fails_cleanly(self, scratch, save_dir):
        """A BIN that can be stat'ed but not opened must fail the load, not throw across the libretro C API."""
        folder = scratch / "unopenable"
        folder.mkdir(exist_ok=True)
        (folder / "gf.cue").write_text('FILE "gf.bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n')
        (folder / "gf.bin").write_bytes(bytes(16 * 2352))
        (folder / "gf.bin").chmod(0)
        try:
            vfs = VfsOnlyDriver({"roms": folder})
            with pytest.raises(RuntimeError, match="Failed to load game"):
                with core_session("vfstest://roms/gf.cue", SYSTEM_DIR, save_dir, vfs):
                    pass
        finally:
            (folder / "gf.bin").chmod(0o644)
