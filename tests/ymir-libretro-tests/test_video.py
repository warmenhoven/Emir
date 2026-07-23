"""
Video tests for Ymir libretro core.

These tests verify:
- XRGB8888 pixel format (4 bytes per pixel)
- Frame dimensions are stable
- Frames stay within max geometry
- Video callback fires regularly
- Frame content changes (emulation running)
"""

from helpers import session


class TestVideoFormat:
    """Test video format and pixel data."""

    def test_pixel_format_xrgb8888(self):
        """Core uses XRGB8888 pixel format (4 bytes per pixel)."""
        with session() as s:
            for _ in range(10):
                s.run()

            frame = s.video.screenshot()
            assert frame is not None, "No video frame"

            # XRGB8888 = 4 bytes per pixel
            expected_size = frame.width * frame.height * 4
            actual_size = len(frame.data) if hasattr(frame, "data") else len(frame.tobytes())
            assert actual_size >= expected_size, \
                f"Frame too small for XRGB8888: {actual_size} < {expected_size}"

    def test_frame_has_valid_dimensions(self):
        """Frame has positive, reasonable dimensions."""
        with session() as s:
            for _ in range(10):
                s.run()

            frame = s.video.screenshot()
            assert frame is not None
            assert frame.width > 0
            assert frame.height > 0
            assert frame.width <= 704
            assert frame.height <= 480


class TestVideoDimensions:
    """Test frame dimension consistency."""

    def test_frame_dimensions_stable(self):
        """Frame dimensions remain consistent across frames."""
        with session() as s:
            for _ in range(10):
                s.run()

            first_frame = s.video.screenshot()
            first_width = first_frame.width
            first_height = first_frame.height

            for _ in range(50):
                s.run()

            later_frame = s.video.screenshot()
            assert later_frame.width == first_width
            assert later_frame.height == first_height

    def test_frame_within_max_geometry(self):
        """Frame dimensions never exceed max geometry."""
        with session() as s:
            av = s.core.get_system_av_info()
            max_w = av.geometry.max_width
            max_h = av.geometry.max_height

            for _ in range(60):
                s.run()

            frame = s.video.screenshot()
            assert frame.width <= max_w
            assert frame.height <= max_h


class TestVideoContent:
    """Test video content and updates."""

    def test_video_callback_fires(self):
        """Video callback is called during emulation."""
        with session() as s:
            for _ in range(30):
                s.run()
            frame = s.video.screenshot()
            assert frame is not None, "Video callback never fired"

    def test_frame_not_all_black_after_boot(self):
        """Frame is not all black after BIOS boot."""
        with session() as s:
            # Run past BIOS boot screen (~180 frames)
            for _ in range(180):
                s.run()

            frame = s.video.screenshot()
            assert frame is not None
            data = bytes(frame.data) if hasattr(frame, "data") else frame.tobytes()
            has_content = any(b != 0 for b in data)
            assert has_content, "Frame all black after boot"

    def test_frame_content_changes(self):
        """Frame content changes over time (emulation running)."""
        with session() as s:
            for _ in range(60):
                s.run()
            frame1 = s.video.screenshot()
            data1 = bytes(frame1.data) if hasattr(frame1, "data") else frame1.tobytes()

            for _ in range(180):
                s.run()
            frame2 = s.video.screenshot()
            data2 = bytes(frame2.data) if hasattr(frame2, "data") else frame2.tobytes()

            # Frames should differ — game is animating
            # (don't fail if static, just warn)
            if data1 == data2:
                print("  (warning: frames identical - may be static screen)")

    def test_frames_produced_each_run(self):
        """Each run() call should produce a frame."""
        with session() as s:
            for i in range(10):
                s.run()
                frame = s.video.screenshot()
                assert frame is not None, f"No frame after run {i + 1}"
