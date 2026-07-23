"""
Smoke tests for Ymir libretro core.

These tests verify basic functionality:
- Core loads and initializes
- ROM loading works
- Emulation runs without crashing
- Video output is generated
"""

from helpers import session, CORE_PATH


class TestCoreBasics:
    """Test core loading and basic info."""

    def test_core_exists(self):
        """Core dylib exists."""
        assert CORE_PATH.exists(), f"Core not found at {CORE_PATH}"

    def test_core_loads(self):
        """Core loads without error."""
        with session() as s:
            assert s is not None

    def test_api_version(self):
        """Core returns valid API version."""
        with session() as s:
            version = s.core.api_version()
            assert version == 1, f"Expected API version 1, got {version}"

    def test_system_info(self):
        """Core returns valid system info."""
        with session() as s:
            info = s.core.get_system_info()
            name = info.library_name
            if isinstance(name, bytes):
                name = name.decode("utf-8")
            assert "mir" in name.lower(), f"Expected 'Ymir' or 'Emir' in library_name, got {name!r}"

            exts = info.valid_extensions
            if isinstance(exts, bytes):
                exts = exts.decode("utf-8")
            for ext in ("chd", "cue"):
                assert ext in exts, f"Expected '{ext}' in valid_extensions: {exts}"


class TestRomLoading:
    """Test ROM loading functionality."""

    def test_load_chd(self):
        """Load a CHD disc image."""
        with session() as s:
            assert s is not None

    def test_av_info_after_load(self):
        """AV info is valid after loading."""
        with session() as s:
            av = s.core.get_system_av_info()
            # Saturn base resolution is typically 320x224 or 352x224 (NTSC)
            assert 256 <= av.geometry.base_width <= 704
            assert 224 <= av.geometry.base_height <= 480
            assert av.geometry.max_width >= av.geometry.base_width
            assert av.geometry.max_height >= av.geometry.base_height
            # NTSC ~59.82 fps, PAL ~50 fps
            assert 49.0 <= av.timing.fps <= 61.0
            assert av.timing.sample_rate == 44100


class TestEmulation:
    """Test emulation execution."""

    def test_run_frames(self):
        """Run multiple frames without crashing."""
        with session() as s:
            for _ in range(60):
                s.run()

    def test_video_output(self):
        """Video frames are generated."""
        with session() as s:
            for _ in range(10):
                s.run()
            frame = s.video.screenshot()
            assert frame is not None, "No video frame captured"

    def test_frame_not_all_black(self):
        """Video frame has non-black pixels after boot."""
        with session() as s:
            # Run past BIOS boot screen
            for _ in range(180):
                s.run()
            frame = s.video.screenshot()
            assert frame is not None
            frame_bytes = bytes(frame.data) if hasattr(frame, "data") else frame.tobytes()
            has_content = any(b != 0 for b in frame_bytes)
            assert has_content, "Frame appears to be all black"

    def test_reset(self):
        """Reset doesn't crash."""
        with session() as s:
            for _ in range(30):
                s.run()
            s.core.reset()
            for _ in range(30):
                s.run()
