"""
Core options tests for Ymir libretro core.

These tests verify:
- Core options are registered correctly
- All expected options exist
- Options have valid definitions
"""

from helpers import session


# Expected core option keys
EXPECTED_OPTIONS = [
    b"ymir_region",
    b"ymir_sh2_cache",
    b"ymir_rtc_mode",
    b"ymir_cartridge",
    b"ymir_threaded_vdp1",
    b"ymir_threaded_vdp2",
    b"ymir_deinterlace",
    b"ymir_transparent_meshes",
    b"ymir_audio_interpolation",
    b"ymir_cd_speed",
    b"ymir_cdblock_lle",
]


class TestCoreOptionsRegistration:
    """Test that core options are properly registered."""

    def test_options_available(self):
        """Core registers options during init."""
        with session() as s:
            assert s.options is not None, "Options not available"

    def test_options_definitions_exist(self):
        """Core provides option definitions."""
        with session() as s:
            defs = s.options.definitions
            assert defs is not None
            assert len(defs) > 0, "No options defined"

    def test_all_expected_options_exist(self):
        """All expected options are registered."""
        with session() as s:
            defs = s.options.definitions
            for key in EXPECTED_OPTIONS:
                assert key in defs, \
                    f"Missing option: {key.decode()!r} (have: {[k.decode() for k in defs.keys()]})"

    def test_region_option(self):
        """Region option exists."""
        with session() as s:
            defs = s.options.definitions
            assert b"ymir_region" in defs

    def test_cartridge_option(self):
        """Cartridge option exists."""
        with session() as s:
            defs = s.options.definitions
            assert b"ymir_cartridge" in defs


class TestCoreOptionDefinitions:
    """Test that core option definitions are valid."""

    def test_options_have_keys(self):
        """All option definitions have keys."""
        with session() as s:
            defs = s.options.definitions
            for key, opt in defs.items():
                assert opt.key, f"Option {key} has no key"

    def test_option_count(self):
        """Expected number of options are registered."""
        with session() as s:
            defs = s.options.definitions
            # At least the options we know about
            assert len(defs) >= len(EXPECTED_OPTIONS), \
                f"Expected at least {len(EXPECTED_OPTIONS)} options, got {len(defs)}"


class TestCoreOptionBehavior:
    """Test that emulation works with options."""

    def test_emulation_stable_with_options(self):
        """Emulation runs stably with default options."""
        with session() as s:
            for _ in range(300):
                s.run()
            frame = s.video.screenshot()
            assert frame is not None
