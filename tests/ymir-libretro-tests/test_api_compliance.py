"""
API compliance tests for Ymir libretro core.

These tests verify the core follows the libretro API specification:
- Required functions exist and return valid values
- System info is properly formatted
- AV info contains valid timing and geometry
- Memory functions work correctly
- Serialization functions exist
"""

import pytest
from helpers import session, require_rom, require_bios, CORE_PATH
from libretro import defaults
from conftest import SYSTEM_DIR
from tempfile import TemporaryDirectory


class TestRequiredFunctions:
    """Test that all required libretro API functions work."""

    def test_retro_api_version(self):
        """retro_api_version returns RETRO_API_VERSION (1)."""
        with session() as s:
            assert s.core.api_version() == 1

    def test_retro_get_system_info(self):
        """retro_get_system_info returns valid info."""
        with session() as s:
            info = s.core.get_system_info()
            assert info.library_name, "library_name is empty"
            assert info.library_version, "library_version is empty"
            assert info.valid_extensions, "valid_extensions is empty"

    def test_retro_get_system_av_info(self):
        """retro_get_system_av_info returns valid AV info."""
        with session() as s:
            av = s.core.get_system_av_info()
            assert av.geometry.base_width > 0
            assert av.geometry.base_height > 0
            assert av.geometry.max_width >= av.geometry.base_width
            assert av.geometry.max_height >= av.geometry.base_height
            assert av.timing.fps > 0
            assert av.timing.sample_rate > 0

    def test_retro_init_deinit(self):
        """retro_init and retro_deinit don't crash."""
        with session() as s:
            pass

    def test_retro_load_unload_game(self):
        """retro_load_game and retro_unload_game work."""
        with session() as s:
            for _ in range(10):
                s.run()

    def test_retro_run(self):
        """retro_run executes without crash."""
        with session() as s:
            for _ in range(60):
                s.run()

    def test_retro_reset(self):
        """retro_reset works without crash."""
        with session() as s:
            for _ in range(30):
                s.run()
            s.core.reset()
            for _ in range(30):
                s.run()


class TestSystemInfo:
    """Test system info validity."""

    def test_library_name_format(self):
        """Library name is reasonable string."""
        with session() as s:
            info = s.core.get_system_info()
            name = info.library_name
            if isinstance(name, bytes):
                name = name.decode("utf-8")
            assert 0 < len(name) < 100
            assert any(c.isalnum() for c in name)

    def test_library_version_format(self):
        """Library version is reasonable string."""
        with session() as s:
            info = s.core.get_system_info()
            version = info.library_version
            if isinstance(version, bytes):
                version = version.decode("utf-8")
            assert 0 < len(version) < 50

    def test_valid_extensions(self):
        """Valid extensions list contains expected formats."""
        with session() as s:
            info = s.core.get_system_info()
            exts = info.valid_extensions
            if isinstance(exts, bytes):
                exts = exts.decode("utf-8")
            for ext in ("chd", "cue", "iso", "m3u"):
                assert ext in exts, f"'{ext}' not in valid_extensions: {exts}"


class TestAVInfo:
    """Test audio/video info validity."""

    def test_geometry_reasonable(self):
        """Geometry values are in reasonable ranges."""
        with session() as s:
            av = s.core.get_system_av_info()
            g = av.geometry
            assert 256 <= g.base_width <= 704
            assert 200 <= g.base_height <= 480
            assert g.max_width <= 1024
            assert g.max_height <= 1024

    def test_fps_reasonable(self):
        """Frame rate is NTSC or PAL range."""
        with session() as s:
            av = s.core.get_system_av_info()
            assert 49.0 <= av.timing.fps <= 61.0

    def test_sample_rate(self):
        """Audio sample rate is 44100 Hz."""
        with session() as s:
            av = s.core.get_system_av_info()
            assert av.timing.sample_rate == 44100

    def test_aspect_ratio_reasonable(self):
        """Aspect ratio is reasonable (if provided)."""
        with session() as s:
            av = s.core.get_system_av_info()
            aspect = av.geometry.aspect_ratio
            if aspect > 0:
                assert 0.5 <= aspect <= 3.0


class TestSerializationFunctions:
    """Test save state (serialization) API functions."""

    def test_serialize_size(self):
        """retro_serialize_size returns nonzero."""
        with session() as s:
            for _ in range(30):
                s.run()
            size = s.core.serialize_size()
            assert size > 0, "serialize_size returned 0"

    def test_serialize_size_reasonable(self):
        """Serialize size is in reasonable range (< 16MB)."""
        with session() as s:
            size = s.core.serialize_size()
            assert 0 < size < 16 * 1024 * 1024

    def test_serialize_unserialize_cycle(self):
        """Serialize and unserialize round-trip works."""
        with session() as s:
            for _ in range(60):
                s.run()

            size = s.core.serialize_size()
            state = bytearray(size)
            result = s.core.serialize(state)
            assert result, "Serialization failed"

            for _ in range(60):
                s.run()

            result = s.core.unserialize(state)
            assert result, "Unserialization failed"

            # Should still be able to run after load state
            for _ in range(30):
                s.run()

    def test_save_state_not_empty(self):
        """Save state data is not all zeros."""
        with session() as s:
            for _ in range(60):
                s.run()

            size = s.core.serialize_size()
            state = bytearray(size)
            s.core.serialize(state)
            assert any(state), "Save state is all zeros"

    def test_save_state_restores_correctly(self):
        """Save state restores emulation to the saved point.

        Saves state, runs forward until the video frame diverges, restores,
        then verifies the restored frame is closer to the save point than
        the diverged frame was.
        """
        with session() as s:
            # Run well past BIOS boot into game (~10 seconds)
            for _ in range(600):
                s.run()

            # Capture frame and save state
            frame_save = s.video.screenshot()
            data_save = bytes(frame_save.data) if hasattr(frame_save, "data") \
                else frame_save.tobytes()

            size = s.core.serialize_size()
            state = bytearray(size)
            result = s.core.serialize(state)
            assert result, "Serialization failed"

            # Run forward until video diverges (up to 1800 frames / ~30s)
            data_diverged = data_save
            for _ in range(1800):
                s.run()
                frame = s.video.screenshot()
                data_diverged = bytes(frame.data) if hasattr(frame, "data") \
                    else frame.tobytes()
                if data_diverged != data_save:
                    break
            else:
                pytest.skip("Video did not change after 1800 frames")

            # Restore state
            result = s.core.unserialize(state)
            assert result, "Unserialization failed"

            # Run one frame to regenerate video
            s.run()
            frame_restored = s.video.screenshot()
            data_restored = bytes(frame_restored.data) if hasattr(frame_restored, "data") \
                else frame_restored.tobytes()

            def byte_diff(a, b):
                return sum(1 for x, y in zip(a, b) if x != y)

            diff_diverged = byte_diff(data_save, data_diverged)
            diff_restored = byte_diff(data_save, data_restored)

            assert diff_restored < diff_diverged, \
                f"Restored frame not closer to save point " \
                f"(restored diff={diff_restored}, diverged diff={diff_diverged})"


class TestRegion:
    """Test region detection."""

    def test_get_region(self):
        """retro_get_region returns NTSC (0) or PAL (1)."""
        with session() as s:
            region = s.core.get_region()
            assert region in (0, 1), f"Expected NTSC(0) or PAL(1), got {region}"
