#pragma once

#include <ymir/gpu/d3d12/d3d12_device.hpp>
#include <ymir/gpu/d3d12/d3d12_resource.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/dev_log.hpp>
#include <ymir/util/result.hpp>

#include <ymir/core/types.hpp>

#include <fmt/format.h>

#include <cassert>
#include <deque>
#include <string>
#include <string_view>

namespace ymir::vdp {

/// @brief A single allocation in an upload buffer.
struct UploadAllocation {
    size_t offset; ///< Offset (in bytes) into the upload buffer
    void *data;    ///< Mapped CPU pointer for writing
    size_t size;   ///< Requested size
};

/// @brief Chunk of data allocated for a frame in an upload buffer.
struct UploadFrameChunk {
    size_t endOffset;  ///< One past last byte used
    UINT64 fenceValue; ///< Fence value of the frame that owns this chunk
};

/// @brief Manages per-frame allocations in an upload ring buffer.
class UploadRingBuffer {
public:
    ~UploadRingBuffer() {
        if (m_buffer) {
            m_buffer->Unmap(0, nullptr);
        }
    }

    /// @brief Creates the upload ring buffer.
    /// @param[in] device the device that will own the buffer
    /// @param[in] size the size (in bytes) of the upload buffer
    /// @return nothing on success, an error message otherwise
    util::VoidResult<> Create(gpu::d3d12::D3D12Device &device, size_t size) {
        auto builder = m_buffer.BufferBuilder(size);
        builder.HeapType(D3D12_HEAP_TYPE_UPLOAD);
        builder.InitialState(D3D12_RESOURCE_STATE_GENERIC_READ);
        if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
            return util::ErrorMessage{fmt::format("Could not create upload buffer, error code {:X}", (uint32)hr)};
        }
        if (HRESULT hr = m_buffer->Map(0, nullptr, reinterpret_cast<void **>(&m_basePtr)); FAILED(hr)) {
            return util::ErrorMessage{fmt::format("Could not map upload buffer, error code {:X}", (uint32)hr)};
        }
        m_size = size;
        m_head = 0;
        m_tail = 0;
        m_chunks.clear();
        return {};
    }

    /// @brief Retrieves a reference to the buffer resource.
    /// @return the buffer resource
    gpu::d3d12::D3D12Resource &GetBufferResource() {
        return m_buffer;
    }

    /// @brief Retrieves the allocated upload buffer size.
    /// @return the allocated buffer size
    size_t GetSize() const {
        return m_size;
    }

    /// @brief Sets a debug name for this buffer.
    /// @param[in] name the new debug name
    void SetDebugName(std::string_view name) {
        m_debugName = name;
    }

    /// @brief Retrieves the debug name for this buffer.
    /// @return this upload buffer's debug name
    std::string_view GetDebugName() const {
        return m_debugName;
    }

    /// @brief Attempts to allocate a chunk of memory from the upload buffer.
    /// @param[in] size the requested size
    /// @param[in] alignment the requested alignment
    /// @param[in] completedFenceValue the latest completed fence value, for reclaiming chunks from completed frames
    /// @param[out] outAlloc receives the allocation information
    /// @return `true` if allocation succeeded, `false` if there's no more room for allocations
    bool Allocate(size_t size, size_t alignment, UINT64 completedFenceValue, UploadAllocation &outAlloc) {
        ReclaimCompletedChunks(completedFenceValue);

        // Check if there's enough contiguous space of the requested size starting from the aligned head position.
        // The head may be readjusted to the beginning of the upload buffer if there is not enough room at the end of
        // the buffer.
        size_t alignedHead = Align(m_head, alignment);
        if (!HasContiguousSpace(alignedHead, size, m_tail, &alignedHead)) {
            return false;
        }

        // Successfully allocated a chunk
        outAlloc.offset = alignedHead;
        outAlloc.data = static_cast<void *>(m_basePtr + alignedHead);
        outAlloc.size = size;
        devlog::trace<grp::dx12_upload>("[{}] Allocated {:X}..{:X}, fence {} / {}", m_debugName, outAlloc.offset,
                                        outAlloc.offset + outAlloc.size, completedFenceValue,
                                        m_lastSubmittedFenceValue);

        // Update head position; wrap back to zero if needed
        m_head = alignedHead + size;
        if (m_head >= m_size) {
            m_head = 0;
        }

        return true;
    }

