#pragma once

/**
@file
@brief Defines `DescriptorHeapAllocator`, an object that manages descriptor heap allocations in a `D3D12DescriptorHeap`.
*/

#include "d3d12_descriptor_heap.hpp"

#include <ymir/util/dev_assert.hpp>

#include <d3d12.h>

#include <cassert>
#include <list>

namespace ymir::gpu::d3d12 {

/// @brief Descriptor pointers, allocated in a heap.
struct DescriptorRange {
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
    UINT baseIndex = 0; // index of first descriptor in heap
    UINT count = 0;     // number of descriptors allocated in this range
    UINT descSize = 0;  // size of a descriptor

    void Reset() {
        cpuHandle = {};
        gpuHandle = {};
        baseIndex = 0;
        count = 0;
        descSize = 0;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(UINT offset) const {
        return D3D12_CPU_DESCRIPTOR_HANDLE{.ptr = cpuHandle.ptr + std::min(offset, count) * descSize};
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(UINT offset) const {
        return D3D12_GPU_DESCRIPTOR_HANDLE{.ptr = gpuHandle.ptr + std::min(offset, count) * descSize};
    }
};

/// @brief A descriptor allocator that can be bound to a `D3D12DescriptorHeap`.
class DescriptorHeapAllocator {
public:
    DescriptorHeapAllocator() = default;
    DescriptorHeapAllocator(const D3D12DescriptorHeap &heap) {
        Bind(heap);
    }

    /// @brief Binds this allocator to the given descriptor heap.
    /// @param[in] heap the heap to bind to
    void Bind(const D3D12DescriptorHeap &heap) {
        assert(m_heap == nullptr);
        assert(m_freeRanges.empty());

        m_heap = &heap;
        m_freeRanges.clear();
        m_freeRanges.push_back(FreeRange{.start = 0, .length = heap.GetDescriptorHeapSize()});
    }

    /// @brief Unbinds the allocator from the heap.
    void Unbind() {
        m_heap = nullptr;
        m_freeRanges.clear();
    }

    /// @brief Determines if this allocator is bound to a descriptor heap.
    /// @return `true` if bound, `false` if not
    bool IsBound() const {
        return m_heap != nullptr;
    }

    /// @brief Allocates a descriptor.
    /// @param[out] outDesc the output descriptor
    /// @param[in] count number of descriptors to allocate. 0 always succeeds but allocates nothing
    /// @return `true` if successfully allocated, `false` if there is no free space for the descriptor range
    bool Allocate(DescriptorRange &outDesc, UINT count = 1) {
        // Cannot allocate if not bound to a heap
        if (m_heap == nullptr) {
            return false;
        }

        // Allocating an empty range always succeeds
        if (count == 0) {
            return true;
        }

        // Find first range with enough room for the requested number of descriptors
        for (auto it = m_freeRanges.begin(); it != m_freeRanges.end(); ++it) {
            if (count <= it->length) {
                // Found a free range with enough space

                // Write descriptor info
                const UINT index = it->start;
                const SIZE_T ptrOffset = static_cast<SIZE_T>(index) * m_heap->GetDescriptorSize();
                outDesc.baseIndex = index;
                outDesc.count = count;
                outDesc.cpuHandle.ptr = m_heap->GetCPUStart().ptr + ptrOffset;
                outDesc.gpuHandle.ptr = m_heap->GetGPUStart().ptr + ptrOffset;
                outDesc.descSize = m_heap->GetDescriptorSize();

                // Adjust free range start and length
                it->start += count;
                it->length -= count;
                if (it->length == 0) {
                    // Remove empty ranges to speed up future queries
                    m_freeRanges.erase(it);
                }

                // Report success
                return true;
            }
        }

        // Could not find free space in the heap; report failure
        return false;
    }

    /// @brief Frees a range of descriptors.
    /// @param[in] start start of the range to free
    /// @param[in] count number of descriptors to free
    /// @return `true` if the descriptor range was deallocated, `false` otherwise
    bool Free(UINT start, UINT count = 1) {
        // Cannot free if not bound to a heap
        if (m_heap == nullptr) {
            return false;
        }

        // Freeing an empty range always succeeds
        if (count == 0) {
            return true;
        }

        const UINT rangeStart = std::min(start, m_heap->GetDescriptorHeapSize());
        const UINT rangeEnd = std::min(start + count, m_heap->GetDescriptorHeapSize());

        // Trivial case: no free ranges results in a single free range
        if (m_freeRanges.empty()) {
            m_freeRanges.push_back(FreeRange{.start = start, .length = count});
            return true;
        }

        // Find the first free range that overlaps or is immediately after the range to free
        auto it = m_freeRanges.begin();
        while (it != m_freeRanges.end() && rangeStart > it->start + it->length) {
            ++it;
        }

        // Trivial case: range to free is entirely after the free range.
        //   freeing:                      |-----|
        //      from: |--------------|
        // Add new free range to the end of the list.
        if (it == m_freeRanges.end()) {
            m_freeRanges.push_back(FreeRange{.start = start, .length = count});
            return true;
        }

        const UINT freeRangeStart = it->start;
        const UINT freeRangeEnd = it->start + it->length;

        // Trivial case: range to free is entirely before the free range.
        //   freeing: |-----|
        //      from:           |--------------|
        // Add new free range to the start of the list.
        if (rangeEnd < freeRangeStart) {
            m_freeRanges.insert(it, FreeRange{.start = start, .length = count});
            return true;
        }

        // Trivial case: range to free fits entirely in the free range.
        //   freeing: |-----------|
        //               |--------|
        //               |-----------|
        //            |--------------|
        //      from: |--------------|
        //    result: |--------------|
        // Should never happen -- possible double-free bug.
        if (rangeStart >= freeRangeStart && rangeEnd <= freeRangeEnd) {
            YMIR_DEV_CHECK();
            return false;
        }

        // Range to free joins or overlaps the free range on either end.
        //   freeing: |----|                    (1)
        //            |--------------|          (1) overlap
        //            |-------------------|     (1) overlap
        //            |-----------------------| (2) overlap
        //                 |------------------| (3) overlap
        //                      |-------------| (3) overlap
        //                                |---| (3)
        //      from:      |--------------|
        //   results: |-------------------|     (1)
        //            |-----------------------| (2)
        //                 |------------------| (3)
        // Overlaps indicate potential bugs.
        YMIR_DEV_ASSERT(rangeEnd == freeRangeStart || rangeStart == freeRangeEnd);

        // Expand free range to include the range to free
        FreeRange &freeRange = *it;
        freeRange.start = std::min(freeRangeStart, rangeStart);
        freeRange.length = std::max(freeRangeEnd, rangeEnd) - it->start;

        // Merge with subsequent ranges
        ++it;
        while (it != m_freeRanges.end()) {
            if (freeRange.start + freeRange.length < it->start) {
                // Next range doesn't connect or overlap with current range; cannot merge any further
                break;
            }
            const UINT mergedLength = it->start + it->length - freeRange.start;
            freeRange.length = std::max(freeRange.length, mergedLength);
            it = m_freeRanges.erase(it);
        }

        return true;
    }

    /// @brief Retrieves the index for the given CPU descriptor handle.
    /// @param[in] cpuDescHandle the CPU descriptor handle
    /// @return the index for the given handle, or 0xFFFFFFFF if not found or not part of this heap.
    UINT GetIndex(const D3D12_CPU_DESCRIPTOR_HANDLE &cpuDescHandle) {
        if (cpuDescHandle.ptr < m_heap->GetCPUStart().ptr) {
            return 0xFFFFFFFF;
        }
        const UINT index = (cpuDescHandle.ptr - m_heap->GetCPUStart().ptr) / m_heap->GetDescriptorSize();
        if (index >= m_heap->GetDescriptorHeapSize()) {
            return 0xFFFFFFFF;
        }
        return index;
    }

    /// @brief Retrieves the index for the given GPU descriptor handle.
    /// @param[in] gpuDescHandle the GPU descriptor handle
    /// @return the index for the given handle, or 0xFFFFFFFF if not found or not part of this heap.
    UINT GetIndex(const D3D12_GPU_DESCRIPTOR_HANDLE &gpuDescHandle) {
        if (gpuDescHandle.ptr < m_heap->GetGPUStart().ptr) {
            return 0xFFFFFFFF;
        }
        const UINT index = (gpuDescHandle.ptr - m_heap->GetGPUStart().ptr) / m_heap->GetDescriptorSize();
        if (index >= m_heap->GetDescriptorHeapSize()) {
            return 0xFFFFFFFF;
        }
        return index;
    }

private:
    const D3D12DescriptorHeap *m_heap = nullptr;
    struct FreeRange {
        UINT start;
        UINT length;
    };
    std::list<FreeRange> m_freeRanges;
};

} // namespace ymir::gpu::d3d12
