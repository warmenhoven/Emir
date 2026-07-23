"""
Audio tests for Ymir libretro core.

These tests verify:
- Audio callback is registered
- Audio samples are generated
- Sample rate matches AV info (44100 Hz)
"""

from helpers import session


class TestAudio:
    """Test audio output functionality."""

    def test_audio_callback_set(self):
        """Audio callback is registered."""
        with session() as s:
            assert s.audio is not None, "Audio interface not available"

    def test_audio_samples_generated(self):
        """Audio samples are produced after running frames."""
        with session() as s:
            for _ in range(60):
                s.run()

            samples = s.audio.buffer
            assert samples is not None, "No audio buffer available"
            assert len(samples) > 0, "No audio samples generated"

    def test_audio_sample_rate(self):
        """Audio sample rate is 44100 Hz."""
        with session() as s:
            av = s.core.get_system_av_info()
            assert av.timing.sample_rate == 44100
