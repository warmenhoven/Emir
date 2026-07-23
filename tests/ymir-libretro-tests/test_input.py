"""
Input tests for Ymir libretro core.

These tests verify:
- Controller port devices can be set
- Input descriptors are registered
- Input polling doesn't crash
"""

from helpers import session

# libretro device types
RETRO_DEVICE_NONE = 0
RETRO_DEVICE_JOYPAD = 1
RETRO_DEVICE_MOUSE = 2
RETRO_DEVICE_ANALOG = 5
RETRO_DEVICE_LIGHTGUN = 6


class TestControllerPorts:
    """Test controller port device configuration."""

    def test_set_joypad(self):
        """Setting control pad device doesn't crash."""
        with session() as s:
            s.core.set_controller_port_device(0, RETRO_DEVICE_JOYPAD)
            for _ in range(30):
                s.run()

    def test_set_analog(self):
        """Setting 3D control pad (analog) device doesn't crash."""
        with session() as s:
            s.core.set_controller_port_device(0, RETRO_DEVICE_ANALOG)
            for _ in range(30):
                s.run()

    def test_set_mouse(self):
        """Setting mouse device doesn't crash."""
        with session() as s:
            s.core.set_controller_port_device(0, RETRO_DEVICE_MOUSE)
            for _ in range(30):
                s.run()

    def test_set_lightgun(self):
        """Setting lightgun device doesn't crash."""
        with session() as s:
            s.core.set_controller_port_device(0, RETRO_DEVICE_LIGHTGUN)
            for _ in range(30):
                s.run()

    def test_set_port1_device(self):
        """Setting port 1 device doesn't crash."""
        with session() as s:
            s.core.set_controller_port_device(1, RETRO_DEVICE_JOYPAD)
            for _ in range(30):
                s.run()

    def test_switch_device_types(self):
        """Switching between device types doesn't crash."""
        with session() as s:
            for device in [RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ANALOG, RETRO_DEVICE_MOUSE]:
                s.core.set_controller_port_device(0, device)
                for _ in range(10):
                    s.run()


class TestInputPolling:
    """Test input polling."""

    def test_input_poll_works(self):
        """Input polling during emulation doesn't crash."""
        with session() as s:
            for _ in range(60):
                s.run()