    /// @brief Finds the fence value to wait for which will have enough space for the requested allocation.
    /// @param[in] size the requested size
    /// @param[in] alignment the requested alignment
    /// @return the minimum fence number to wait for which frees up enough space for the requested allocation
    UINT64 FindFenceValueForAllocation(size_t size, size_t alignment) {
        // Common early bail-outs:
        // - there's already enough room for the buffer, so there's no need to wait
        // - the chunk list is empty
        const size_t alignedHead = Align(m_head, alignment);
        if (HasContiguousSpace(alignedHead, size, m_tail)) {
            return m_lastCompletedFenceValue;
        }
        if (m_chunks.empty()) {
            return m_lastSubmittedFenceValue;
        }

        size_t queuePos = 0;
        size_t tail = m_tail;
        UINT64 fenceValue = m_lastCompletedFenceValue;
        do {
            if (HasContiguousSpace(alignedHead, size, tail)) {
                return fenceValue;
            }
            tail = m_chunks[queuePos].endOffset;
            fenceValue = m_chunks[queuePos].fenceValue;
            ++queuePos;
        } while (queuePos < m_chunks.size());
        return m_lastSubmittedFenceValue;
    }

    /// @brief Records the end of a frame.
    /// @param[in] fenceValue the frame's fence value
    void EndFrame(UINT64 fenceValue) {
        UploadFrameChunk &chunk = m_chunks.emplace_back();
        chunk.endOffset = m_head;
        chunk.fenceValue = fenceValue;
        devlog::trace<grp::dx12_upload>("[{}] Frame ended, fence {}", m_debugName, fenceValue);
        m_lastSubmittedFenceValue = std::max(m_lastSubmittedFenceValue, fenceValue);
    }

private:
    gpu::d3d12::D3D12Resource m_buffer;
    uint8 *m_basePtr = nullptr;
    size_t m_size = 0;
    size_t m_head = 0;
    size_t m_tail = 0;
    std::deque<UploadFrameChunk> m_chunks;
    UINT64 m_lastSubmittedFenceValue = 0;
    UINT64 m_lastCompletedFenceValue = 0;
    std::string m_debugName;

    /// @brief Reclaims allocated chunks from previously completed frames.
    /// @param[in] fenceValue the latest completed fence value
    void ReclaimCompletedChunks(UINT64 fenceValue) {
        while (!m_chunks.empty() && m_chunks.front().fenceValue <= fenceValue) {
            m_tail = m_chunks.front().endOffset;
            devlog::trace<grp::dx12_upload>("[{}] Reclaimed fence {}, tail={:X}", m_debugName,
                                            m_chunks.front().fenceValue, m_tail);
            m_chunks.pop_front();
        }
        m_lastCompletedFenceValue = std::max(m_lastCompletedFenceValue, fenceValue);
    }

    /// @brief Checks if there's enough free contiguous space from a starting point.
    /// @param[in] start the starting offset
    /// @param[in] size the requested allocation size
    /// @param[in] tail the allocation tail
    /// @param[out] outStart if specified, receives the updated start offset, either the provided start offset or zero
    /// @return `true` if the buffer has enough space in the specified area, `false` if not
    bool HasContiguousSpace(size_t start, size_t size, size_t tail, size_t *outStart = nullptr) const {
        if (outStart != nullptr) {
            *outStart = start;
        }
        if (tail <= start) {
            // Free region is [start, m_size) and [0, tail)
            size_t beforeWrap = m_size - start;
            if (size <= beforeWrap) {
                return true;
            }
            if (outStart != nullptr) {
                *outStart = 0;
            }
            return size <= tail;
        } else {
            // Free region is [start, tail)
            return (start + size) <= tail;
        }
    }

    /// @brief Aligns the value up to the specified alignment.
    /// @param[in] value the value to align
    /// @param[in] alignment the desired alignment, which must be a power of two.
    /// @return the value, aligned up to the specified alignment
    size_t Align(size_t value, size_t alignment) {
        assert(bit::is_power_of_two(alignment));
        return (value + alignment - 1) & ~(alignment - 1);
    }
};

} // namespace ymir::vdp
