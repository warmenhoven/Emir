"""
Lifecycle tests for Ymir libretro core.

These tests verify:
- Load/unload cycles work correctly
- Multiple resets don't crash
- Extended runtime is stable
- Resource cleanup works
"""

from helpers import session, require_rom, require_bios, CORE_PATH
from libretro import ExplicitPathDriver, Session
from libretro.api import retro_device_power, PowerState
from libretro.drivers import (
    ArrayAudioDriver,
    ConstantPowerDriver,
    DefaultPerfDriver,
    DefaultTimingDriver,
    DefaultUserDriver,
    DictLedDriver,
    DictOptionDriver,
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
from conftest import SYSTEM_DIR
from tempfile import TemporaryDirectory


class TestLifecycle:
    """Test core lifecycle operations."""

    def test_load_unload_cycle(self):
        """Load ROM, run, unload, repeat without crash."""
        require_bios()
        rom = require_rom()
        for cycle in range(3):
            with TemporaryDirectory() as save_dir:
                path_driver = ExplicitPathDriver(
                    corepath=str(CORE_PATH),
                    system=str(SYSTEM_DIR),
                    save=save_dir,
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
                    options=DictOptionDriver(),
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
                    for _ in range(30):
                        s.run()

    def test_multiple_resets(self):
        """Reset multiple times in a row without crash."""
        with session() as s:
            for _ in range(30):
                s.run()
            for i in range(10):
                s.core.reset()
                for _ in range(10):
                    s.run()

    def test_reset_immediately_after_load(self):
        """Reset immediately after loading doesn't crash."""
        with session() as s:
            s.core.reset()
            for _ in range(30):
                s.run()

    def test_run_extended(self):
        """Run 600 frames (~10 seconds) without crash."""
        with session() as s:
            for _ in range(600):
                s.run()
            frame = s.video.screenshot()
            assert frame is not None, "No video after extended run"

    def test_run_very_extended(self):
        """Run 3600 frames (~1 minute) for stability."""
        with session() as s:
            for _ in range(3600):
                s.run()
            frame = s.video.screenshot()
            assert frame is not None, "No video after very extended run"

    def test_rapid_reset_cycle(self):
        """Rapid reset/run cycles don't leak or crash."""
        with session() as s:
            for _ in range(50):
                s.core.reset()
                s.run()
                s.run()


class TestResourceManagement:
    """Test resource allocation and cleanup."""

    def test_repeated_sessions(self):
        """Multiple sequential sessions don't leak resources."""
        require_bios()
        rom = require_rom()
        for _ in range(5):
            with TemporaryDirectory() as save_dir:
                path_driver = ExplicitPathDriver(
                    corepath=str(CORE_PATH),
                    system=str(SYSTEM_DIR),
                    save=save_dir,
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
                    options=DictOptionDriver(),
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
                    for _ in range(10):
                        s.run()

    def test_av_info_stable_across_frames(self):
        """AV info remains consistent during emulation."""
        with session() as s:
            av1 = s.core.get_system_av_info()
            for _ in range(100):
                s.run()
            av2 = s.core.get_system_av_info()
            assert av1.timing.fps == av2.timing.fps
            assert av1.timing.sample_rate == av2.timing.sample_rate
