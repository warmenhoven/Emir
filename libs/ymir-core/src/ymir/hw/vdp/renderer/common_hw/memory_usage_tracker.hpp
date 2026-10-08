#pragma once

#include <ymir/util/inline.hpp>

#include <ymir/core/types.hpp>

#include <algorithm>
#include <array>

namespace ymir::vdp {

/// @brief Tracks memory usage with a generation map.
/// @tparam memorySize total size of the memory area to track
/// @tparam blockSizeBits splits the memory into blocks of 2^blockSizeBits bytes
template <size_t memorySize, unsigned blockSizeBits = 5>
class MemoryUsageTracker {
    static constexpr uint32 kBlockSize = 1u << blockSizeBits;
    static constexpr uint32 kArraySize = memorySize >> blockSizeBits;

public:
    /// @brief Clears usage across the whole memory.
    FORCE_INLINE void Clear() {
        // Clear the whole map if we wrap around.
        if (++m_currGen == 0) {
            std::fill(m_usage.begin(), m_usage.end(), 0u);
            m_currGen = 1;
        }
    }

    /// @brief Marks the specified range of bytes as in use.
    /// @param[in] address the base address
    /// @param[in] size the length of the range
    FORCE_INLINE void MarkRange(uint32 address, uint32 size) {
        size = std::max(size, 1u);
        uint32 first = address >> blockSizeBits;
        uint32 last = (address + size - 1) >> blockSizeBits;
        for (uint32 i = first; i <= last; ++i) {
            m_usage[i] = m_currGen;
        }
    }

    /// @brief Checks if a particular address is marked as in use.
    /// @param[in] address the address to check
    /// @return `true` if the address is marked, `false` if not.
    FORCE_INLINE bool IsInUse(uint32 address) const {
        return m_usage[address >> blockSizeBits] == m_currGen;
    }

private:
    std::array<uint32, kArraySize> m_usage{};
    uint32 m_currGen = 1u;
};

} // namespace ymir::vdp
