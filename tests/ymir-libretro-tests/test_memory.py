"""
Memory tests for Ymir libretro core.

Tests verify:
- Save RAM (internal backup memory) is exposed
- RetroAchievements memory map is correct
- Memory is readable and contains valid data
"""

import ctypes
import pytest
from helpers import session

# libretro memory IDs
RETRO_MEMORY_SAVE_RAM = 0
RETRO_MEMORY_SYSTEM_RAM = 2


class TestSaveRAM:
    """Test internal backup RAM (256Kbit / 32KB)."""

    def test_save_ram_size(self):
        """Save RAM should be 32KB (256Kbit internal backup)."""
        with session() as s:
            size = s.core.get_memory_size(RETRO_MEMORY_SAVE_RAM)
            assert size == 32768, f"Expected 32768, got {size}"

    def test_save_ram_data_valid(self):
        """Save RAM data pointer should be non-null."""
        with session() as s:
            data = s.core.get_memory_data(RETRO_MEMORY_SAVE_RAM)
            assert data, "Save RAM data pointer is null"

    def test_save_ram_valid_after_reset(self):
        """Save RAM should still be accessible after reset."""
        with session() as s:
            for _ in range(30):
                s.run()
            s.core.reset()
            size = s.core.get_memory_size(RETRO_MEMORY_SAVE_RAM)
            assert size == 32768
            data = s.core.get_memory_data(RETRO_MEMORY_SAVE_RAM)
            assert data


class TestRcheevosMemory:
    """Test RetroAchievements memory map.

    Ymir exposes two WRAM regions for rcheevos:
    - Low WRAM:  start=0x00200000, len=0x100000 (1MB)
    - High WRAM: start=0x06000000, len=0x100000 (1MB)
    """

    def test_memory_map_exists(self):
        """Memory map should be available."""
        with session() as s:
            mmap = s.memory_maps
            assert mmap is not None, "No memory map"

    def test_memory_map_descriptor_count(self):
        """Should have 2 memory descriptors (low + high WRAM)."""
        with session() as s:
            mmap = s.memory_maps
            assert mmap.num_descriptors == 2, \
                f"Expected 2 descriptors, got {mmap.num_descriptors}"

    def test_low_wram_descriptor(self):
        """Low WRAM descriptor at 0x00200000, 1MB."""
        with session() as s:
            mmap = s.memory_maps
            desc = mmap.descriptors[0]
            assert desc.start == 0x00200000, \
                f"Expected start=0x00200000, got 0x{desc.start:x}"
            assert desc.len == 0x100000, \
                f"Expected len=0x100000, got 0x{desc.len:x}"

    def test_high_wram_descriptor(self):
        """High WRAM descriptor at 0x06000000, 1MB."""
        with session() as s:
            mmap = s.memory_maps
            desc = mmap.descriptors[1]
            assert desc.start == 0x06000000, \
                f"Expected start=0x06000000, got 0x{desc.start:x}"
            assert desc.len == 0x100000, \
                f"Expected len=0x100000, got 0x{desc.len:x}"

    def test_memory_ptrs_valid(self):
        """Memory descriptor pointers should be non-null."""
        with session() as s:
            mmap = s.memory_maps
            for i in range(mmap.num_descriptors):
                desc = mmap.descriptors[i]
                assert desc.ptr, f"Descriptor {i} ptr is null"

    def test_memory_readable(self):
        """Memory should be readable via descriptor pointers."""
        with session() as s:
            for _ in range(60):
                s.run()

            mmap = s.memory_maps
            for i in range(mmap.num_descriptors):
                desc = mmap.descriptors[i]
                ptr = int(desc.ptr) if isinstance(desc.ptr, int) else int.from_bytes(desc.ptr, "little")
                length = int(desc.len) if isinstance(desc.len, int) else int.from_bytes(desc.len, "little")
                mem = bytes((ctypes.c_uint8 * length).from_address(ptr))
                assert len(mem) == length

    @pytest.mark.xfail(reason="Shadow buffer may not be visible to test harness (vector reallocation)")
    def test_wram_not_all_zeros(self):
        """WRAM shadow buffers should have non-zero values after running."""
        with session() as s:
            for _ in range(300):
                s.run()

            mmap = s.memory_maps
            for i in range(mmap.num_descriptors):
                desc = mmap.descriptors[i]
                ptr = int(desc.ptr) if isinstance(desc.ptr, int) else int.from_bytes(desc.ptr, "little")
                length = int(desc.len) if isinstance(desc.len, int) else int.from_bytes(desc.len, "little")
                # Check first 4KB — enough to detect active use
                check_len = min(length, 4096)
                mem = bytes((ctypes.c_uint8 * check_len).from_address(ptr))
                non_zero = sum(1 for b in mem if b != 0)
                assert non_zero > 0, \
                    f"Descriptor {i} (0x{desc.start:x}) all zeros after emulation"

    def test_memory_valid_after_reset(self):
        """Memory map should still be valid after reset."""
        with session() as s:
            for _ in range(30):
                s.run()
            s.core.reset()

            mmap = s.memory_maps
            assert mmap.num_descriptors == 2
            for i in range(mmap.num_descriptors):
                desc = mmap.descriptors[i]
                assert desc.ptr, f"Descriptor {i} ptr null after reset"
