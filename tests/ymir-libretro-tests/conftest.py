"""
Pytest configuration and shared fixtures for Ymir libretro core tests.

Paths are configurable via environment variables:
    YMIR_CORE_PATH  - path to ymir_libretro.dylib/.so/.dll
    YMIR_ROM_DIR    - directory containing Saturn disc images
    YMIR_SYSTEM_DIR - directory containing Saturn BIOS files
"""

import os
import platform
import shutil
import pytest
from contextlib import contextmanager
from pathlib import Path
from tempfile import TemporaryDirectory

from libretro import ExplicitPathDriver, DictOptionDriver, Session
from libretro.api import retro_device_power, PowerState
from libretro.drivers import (
    ArrayAudioDriver,
    ConstantPowerDriver,
    DefaultPerfDriver,
    DefaultTimingDriver,
    DefaultUserDriver,
    DictLedDriver,
    DictRumbleDriver,
    GeneratorMicrophoneDriver,
    GeneratorMidiDriver,
    IterableInputDriver,
    IterableSensorDriver,
    LoggerMessageDriver,
    MultiVideoDriver,
    StandardContentDriver,
    UnformattedLogDriver,
)


# ---------------------------------------------------------------------------
# Path configuration
# ---------------------------------------------------------------------------

TEST_DIR = Path(__file__).parent
REPO_DIR = TEST_DIR.parent.parent

# Core library path
_ext = {"Darwin": "dylib", "Linux": "so", "Windows": "dll"}.get(platform.system(), "so")
_default_core = REPO_DIR / f"build/osx-arm64/apps/ymir-libretro/ymir_libretro.{_ext}"
CORE_PATH = Path(os.environ.get("YMIR_CORE_PATH", str(_default_core)))

# ROM directory
_default_rom_dir = Path.home() / "Documents" / "RetroArch" / "roms" / "Sega - Saturn"
ROM_DIR = Path(os.environ.get("YMIR_ROM_DIR", str(_default_rom_dir)))

# System directory (BIOS files)
_default_system_dir = Path.home() / "Documents" / "RetroArch" / "system"
SYSTEM_DIR = Path(os.environ.get("YMIR_SYSTEM_DIR", str(_default_system_dir)))

# Default test ROM — smallest game, and the one that triggered the devlog crash
DEFAULT_ROM = "Guardian Force (Japan).chd"

# BIOS filenames the core searches for (in priority order)
BIOS_FILES = ["sega_101.bin", "mpr-17933.bin", "saturn_bios.bin"]


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def find_rom(pattern, rom_dir=None):
    """Find ROM matching glob pattern."""
    search_dir = rom_dir or ROM_DIR
    if not search_dir.exists():
        return None
    matches = list(search_dir.glob(pattern))
    return matches[0] if matches else None


def require_rom(pattern=None, rom_dir=None):
    """Find ROM or skip test if not found."""
    if pattern is None:
        pattern = DEFAULT_ROM
    rom = find_rom(pattern, rom_dir)
    if not rom:
        pytest.skip(f"ROM not found: {pattern}")
    return rom


def has_bios():
    """Check if any BIOS file exists in the system directory."""
    return any((SYSTEM_DIR / b).exists() for b in BIOS_FILES)


def require_bios():
    """Skip test if no BIOS file is available."""
    if not has_bios():
        pytest.skip(f"No Saturn BIOS in {SYSTEM_DIR}")


@contextmanager
def session(rom_pattern=None, rom_dir=None, options=None):
    """Create test session with ROM and BIOS access.

    Args:
        rom_pattern: Glob pattern for ROM file.
        rom_dir: Directory to search for ROMs.
        options: Dict of core option overrides (e.g. {"ymir_cdblock_lle": "enabled"}).
    """
    require_bios()
    rom = require_rom(rom_pattern, rom_dir)
    with TemporaryDirectory() as save_dir:
        # Seed SMPC persistent data (RTC, region) so the BIOS doesn't
        # stop on the "Set Clock" screen waiting for user input.
        smpc_src = TEST_DIR / "smpc.bin"
        if smpc_src.exists():
            shutil.copy2(smpc_src, Path(save_dir) / "smpc.bin")
        path_driver = ExplicitPathDriver(
            corepath=str(CORE_PATH),
            system=str(SYSTEM_DIR),
            save=save_dir,
        )
        option_driver = (
            DictOptionDriver(variables=options) if options
            else DictOptionDriver()
        )
        with Session(
            core=str(CORE_PATH),
            game=str(rom),
            audio=ArrayAudioDriver(),
            input=IterableInputDriver(),
            video=MultiVideoDriver(),
            content=StandardContentDriver(),
            overscan=False,
            message=LoggerMessageDriver(),
            options=option_driver,
            path=path_driver,
            rumble=DictRumbleDriver(),
            sensor=IterableSensorDriver(),
            log=UnformattedLogDriver(),
            perf=DefaultPerfDriver(),
            user=DefaultUserDriver(),
            led=DictLedDriver(),
            midi=GeneratorMidiDriver(),
            timing=DefaultTimingDriver(),
            mic=GeneratorMicrophoneDriver(),
            device_power=ConstantPowerDriver(
                retro_device_power(PowerState.PLUGGED_IN, 0, 100)
            ),
        ) as s:
            yield s


@pytest.fixture
def ymir_session():
    """Pytest fixture providing a session with the default ROM."""
    with session() as s:
        yield s
