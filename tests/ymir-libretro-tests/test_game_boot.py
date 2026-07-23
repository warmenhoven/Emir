"""
Game boot regression tests.

These tests verify that specific games successfully boot past the BIOS
and produce non-black video output. Each test corresponds to a reported
upstream issue where a game fails to boot.

Tests are marked xfail when the bug is still present — they document
known failures and will start passing once the underlying issue is fixed.
"""

import pytest
from helpers import session


def _frame_is_black(s):
    """Check if the current video frame is entirely black (RGB channels)."""
    frame = s.video.screenshot()
    if frame is None:
        return True
    data = bytes(frame.data) if hasattr(frame, "data") else frame.tobytes()
    # RGBX8888: every 4 bytes is [R, G, B, X] — skip the X padding byte
    for i in range(0, len(data), 4):
        if data[i] or data[i + 1] or data[i + 2]:
            return False
    return True


def _run_frames(s, n):
    """Run n emulation frames."""
    for _ in range(n):
        s.run()


class TestGameBoot:
    """Tests for games that should boot but currently don't."""

    @pytest.mark.xfail(reason="upstream #522: game goes black after BIOS")
    def test_madden_nfl_97_boots(self):
        """Madden NFL 97 (USA) should display the EA logo after BIOS boot.

        Upstream: https://github.com/StrikerX3/Ymir/issues/522

        The BIOS boot sequence completes normally (Sega logo is displayed),
        but the game never takes over — the screen goes and stays black.
        The core continues producing frames at the correct rate, so it's
        not a hang, but the game code doesn't appear to be running.
        """
        with session("Madden NFL 97 (USA).chd",
                      options={"ymir_cdblock_lle": "enabled"}) as s:
            # Run past BIOS boot (Sega logo disappears around frame 800)
            _run_frames(s, 900)

            # The game should be doing something by now — at minimum an
            # EA logo, loading screen, or title sequence.  Sample several
            # points over the next 10 seconds to catch any late startup.
            for check in range(6):
                _run_frames(s, 100)
                if not _frame_is_black(s):
                    return  # Success — game is showing something

            pytest.fail("Game stuck on black screen after BIOS boot")
