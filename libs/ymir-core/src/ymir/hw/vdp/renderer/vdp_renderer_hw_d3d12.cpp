#include <ymir/hw/vdp/renderer/vdp_renderer_hw_d3d12.hpp>

#include <ymir/gpu/d3d12/d3d12_commands.hpp>
#include <ymir/gpu/d3d12/d3d12_descriptor_heap.hpp>
#include <ymir/gpu/d3d12/d3d12_descriptor_heap_allocator.hpp>
#include <ymir/gpu/d3d12/d3d12_device.hpp>
#include <ymir/gpu/d3d12/d3d12_fence.hpp>
#include <ymir/gpu/d3d12/d3d12_pipeline_state.hpp>
#include <ymir/gpu/d3d12/d3d12_resource.hpp>
#include <ymir/gpu/d3d12/d3d12_root_signature.hpp>

#include <ymir/gpu/shaders/gpu_shaders.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/dev_assert.hpp>
#include <ymir/util/dev_log.hpp>
#include <ymir/util/dirty_bitmap.hpp>

#include <d3d12.h>

#include <fmt/format.h>

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(ymir_core_shaders);

#include <cassert>
#include <concepts>
#include <deque>
#include <unordered_map>
#include <vector>

using namespace ymir::gpu::d3d12;

namespace ymir::vdp {

namespace grp {

    // -------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // dx12_base
    //   dx12_upload
    //   dx12_vdp1
    //   dx12_vdp2

    struct dx12_base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "VDP-DX12";
    };

    struct dx12_upload : public dx12_base {
        // static constexpr devlog::Level level = devlog::level::trace;
        static constexpr std::string_view name = "VDP-DX12-Upload";
    };

    struct dx12_vdp1 : public dx12_base {
        static constexpr std::string_view name = "VDP1-DX12";
    };

    struct dx12_vdp2 : public dx12_base {
        static constexpr std::string_view name = "VDP2-DX12";
    };

} // namespace grp

// ---------------------------------------------------------------------------------------------------------------------
// TODO: most of these are likely to be shared across all backends. Move to a shared header.

/// @brief Name of the entrypoint function for all compute shaders.
static constexpr const char *kCSEntrypoint = "CSMain";

/// @brief Contains the compiled shader files from res/shaders/src.
cmrc::embedded_filesystem g_fsShaders = cmrc::ymir_core_shaders::get_filesystem();

struct ColorR8G8B8A8 {
    uint8 r, g, b, a;
};
static_assert(sizeof(ColorR8G8B8A8) == sizeof(uint32));

using HLSLbool = uint32; // bools align to 4 bytes
using HLSLint = sint32;
using HLSLuint = uint32;

union HLSLuint2 {
    std::array<HLSLuint, 2> array;
    struct {
        HLSLuint x, y;
    };
    struct {
        HLSLuint r, g;
    };
};
static_assert(sizeof(HLSLuint2) == sizeof(HLSLuint) * 2);

union HLSLuint3 {
    std::array<HLSLuint, 3> array;
    struct {
        HLSLuint x, y, z;
    };
    struct {
        HLSLuint r, g, b;
    };
};
static_assert(sizeof(HLSLuint3) == sizeof(HLSLuint) * 3);

union HLSLuint4 {
    std::array<HLSLuint, 4> array;
    struct {
        HLSLuint x, y, z, w;
    };
    struct {
        HLSLuint r, g, b, a;
    };
};
static_assert(sizeof(HLSLuint4) == sizeof(HLSLuint) * 4);

union HLSLint2 {
    std::array<HLSLint, 2> array;
    struct {
        HLSLint x, y;
    };
    struct {
        HLSLint r, g;
    };
};
static_assert(sizeof(HLSLint2) == sizeof(HLSLint) * 2);

union HLSLint3 {
    std::array<HLSLint, 3> array;
    struct {
        HLSLint x, y, z;
    };
    struct {
        HLSLint r, g, b;
    };
};
static_assert(sizeof(HLSLint3) == sizeof(HLSLint) * 3);

/// @brief Packs up bool into the least significant bits of an unsigned integer.
/// @tparam T the unsigned integral type
/// @param[in] bools the bools to pack
/// @return the packed value
template <std::unsigned_integral T>
static uint32 PackBools(std::span<const bool> bools) {
    T value = 0;
    size_t count = std::min(bools.size(), sizeof(T) * 8);
    for (size_t i = 0; i < count; ++i) {
        if (bools[i]) {
            value |= static_cast<T>(1u) << static_cast<T>(i);
        }
    }
    return value;
}

// Base Xst, Yst, KA for params A and B relative to startY
struct alignas(16) VDP2RotParamBase {
    uint32 tableAddress;
    sint32 Xst, Yst;
    uint32 KA;
};
static_assert(sizeof(VDP2RotParamBase) == sizeof(uint32) * 4);

// ---------------------------------------------------------------------------------------------------------------------
// TODO: these could be useful in multiple backends. Make them generic and reusable, and move to a shared header.

/// @brief Maximum number of frames in flight.
static constexpr size_t kNumFrames = 3;

/// @brief Size of the upload buffers, in bytes.
/// Should be large enough to fit multiple worst case single transfers, but not waste space needlessly.
static constexpr UINT64 kUploadBufferSize = 16 * 1024 * 1024;

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
    util::VoidResult<> Create(D3D12Device &device, size_t size) {
        auto builder = m_buffer.BufferBuilder(kUploadBufferSize);
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
    D3D12Resource &GetBufferResource() {
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
    D3D12Resource m_buffer;
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

// ---------------------------------------------------------------------------------------------------------------------

/// @brief Converts the given shader into a `D3D12_SHADER_BYTECODE` structure.
/// @tparam stage the shader stage
/// @param[in] shader the compiler shader
/// @return a `D3D12_SHADER_BYTECODE` with a reference to the shader's bytecode
template <gpu::ShaderStage stage>
D3D12_SHADER_BYTECODE ToShaderBytecode(const gpu::CompiledShader<stage> &shader) {
    return D3D12_SHADER_BYTECODE{
        .pShaderBytecode = shader.bytecode.data(),
        .BytecodeLength = shader.bytecode.size(),
    };
}

static constexpr D3D12_BARRIER_SUBRESOURCE_RANGE kTexRangeAll{
    .IndexOrFirstMipLevel = 0xFFFFFFFF,
    .NumMipLevels = 0,
};

/// @brief Determines if a barrier access grants write access.
/// NOTE: Only concerned with bits valid for compute tasks.
/// @param[in] access the access bitmask to check
/// @return `true` if any of the provided access bits grant write access, `false` if read-only.
static bool IsBarrierAccessWrite(D3D12_BARRIER_ACCESS access) {
    return access == D3D12_BARRIER_ACCESS_COMMON ||
           (access & (D3D12_BARRIER_ACCESS_UNORDERED_ACCESS | D3D12_BARRIER_ACCESS_COPY_DEST));
}

/// @brief Manages a set of transition barriers and emits them to command lists.
struct BarrierTracker {
    /// @brief Configures the use of enhanced barriers.
    /// @param[in] use whether to use enhanced barriers
    void UseEnhancedBarriers(bool use) {
        m_enhancedBarriers = use;
    }

    /// @brief Registers and initializes state tracking for the given buffer.
    /// No commands are emitted for this.
    /// @param[in] resource the buffer resource
    /// @param[in] state the initial resource state (legacy)
    /// @param[in] sync the initial barrier sync state
    /// @param[in] access the initial barrier access mode
    void InitializeBuffer(ID3D12Resource *resource, D3D12_RESOURCE_STATES state, D3D12_BARRIER_SYNC sync,
                          D3D12_BARRIER_ACCESS access) {
        assert(!m_currentBufferStates.contains(resource));
        assert(resource->GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_BUFFER);
        m_currentBufferStates[resource] = {
            .state = state,
            .sync = sync,
            .access = access,
        };
    }

    /// @brief Registers and initializes state tracking for the given texture.
    /// No commands are emitted for this.
    /// @param[in] resource the buffer resource
    /// @param[in] state the initial resource state (legacy)
    /// @param[in] sync the initial barrier sync state
    /// @param[in] access the initial barrier access mode
    /// @param[in] layout the initial barrier layout
    void InitializeTexture(ID3D12Resource *resource, D3D12_RESOURCE_STATES state, D3D12_BARRIER_SYNC sync,
                           D3D12_BARRIER_ACCESS access, D3D12_BARRIER_LAYOUT layout) {
        assert(!m_currentTextureStates.contains(resource));
        assert(resource->GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
               resource->GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
               resource->GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D);
        m_currentTextureStates[resource] = {
            .state = state,
            .sync = sync,
            .access = access,
            .layout = layout,
        };
    }

    /// @brief Registers a buffer transition.
    ///
    /// @param[in] buffer pointer to the buffer resource
    /// @param[in] newState new resource state to apply
    /// @param[in] newAccess new access bits corresponding with resource usage to apply
    /// @param[in] newState new usage bits to apply
    /// @return this barrier set
    BarrierTracker &TransitionBuffer(ID3D12Resource *buffer, D3D12_RESOURCE_STATES newState, D3D12_BARRIER_SYNC newSync,
                                     D3D12_BARRIER_ACCESS newAccess) {
        m_desiredBufferStates[buffer] = {
            .state = newState,
            .sync = newSync,
            .access = newAccess,
        };
        return *this;
    }

    /// @brief Adds a texture barrier to this set.
    ///
    /// `sync*` and `access*` parameters are used with enhanced barriers, while `state*` parameters are used with legacy
    /// barriers.
    ///
    /// @param[in] texture pointer to the texture resource
    /// @param[in] newState new resource state to apply
    /// @param[in] newAccess new access bits corresponding with resource usage to apply
    /// @param[in] newState new usage bits to apply
    /// @param[in] newLayout new texture layout to apply
    /// @return this barrier set
    BarrierTracker &TransitionTexture(ID3D12Resource *texture, D3D12_RESOURCE_STATES newState,
                                      D3D12_BARRIER_SYNC newSync, D3D12_BARRIER_ACCESS newAccess,
                                      D3D12_BARRIER_LAYOUT newLayout) {
        m_desiredTextureStates[texture] = {
            .state = newState,
            .sync = newSync,
            .access = newAccess,
            .layout = newLayout,
        };
        return *this;
    }

    /// @brief Emits a barrier command into the command list if any relevant changes to barriers are detected and
    /// updates the current states of all affected barriers.
    /// @param[in] cmdList the command list
    void Flush(D3D12GraphicsCommandList &cmdList) {
        if (auto *enhCmdList = GetCommandListForEnhancedBarriers(cmdList)) {
            std::vector<D3D12_BARRIER_GROUP> groups{};

            std::vector<D3D12_BUFFER_BARRIER> bufferBarriers{};
            for (auto &[buffer, newState] : m_desiredBufferStates) {
                BufferState oldState{};
                auto it = m_currentBufferStates.find(buffer);
                if (it != m_currentBufferStates.end()) {
                    oldState = it->second;
                }

                const bool changed = oldState.sync != newState.sync || oldState.access != newState.access;
                if (!changed) {
                    continue;
                }

                // Emit barrier on relevant changes.
                // Read-after-read does not need a barrier.
                if (IsBarrierAccessWrite(oldState.access) || IsBarrierAccessWrite(newState.access)) {
                    bufferBarriers.push_back({
                        .SyncBefore = oldState.sync,
                        .SyncAfter = newState.sync,
                        .AccessBefore = oldState.access,
                        .AccessAfter = newState.access,
                        .pResource = buffer,
                        .Offset = 0,
                        .Size = UINT64_MAX,
                    });
                }

                // Update current state
                m_currentBufferStates[buffer] = newState;
            }
            m_desiredBufferStates.clear();
            if (!bufferBarriers.empty()) {
                groups.push_back({
                    .Type = D3D12_BARRIER_TYPE_BUFFER,
                    .NumBarriers = static_cast<UINT32>(bufferBarriers.size()),
                    .pBufferBarriers = bufferBarriers.data(),
                });
            }

            std::vector<D3D12_TEXTURE_BARRIER> textureBarriers{};
            for (auto &[texture, newState] : m_desiredTextureStates) {
                TextureState oldState{};
                auto it = m_currentTextureStates.find(texture);
                if (it != m_currentTextureStates.end()) {
                    oldState = it->second;
                }

                const bool layoutChanged = oldState.layout != newState.layout;
                const bool changed =
                    oldState.sync != newState.sync || oldState.access != newState.access || layoutChanged;
                if (!changed) {
                    continue;
                }

                // Emit barrier on relevant changes
                // Read-after-read does not need a barrier unless the texture layout changed.
                if (layoutChanged || IsBarrierAccessWrite(oldState.access) || IsBarrierAccessWrite(newState.access)) {
                    // Add barrier to list
                    textureBarriers.push_back({
                        .SyncBefore = oldState.sync,
                        .SyncAfter = newState.sync,
                        .AccessBefore = oldState.access,
                        .AccessAfter = newState.access,
                        .LayoutBefore = oldState.layout,
                        .LayoutAfter = newState.layout,
                        .pResource = texture,
                        .Subresources = kTexRangeAll,
                        .Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE,
                    });
                }

                // Update current state
                m_currentTextureStates[texture] = newState;
            }
            m_desiredTextureStates.clear();
            if (!textureBarriers.empty()) {
                groups.push_back({
                    .Type = D3D12_BARRIER_TYPE_TEXTURE,
                    .NumBarriers = static_cast<UINT32>(textureBarriers.size()),
                    .pTextureBarriers = textureBarriers.data(),
                });
            }

            if (!groups.empty()) {
                enhCmdList->Barrier(groups.size(), groups.data());
            }
        } else {
            std::vector<D3D12_RESOURCE_BARRIER> barriers{};

            for (auto &[buffer, newState] : m_desiredBufferStates) {
                BufferState oldState{};
                auto it = m_currentBufferStates.find(buffer);
                if (it != m_currentBufferStates.end()) {
                    oldState = it->second;
                }

                if (oldState.state == newState.state) {
                    // No changes
                    continue;
                }

                barriers.push_back({
                    .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                    .Transition =
                        {
                            .pResource = buffer,
                            .Subresource = 0,
                            .StateBefore = oldState.state,
                            .StateAfter = newState.state,
                        },
                });

                // Update current state
                m_currentBufferStates[buffer] = newState;
            }
            m_desiredBufferStates.clear();

            for (auto &[texture, newState] : m_desiredTextureStates) {
                TextureState oldState{};
                auto it = m_currentTextureStates.find(texture);
                if (it != m_currentTextureStates.end()) {
                    oldState = it->second;
                }

                if (oldState.state == newState.state) {
                    // No changes
                    continue;
                }

                barriers.push_back({
                    .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                    .Transition =
                        {
                            .pResource = texture,
                            .Subresource = 0,
                            .StateBefore = oldState.state,
                            .StateAfter = newState.state,
                        },
                });

                // Update current state
                m_currentTextureStates[texture] = newState;
            }
            m_desiredTextureStates.clear();

            cmdList->ResourceBarrier(barriers.size(), barriers.data());
        }
    }

private:
    bool m_enhancedBarriers;

    struct BufferState {
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

        D3D12_BARRIER_SYNC sync = D3D12_BARRIER_SYNC_ALL;
        D3D12_BARRIER_ACCESS access = D3D12_BARRIER_ACCESS_COMMON;
    };
    std::unordered_map<ID3D12Resource *, BufferState> m_currentBufferStates;
    std::unordered_map<ID3D12Resource *, BufferState> m_desiredBufferStates;

    struct TextureState {
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

        D3D12_BARRIER_SYNC sync = D3D12_BARRIER_SYNC_NONE;
        D3D12_BARRIER_ACCESS access = D3D12_BARRIER_ACCESS_NO_ACCESS;
        D3D12_BARRIER_LAYOUT layout = D3D12_BARRIER_LAYOUT_UNDEFINED;
    };
    std::unordered_map<ID3D12Resource *, TextureState> m_currentTextureStates;
    std::unordered_map<ID3D12Resource *, TextureState> m_desiredTextureStates;

    /// @brief Retrieves a pointer to the specified command list if enhanced barriers are supported.
    /// @param[in] cmdList the command list
    /// @param[in] enhancedBarriers whether enhanced barriers are supported
    /// @return a pointer to the command list converted to `ID3D12GraphicsCommandList7` for enhanced barriers
    /// operations, or `nullptr` if the feature is not supported by the device
    ID3D12GraphicsCommandList7 *GetCommandListForEnhancedBarriers(D3D12GraphicsCommandList &cmdList) const {
        return m_enhancedBarriers ? cmdList.As7() : nullptr;
    }
};

// ---------------------------------------------------------------------------------------------------------------------

struct Direct3D12VDPRenderer::Impl {
    Impl(VDPState &state, const config::VDP2AccessPatternsConfig &vdp2AccessPatternsConfig,
         const config::VDP2DebugRender &vdp2DebugRenderOptions, const config::Enhancements &enhancements)
        : vdpState(state)
        , enhancements(enhancements)
        , vdp2(vdp2AccessPatternsConfig, vdp2DebugRenderOptions) {}

    VDPState &vdpState;
    const config::Enhancements &enhancements;

    D3D12Device device;

    struct Features {
        bool enhancedBarriers = false;
    } features;

    D3D12CommandQueue cmdQueue;
    D3D12Fence fence;

    D3D12DescriptorHeap offlineHeap;
    DescriptorHeapAllocator offlineHeapAlloc;

    D3D12DescriptorHeap resourceHeap;
    DescriptorHeapAllocator resourceHeapAlloc;

    /// @brief Resources for a single frame.
    struct FrameContext {
        D3D12CommandAllocator cmdAlloc;
        UINT64 signaledValue = 0; // fence value associated with this frame. 0 means never used

        void Reset() {
            cmdAlloc->Reset();
        }
    };

    /// @brief Ring buffer of frame resources.
    /// @tparam count number of frames
    /// @tparam TFrameContext frame context type. Extend FrameContext to store additional per-frame resources
    template <size_t count, typename TFrameContext = FrameContext>
        requires std::derived_from<TFrameContext, FrameContext>
    struct FrameSet {
        std::array<TFrameContext, count> frames;
        size_t frameIndex = 0;
        UINT64 currFenceValue = 0;

        TFrameContext &GetCurrentFrame() {
            return frames[frameIndex];
        }
        const TFrameContext &GetCurrentFrame() const {
            return frames[frameIndex];
        }

        util::VoidResult<> MoveToNextFrame(D3D12Fence &fence, D3D12CommandQueue &cmdQueue) {
            // Schedule a signal command in the queue
            FrameContext &currFrame = GetCurrentFrame();
            const UINT64 signalValue = currFenceValue + 1;
            if (FAILED(fence.Signal(cmdQueue, signalValue))) {
                return util::ErrorMessage{"Failed to signal fence"};
            }
            currFrame.signaledValue = signalValue;

            // Update the frame index
            ++frameIndex;
            if (frameIndex >= count) {
                frameIndex = 0;
            }

            // Wait for next frame
            FrameContext &nextFrame = GetCurrentFrame();
            if (fence->GetCompletedValue() < nextFrame.signaledValue) {
                fence.Wait(INFINITE, nextFrame.signaledValue);
            }

            // Reset frame
            nextFrame.Reset();

            // Set the fence value for the next frame
            currFenceValue = signalValue;

            return {};
        }

        util::VoidResult<> WaitForGPU(D3D12Fence &fence, D3D12CommandQueue &cmdQueue) {
            FrameContext &currFrame = GetCurrentFrame();

            // Schedule a signal command in the queue
            const UINT64 signalValue = currFenceValue + 1;
            if (FAILED(fence.Signal(cmdQueue, signalValue))) {
                return util::ErrorMessage{"Failed to signal fence"};
            }

            // Wait until the fence has been processed
            fence.Wait(INFINITE, signalValue);

            // Increment the fence value for the current frame
            currFenceValue = signalValue;

            return {};
        }

        TFrameContext &operator[](size_t index) {
            return frames[index];
        }
        const TFrameContext &operator[](size_t index) const {
            return frames[index];
        }

        constexpr size_t Count() const {
            return count;
        }
    };

    // =================================================================================================================
    // VDP1 rendering
    //
    // TODO

    struct VDP1Resources {
        /// @brief VDP1 per-frame resources.
        FrameSet<kNumFrames> frames;

        /// @brief VDP1 command list.
        D3D12GraphicsCommandList cmdList;

        /// @brief Upload ring buffer.
        UploadRingBuffer uploadBuffer;
    } vdp1;

    // =================================================================================================================
    // VDP2 rendering
    //
    // The VDP2 rendering pipeline is invoked at least once per frame. When VRAM, CRAM and/or register writes happen,
    // the renderer processes scanlines up to the previous VCNT and commits all changes before proceeding.
    //
    // Since the VDP2 rendering process has no visible effect on the rest of the Saturn's components, there is no need
    // for additional synchronization constraints on memory and register reads or writes. The VDP2 state is handled by
    // the VDP controller, and the renderer maintains a local independent copy of the VRAM, CRAM and VDP2 registers for
    // fully asynchronous rendering.
    //
    // Root 32-bit constants hold renderer parameters shared across all VDP2 compute shaders such as the starting line
    // for continuation of work interrupted by state changes, relevant registers and active enhancements.

    /// @brief Common VDP2 rendering parameters shared by all shaders.
    struct VDP2CommonRenderParams {
        // Top Y coordinate of target rendering area.
        HLSLuint startY;

        struct DisplayParams {                 //  bits  use
            HLSLuint displayEnable : 1;        //     0  Display enabled
                                               //          0 = display off
                                               //          1 = display on
            HLSLuint borderColorMode : 1;      //     1  Border color mode
                                               //          0 = black
                                               //          1 = back screen color
            HLSLuint interlaceMode : 2;        //   2-3  Interlace mode
                                               //          0 = progressive
                                               //          1 = invalid
                                               //          2 = single-density interlace
                                               //          3 = double-density interlace
            HLSLuint oddField : 1;             //     4  Field
                                               //          0 = even
                                               //          1 = odd
            HLSLuint exclusiveMonitor : 1;     //     5  Exclusive monitor mode
                                               //          0 = normal
                                               //          1 = exclusive
            HLSLuint colorRAMMode : 2;         //   6-7  Color RAM mode
                                               //          0 = RGB 5:5:5, 1024 words
                                               //          1 = RGB 5:5:5, 2048 words
                                               //          2 = RGB 8:8:8, 1024 words
                                               //          3 = RGB 8:8:8, 1024 words  (same as mode 2, undocumented)
            HLSLuint hiResH : 1;               //     8  Horizontal resolution
                                               //          0 = 320/352
                                               //          1 = 640/704
            HLSLuint palMode : 1;              //     9  Display standard (VDP2 TVSTAT.PAL)
                                               //          0 = NTSC
                                               //          1 = PAL
            HLSLuint hresMode : 3;             // 10-12  Horizontal resolution mode (VDP2 TVMD.HRESO2-0)
            HLSLuint vresMode : 2;             // 13-14  Vertical resolution mode   (VDP2 TVMD.VRESO1-0)
            HLSLuint dblInterlaceEnable : 1;   //    15  VDP1 double interlace enable flag (VDP1 FBCR.DIE)
            HLSLuint dblInterlaceDrawLine : 1; //    16  VDP1 double interlace draw line   (VDP1 FBCR.DIL)
        } displayParams;

        struct {                              //  bits  use
            HLSLuint layerEnabled : 6;        //   0-5  Layer enable state based on BGON and other factors:
                                              //        bit  RBG0+RBG1   RBG0        RBG1        no RBGs
                                              //          0  Sprite      Sprite      Sprite      Sprite
                                              //          1  RBG0        RBG0        -           -
                                              //          2  RBG1        NBG0        RBG1        NBG0
                                              //          3  EXBG        NBG1/EXBG   NBG1/EXBG   NBG1/EXBG
                                              //          4  -           NBG2        NBG2        NBG2
                                              //          5  -           NBG3        NBG3        NBG3
            HLSLuint bgEnabled : 6;           //  6-11  Individual layer enable flags
                                              //        bit  layer
                                              //          8  NBG0
                                              //          9  NBG1
                                              //         10  NBG2
                                              //         11  NBG3
                                              //         12  RBG0
                                              //         13  RBG1
            HLSLuint lineColorEnableRBG0 : 1; //    12  Line color screen enable for RBG0
            HLSLuint lineColorEnableRBG1 : 1; //    13  Line color screen enable for RBG1
            HLSLuint mosaicH : 4;             // 14-17  Horizontal mosaic size (minus one)
            HLSLuint mosaicV : 4;             // 18-21  Vertical mosaic size (minus one)
            HLSLuint rotParamMode : 2;        // 22-23  Rotation parameter mode
                                              //          0 = always use A
                                              //          1 = always use B
                                              //          2 = select based on coefficient data
                                              //          3 = select based on window flag
        } layerParams;

        struct {                          //  bits  use
            HLSLuint rotate : 1;          //     0  Sprite layer rotation
                                          //          0 = normal
                                          //          1 = use rotation parameter A
            HLSLuint pixel8Bits : 1;      //     1  VDP1 data size
                                          //          0 = 16-bit
                                          //          1 = 8-bit
            HLSLuint type : 4;            //   2-5  Sprite data type
            HLSLuint fbSizeH : 1;         //     6  VDP1 framebuffer horizontal size shift  (512 << x)
            HLSLuint inHalfResH : 1;      //     7  Sprite input at half resolution
            HLSLuint outHalfResH : 1;     //     8  Sprite output at half resolution
            HLSLuint mixedFormat : 1;     //     9  Sprite layer color format
                                          //          0 = palette only
                                          //          1 = mixed palette/RGB
            HLSLuint colorCalcEnable : 1; //    10  Sprite color calculation enable
            HLSLuint colorCalcValue : 3;  // 11-13  Sprite target color calculation value
            HLSLuint colorCalcCond : 2;   // 14-15  Special color calculation condition
            HLSLuint colorDataOffset : 3; // 16-18  Special color data offset in CRAM
            HLSLuint useSpriteWindow : 1; //    19  Use sprite window
            HLSLuint windowEnabled : 1;   //    20  Sprite window enabled for the sprite layer
            HLSLuint windowInverted : 1;  //    21  Sprite window inverted for the sprite layer
            HLSLuint displayFB : 1;       //    22  Current sprite display framebuffer index
        } spriteParams;

        // Packed 8x 3-bit sprite priorities + 5-bit color calculation ratios
        HLSLuint2 spritePriosRatios;

        struct {                        //  bits  use
            HLSLuint tableAddress : 19; //  0-18  Vertical cell scroll table address
            HLSLuint inc : 3;           // 19-21  Vertical cell scroll address increment per cell  (x << 2)
        } vcellScroll;

        struct {                               //  bits  use
            HLSLuint spriteWindowLogic : 1;    //     0  Sprite window logic        0=OR; 1=AND
            HLSLuint spriteW0Enable : 1;       //     1  Sprite W0 enable           0=disable; 1=enable
            HLSLuint spriteW0Invert : 1;       //     2  Sprite W0 invert           0=disable; 1=enable
            HLSLuint spriteW1Enable : 1;       //     3  Sprite W1 enable           0=disable; 1=enable
            HLSLuint spriteW1Invert : 1;       //     4  Sprite W1 invert           0=disable; 1=enable
            HLSLuint colorCalcWindowLogic : 1; //     5  Color calc. window logic   0=OR; 1=AND
            HLSLuint colorCalcW0Enable : 1;    //     6  Color calc. W0 enable      0=disable; 1=enable
            HLSLuint colorCalcW0Invert : 1;    //     7  Color calc. W0 invert      0=disable; 1=enable
            HLSLuint colorCalcW1Enable : 1;    //     8  Color calc. W1 enable      0=disable; 1=enable
            HLSLuint colorCalcW1Invert : 1;    //     9  Color calc. W1 invert      0=disable; 1=enable
            HLSLuint colorCalcSWEnable : 1;    //    10  Color calc. SW enable      0=disable; 1=enable
            HLSLuint colorCalcSWInvert : 1;    //    11  Color calc. SW invert      0=disable; 1=enable
        } windows;

        struct {                            //  bits  use
            HLSLuint deinterlace : 1;       //     0  Deinterlace
            HLSLuint transparentMeshes : 1; //     1  Render mesh sprites as transparent
        } enhancements;
    };

    /// @brief Global VDP2 window parameters.
    struct VDP2GlobalWindowParams {
        // Top-left coordinates
        HLSLuint2 start;

        // Bottom-right coordinates
        HLSLuint2 end;

        // Base address of the line window table
        HLSLuint lineWindowTableAddress;

        // Whether to use the line window table
        HLSLbool lineWindowTableEnable;
    };

    /// @brief Per-layer VDP2 window parameters.
    struct VDP2LayerWindowParams {
        // Window logic
        //   false = OR
        //   true = AND
        HLSLbool windowLogicAnd;

        // Window 0 enable
        HLSLbool window0Enable;

        // Window 0 invert
        HLSLbool window0Invert;

        // Window 1 enable
        HLSLbool window1Enable;

        // Window 1 invert
        HLSLbool window1Invert;
    };

    /// @brief VDP2 extended window parameters (includes sprite window).
    struct VDP2LayerWindowParamsS {
        VDP2LayerWindowParams base;

        // Sprite window enable
        HLSLbool spriteWindowEnable;

        // Sprite window invert
        HLSLbool spriteWindowInvert;
    };

    /// @brief Rotation parameter registers.
    struct VDP2RotRegs {
        // Coefficient table enabled
        HLSLbool coeffTableEnable;

        // Coefficient table location
        //   0 = VRAM
        //   1 = CRAM
        HLSLbool coeffTableCRAM;

        // Coefficient data size
        //   0 = 2 words
        //   1 = 1 word
        HLSLuint coeffDataSize;

        // Coefficient data mode
        //   0 = kx/ky
        //   1 = kx
        //   2 = ky
        //   3 = Px
        HLSLuint coeffDataMode;

        // Coefficient data access for VRAM banks:
        //  bit  bank
        //    0  A0/A
        //    1  A1
        //    2  B0/B
        //    3  B1
        HLSLuint coeffDataAccess;

        // Per-dot coefficients
        //   false = per line
        //   true  = per dot
        HLSLbool coeffDataPerDot;

        // Use coefficient line color data
        HLSLbool coeffUseLineColorData;
    };

    /// @brief Per-pixel rotation parameter states.
    struct VDP2RotParamState {
        /// @brief Output screen coordinates.
        HLSLint2 screenCoords;

        /// @brief Output sprite coordinates.
        HLSLuint2 spriteCoords;

        /// @brief Coefficient data.
        ///  bits  use
        ///   0-6  raw line color data
        ///     7  transparency
        HLSLuint coeffData;
    };

    /// @brief Base VDP2 layer rendering parameters, common to NBGs and RBGs.
    struct VDP2BaseBGParams {
        // Background enabled
        HLSLbool enabled;

        // If true, honor transparency bit in color data.
        // Derived from BGON.xxTPON
        HLSLbool enableTransparency;

        // Whether the background uses cells (false) or a bitmap (true).
        // Derived from CHCTLA/CHCTLB.xxBMEN
        HLSLbool bitmap;

        // Priority number from 0 (transparent) to 7 (highest).
        // Derived from PRINA/PRINB/PRIR.xxPRINn
        HLSLuint priorityNumber;

        // Special priority mode.
        // Derived from SFPRMD.xxSPRMn
        HLSLuint priorityMode;

        // Special function select (0=A, 1=B).
        // Derived from SFSEL.xxSFCS
        HLSLuint specialFunctionSelect;

        // Cell size shift corresponding to the dimensions of a character pattern (0=1x1, 1=2x2).
        // Derived from CHCTLA/CHCTLB.xxCHSZ
        HLSLuint cellSizeShift;

        // Character color format.
        // Derived from CHCTLA/CHCTLB.xxCHCNn
        HLSLuint colorFormat;

        // Color RAM base offset.
        // Derived from CRAOFA/CRAOFB.xxCAOSn
        HLSLuint cramOffset;

        // Supplementary bits 4-0 for scroll screen character number, when using 1-word characters.
        // Derived from PNCNn/PNCR.xxSCNn
        HLSLuint supplScrollCharNum;

        // Supplementary bits 6-4 for palette number.
        // Used with scroll screen when using 1-word characters, or with bitmap screens.
        // The value is already shifted in place to optimize rendering calculations.
        // Derived from PNCNn/PNCR.xxSPLTn (scroll) or BMPNA/BMPNB.xxBMPn (bitmap)
        HLSLuint supplPalNum;

        // Supplementary special color calculation bit.
        // Derived from PNCNn/PNCR.xxSCC (scroll) or BMPNA/BMPNB.xxBMCC (bitmap)
        HLSLuint supplSpecialColorCalc;

        // Supplementary special priority bit.
        // Derived from PNCNn/PNCR.xxSPR (scroll) or BMPNA/BMPNB.xxBMPR (bitmap)
        HLSLuint supplSpecialPriority;

        // Enables the mosaic effect.
        // If vertical cell scroll is also enabled, the mosaic effect is bypassed.
        // Derived from MZCTL.xxMZE
        HLSLbool mosaicEnable;

        // Enables color calculation.
        // Derived from CCCTL.xxCCEN
        HLSLbool colorCalcEnable;

        // Character number width: 10 bits (false) or 12 bits (true).
        // When true, disables the horizontal and vertical flip bits in the character.
        // Derived from PNCNn/PNCR.xxCNSM
        HLSLbool extChar;

        // Whether characters use one (false) or two (true) words.
        // Derived from PNCNn/PNCR.xxPNB
        HLSLbool twoWordChar;

        // Whether pattern name data is accessible for each VRAM bank (bits 0 to 3: A0, A1, B0, B1).
        // Derived from CYCxn, RAMCTL and BGON (for RBG0/1 restrictions to NBGs)
        HLSLuint patNameAccess;

        // Whether character pattern data is accessible for each VRAM bank (bits 0 to 3: A0, A1, B0, B1).
        // Derived from CYCxn, RAMCTL and BGON (for RBG0/1 restrictions to NBGs)
        HLSLuint charPatAccess;

        // Whether accesses to character pattern data for this background is delayed per bank due to illegal VRAM access
        // patterns (bits 0 to 3: A0, A1, B0, B1). Derived from CYCxn, RAMCTL, ZMCTL and CHCTLA/CHCTLB.xxCHSZ
        HLSLuint charPatDelay;

        // Address offset for VRAM data for this background on each VRAM bank caused by illegal VRAM access patterns
        // (bits 0 to 3: A0, A1, B0, B1). Derived from CYCxn, RAMCTL and ZMCTL
        HLSLuint vramDataOffset;

        // Special color calculation mode.
        // Derived from SFCCMD.xxSCCMn
        HLSLuint specialColorCalcMode;

        // Page shifts are either 0 or 1, used when determining which plane a particular (x,y) coordinate belongs to.
        // A shift of 0 corresponds to 1 page per plane dimension.
        // A shift of 1 corresponds to 2 pages per plane dimension.
        // Derived from PLSZ.xxPLSZn
        HLSLuint2 pageShift;

        // Bitmap dimensions, when the screen is in bitmap mode.
        // Derived from CHCTLA/CHCTLB.xxBMSZ
        HLSLuint2 bitmapSize;

        // Base address of bitmap data.
        // Derived from MPOFN (NBG0-3) or MPOFR (RotParam A-B)
        HLSLuint bitmapBaseAddress;

        // Window parameters
        VDP2LayerWindowParamsS windowParams;
    };

    /// @brief VDP2 NBG layer rendering parameters.
    struct NBGParams {
        VDP2BaseBGParams base;

        // Screen scroll amount, in 11.8 fixed-point format.
        // Used in scroll NBGs.
        // Scroll amounts for NBGs 2 and 3 do not have a fractional part, but the values are still stored with 8
        // fractional bits here for consistency and ease of implementation. Derived from SCXINn and SCXDNn
        HLSLuint2 scrollAmount;

        // Screen scroll increment per pixel, in 11.8 fixed-point format.
        // NBGs 2 and 3 do not have increment registers; they always increment each coordinate by 1.0, which is stored
        // here for consistency and ease of implementation. Derived from ZMXINn and ZMXDNn
        HLSLuint2 scrollInc;

        // Page base addresses for NBG planes A-D.
        // Derived from MPOFN, MPABNn, MPCDNn, CHCTLA/CHCTLB.xxCHSZ, PNCNn.xxPNB and PLSZ.xxPLSZn
        std::array<HLSLuint, 4> pageBaseAddresses;

        // Whether to use the vertical cell scroll table in VRAM.
        // Only valid for NBG0 and NBG1.
        // Derived from SCRCTL.NnVCSC
        HLSLbool vcellScrollEnable;

        // Whether to use the horizontal line scroll table in VRAM.
        // Only valid for NBG0 and NBG1.
        // Derived from SCRCTL.NnLSCX
        HLSLbool lineScrollXEnable;

        // Whether to use the vertical line scroll table in VRAM.
        // Only valid for NBG0 and NBG1.
        // Derived from SCRCTL.NnLSCY
        HLSLbool lineScrollYEnable;

        // Whether to use horizontal line zoom/scaling.
        // Only valid for NBG0 and NBG1.
        // Derived from SCRCTL.NnLZMX
        HLSLbool lineZoomEnable;

        // Line scroll table interval shift. The interval is calculated as (1 << lineScrollInterval).
        // Only valid for NBG0 and NBG1.
        // Derived from SCRCTL.NnLSS1-0
        HLSLuint lineScrollInterval;

        // Line scroll table base address.
        // Only valid for NBG0 and NBG1.
        // Derived from LSTAnU/L
        HLSLuint lineScrollTableAddress;

        // Vertical cell scroll offset.
        // Only valid for NBG0 and NBG1.
        // Based on CYCA0/A1/B0/B1 parameters.
        HLSLuint vcellScrollOffset;

        // Is the vertical cell scroll read delayed by one cycle?
        // Only valid for NBG0 and NBG1.
        // Based on CYCA0/A1/B0/B1 parameters.
        HLSLuint vcellScrollDelay;

        // Is the first vertical cell scroll entry repeated?
        // Only valid for NBG0.
        // Based on CYCA0/A1/B0/B1 parameters.
        HLSLuint vcellScrollRepeat;
    };

    /// @brief VDP2 RBG layer rendering parameters.
    struct RBGParams {
        VDP2BaseBGParams base;

        // Rotation BG screen-over process.
        // Derived from PLSZ.RxOVRn
        HLSLuint screenOverProcess;

        // Screen-over pattern name value.
        // Derived from OVPNRA/B
        HLSLuint screenOverPatternName;

        /// @brief Page base addresses for RBG planes A-P using Rotation Parameters A and B.
        /// Indexing: [RotParam A/B][Plane A-P]
        /// Derived from `mapIndices`, `CHCTLA/CHCTLB.xxCHSZ`, `PNCR.xxPNB` and `PLSZ.xxPLSZn`.
        std::array<std::array<HLSLuint, 16>, 2> pageBaseAddresses;
    };

    /// @brief LNCL/BACK screen parameters.
    struct VDP2LineBackScreenParams {
        // Base address of color data
        HLSLuint baseAddress;

        // Use colors per line
        //   false = per screen
        //   true   = per line
        HLSLbool perLine;
    };

    /// @brief VDP2 layer rendering parameters.
    struct VDP2LayerRenderParams {
        NBGParams nbg[4];
        RBGParams rbg[2];

        VDP2GlobalWindowParams windows[2];

        VDP2LayerWindowParams rotWindows;

        VDP2LineBackScreenParams lineScreenParams;
        VDP2LineBackScreenParams backScreenParams;

        // Bit-packed special function code flags.
        //  bits  use
        //   0-7  Special function code A
        //  8-15  Special function code B
        HLSLuint specialFunctionCodes;
    };

    /// @brief VDP2 compositor parameters.
    struct VDP2ComposeParams {
        // Use color calculation per layer (0=disable; 1=enable)
        //   bit  layer
        //     0  Sprite
        //     1  RBG0
        //     2  RBG1/NBG0
        //     3  NBG1/EXBG
        //     4  NBG2
        //     5  NBG3
        //     6  Back screen
        //     7  Line screen
        HLSLuint colorCalcEnable;

        // Use extended color calculation (always disabled in hi-res modes)
        HLSLbool extendedColorCalc;

        // Blend mode
        //   0 = alpha
        //   1 = additive
        HLSLbool useAdditiveBlend;

        // Use second screen ratio
        HLSLbool useSecondScreenRatio;

        // Color offset enable per layer (0=disable; 1=enable)
        //   bit  layer
        //     0  Sprite
        //     1  RBG0
        //     2  RBG1/NBG0
        //     3  NBG1/EXBG
        //     4  NBG2
        //     5  NBG3
        //     6  Back screen
        HLSLuint colorOffsetEnable;

        // Color offset select per layer (0=A; 1=B)
        //   bit  layer
        //     0  Sprite
        //     1  RBG0
        //     2  RBG1/NBG0
        //     3  NBG1/EXBG
        //     4  NBG2
        //     5  NBG3
        //     6  Back screen
        HLSLuint colorOffsetSelect;

        // Line color enable per layer (0=disable; 1=enable)
        //   bit  layer
        //     0  Sprite
        //     1  RBG0
        //     2  RBG1/NBG0
        //     3  NBG1/EXBG
        //     4  NBG2
        //     5  NBG3
        //     6  Back screen (always false to simplify shader implementation)
        HLSLuint lineColorEnable;

        // Color offset A (RGB999)
        HLSLint3 colorOffsetA;

        // Color offset B (RGB999)
        HLSLint3 colorOffsetB;

        // NBG/RBG color calculation ratios
        // index  layer
        //     0  RBG0
        //     1  NBG0/RBG1
        //     2  NBG1/EXBG
        //     3  NBG2
        //     4  NBG3
        std::array<HLSLuint, 5> bgColorCalcRatios;

        // Back/line screen color calculation ratios
        // index  layer
        //     0  Back screen
        //     1  Line screen
        std::array<HLSLuint, 2> backLineColorCalcRatios;

        // Color gradation enabled
        HLSLbool colorGradEnable;

        // Color gradation screen
        //   0  Sprite
        //   1  RBG0
        //   2  NBG0/RBG1
        //   3  (invalid)
        //   4  NBG1/EXBG
        //   5  NBG2
        //   6  NBG3
        //   7  (invalid)
        HLSLuint colorGradScreen;
    };

    /// @brief Number of entries in the VDP2 CRAM color cache.
    /// VDP2 CRAM can have at most 2048 colors (in mode 1 - RGB 5:5:5 with access to full CRAM).
    static constexpr size_t kVDP2CRAMColorCacheEntries = kVDP2CRAMSize / sizeof(uint16);

    /// @brief VDP2 CRAM converted color cache array.
    using CRAMColorCache = std::array<ColorR8G8B8A8, kVDP2CRAMColorCacheEntries>;

    /// @brief Size of the VDP2 CRAM color buffer, in bytes.
    static constexpr UINT kVDP2CRAMColorBufferSize = sizeof(CRAMColorCache);

    /// @brief Size of the VDP2 CRAM rotation coefficients buffer, in bytes.
    /// The second half of CRAM can be used for that purpose.
    static constexpr UINT kVDP2CRAMRotCoeffBufferSize = kVDP2CRAMSize / 2;

    struct VDP2Resources {
        VDP2Resources(const config::VDP2AccessPatternsConfig &accessPatternsConfig,
                      const config::VDP2DebugRender &debugRenderOptions)
            : accessPatternsConfig(accessPatternsConfig)
            , debugRenderOptions(debugRenderOptions) {}

        /// @brief VDP2 per-frame resources.
        FrameSet<kNumFrames> frames;

        /// @brief VDP2 command list.
        D3D12GraphicsCommandList cmdList;

        /// @brief Upload ring buffer.
        UploadRingBuffer uploadBuffer;

        // VDP2 VRAM is exposed as a ByteAddressBuffer to shaders as they often need to access raw bytes in 8-bit,
        // 16-bit and 32-bit formats.

        /// @brief VRAM data buffer.
        D3D12Resource vramBuffer;
        /// @brief VRAM data buffer SRV (offline).
        DescriptorRange vramSRV;

        /// @brief Bit shift for the granularity for VRAM dirty bitmap chunks.
        static constexpr size_t kVRAMDirtyBitmapChunkSizeShift = 8;

        /// @brief Granularity for VRAM dirty bitmap chunks, in bytes.
        static constexpr size_t kVRAMDirtyBitmapChunkSize = static_cast<size_t>(1) << kVRAMDirtyBitmapChunkSizeShift;

        /// @brief Number of bits in the VRAM dirty bitmap.
        static constexpr size_t kVRAMDirtyBitmapSize = kVDP2VRAMSize / kVRAMDirtyBitmapChunkSize;

        // D3D12 buffer transfers must be done in multiples of 4 bytes.
        // The chunk must not be larger than VDP2 VRAM itself. In fact, it shouldn't be too large as it wastes memory
        // and time with unnecessary copies of VRAM data.
        static_assert(kVRAMDirtyBitmapChunkSize >= sizeof(uint32) && kVRAMDirtyBitmapChunkSize <= kVDP2VRAMSize,
                      "VDP2 VRAM upload chunk size is out of range");

        /// @brief VDP2 VRAM dirty bitmap.
        util::DirtyBitmap<kVRAMDirtyBitmapSize> vramDirty;

        // VDP2 CRAM is not directly exposed. Instead, shaders get two convenient views:
        // - CRAM converted to R8G8B8A8 colors based on the current color RAM mode
        // - Top half of raw CRAM bytes, for rotation coefficients

        /// @brief CRAM color buffer.
        D3D12Resource cramColorBuffer;
        /// @brief CRAM color buffer SRV (offline).
        DescriptorRange cramColorSRV;
        /// @brief CPU-side CRAM color buffer.
        CRAMColorCache cpuCRAMColorCache{};

        /// @brief Raw CRAM rotation coefficients buffer.
        D3D12Resource cramRotCoeffBuffer;
        /// @brief Raw CRAM rotation coefficients buffer SRV (offline).
        DescriptorRange cramRotCoeffSRV;

        /// @brief VDP2 CRAM dirty flag.
        bool cramDirty;

        /// @brief 2D texture array for the outputs of NBG0-3, RBG0-1, sprite and mesh layers (in that order).
        /// Contains the intermediate per-layer outputs of the VDP2 rendering process.
        D3D12Resource layerOutTexture;
        /// @brief Layer outputs SRV (offline).
        DescriptorRange layerOutSRV;
        /// @brief Layer outputs UAV (offline).
        DescriptorRange layerOutUAV;

        /// @brief 2D texture array for RBG0-1 line color outputs (in that order).
        D3D12Resource rbgLineColorOutTexture;
        /// @brief RBG0-1 line color outputs SRV (offline).
        DescriptorRange rbgLineColorOutSRV;
        /// @brief RBG0-1 line color outputs UAV (offline).
        DescriptorRange rbgLineColorOutUAV;

        /// @brief Color calculation window 2D texture.
        D3D12Resource colorCalcWindowTexture;
        /// @brief Color calculation window SRV (offline).
        DescriptorRange colorCalcWindowSRV;
        /// @brief Color calculation window UAV (offline).
        DescriptorRange colorCalcWindowUAV;

        /// @brief LNCL/BACK screen buffer.
        D3D12Resource lnclBackBuffer;
        /// @brief LNCL/BACK screen buffer SRV (offline).
        DescriptorRange lnclBackSRV;
        /// @brief CPU-side LNCL/BACK screen buffer (0=LNCL; 1=BACK).
        std::array<std::array<ColorR8G8B8A8, kMaxResV>, 2> cpuLnclBack{};

        /// @brief VDP2 rotation registers buffer.
        D3D12Resource rotRegsBuffer;
        /// @brief VDP2 rotation registers buffer SRV (offline).
        DescriptorRange rotRegsSRV;
        /// @brief CPU-side VDP2 rotation registers.
        std::array<VDP2RotRegs, 2> cpuRotRegs{};

        /// @brief VDP2 rotation parameter base values buffer.
        D3D12Resource rotParamBasesBuffer;
        /// @brief VDP2 rotation parameter base values buffer SRV (offline).
        DescriptorRange rotParamBasesSRV;
        /// @brief CPU-side VDP2 rotation parameter base values.
        std::array<VDP2RotParamBase, kMaxNormalResV * 2> cpuRotParamBases{};

        /// @brief VDP2 sprite attributes 2D texture array (sprite then mesh).
        D3D12Resource spriteAttrsTexture;
        /// @brief VDP2 sprite attributes SRV (offline).
        DescriptorRange spriteAttrsSRV;
        /// @brief VDP2 sprite attributes UAV (offline).
        DescriptorRange spriteAttrsUAV;

        /// @brief 2D texture for the composited VDP2 output.
        D3D12Resource compositeOutTexture;
        /// @brief Composited VDP2 output UAV (offline).
        DescriptorRange compositeOutUAV;

        // ---------------------------------------------------------------------

        /// @brief Common rendering parameters, uploaded as 32-bit root constants.
        VDP2CommonRenderParams cpuCommonRenderParams{};

        /// @brief Layer rendering parameters buffer.
        D3D12Resource layerRenderParamsBuffer;
        /// @brief Layer rendering parameters buffer SRV (offline).
        DescriptorRange layerRenderParamsSRV;
        /// @brief CPU-side layer rendering parameters.
        VDP2LayerRenderParams cpuLayerRenderParams{};

        /// @brief Layer composition parameters buffer.
        D3D12Resource composeParamsBuffer;
        /// @brief Layer composition parameters buffer SRV (offline).
        DescriptorRange composeParamsSRV;
        /// @brief CPU-side layer composition parameters.
        VDP2ComposeParams cpuComposeParams{};

        /// @brief Compute shader for drawing the sprite layer.
        gpu::ComputeShader drawSpriteShader;
        /// @brief Root signature for drawing the sprite layer.
        D3D12RootSignature drawSpriteRootSig;
        /// @brief Pipeline state object for drawing the sprite layer.
        D3D12PipelineState drawSpritePSO;
        /// @brief Descriptor range for drawing the sprite layer.
        DescriptorRange drawSpriteDescs;

        /// @brief Compute shader for drawing background layers.
        gpu::ComputeShader drawBGsShader;
        /// @brief Root signature for drawing background layers.
        D3D12RootSignature drawBGsRootSig;
        /// @brief Pipeline state object for drawing background layers.
        D3D12PipelineState drawBGsPSO;
        /// @brief Descriptor range for drawing background layers.
        DescriptorRange drawBGsDescs;

        /// @brief Compute shader for compositing layers.
        gpu::ComputeShader composeShader;
        /// @brief Root signature for compositing layers.
        D3D12RootSignature composeRootSig;
        /// @brief Pipeline state object for compositing layers.
        D3D12PipelineState composePSO;
        /// @brief Descriptor range for compositing layers.
        DescriptorRange composeDescs;

        // ---------------------------------------------------------------------
        // Rendering state

        uint32 nextLayerRenderLine = 0;
        uint32 nextComposeLine = 0;

        BarrierTracker barrierTracker;

        bool rotRegsDirty = false;
        bool layerRenderParamsDirty = false;
        bool composeParamsDirty = false;

        const config::VDP2AccessPatternsConfig &accessPatternsConfig;
        const config::VDP2DebugRender &debugRenderOptions;
    } vdp2;

    // =================================================================================================================
    // Operations

    util::VoidResult<> Initialize(ID3D12Device *pDevice) {
        device.Assign(pDevice);

        // Check features
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12)))) {
            features.enhancedBarriers = options12.EnhancedBarriersSupported;
        } else {
            features.enhancedBarriers = false;
        }
        vdp2.barrierTracker.UseEnhancedBarriers(features.enhancedBarriers);

        // Main command queue
        if (HRESULT hr = cmdQueue.Create(device, D3D12_COMMAND_LIST_TYPE_COMPUTE); FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Could not create VDP renderer command queue, error code {:X}", (uint32)hr)};
        }
        cmdQueue->SetName(L"[Ymir-VDP] Command queue");

        // Main fence
        if (HRESULT hr = fence.Create(device, 0, D3D12_FENCE_FLAG_NONE); FAILED(hr)) {
            return util::ErrorMessage{fmt::format("Could not create VDP renderer fence, error code {:X}", (uint32)hr)};
        }
        fence->SetName(L"[Ymir-VDP] Fence");

        // Resource heaps
        {
            D3D12_DESCRIPTOR_HEAP_DESC desc{
                .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                .NumDescriptors = 64,
                .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
            };
            if (HRESULT hr = resourceHeap.Create(device, desc); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP renderer CBV/SRV/UAV heap, error code {:X}", (uint32)hr)};
            }
            resourceHeap->SetName(L"[Ymir-VDP] CBV/SRV/UAV heap");
            resourceHeapAlloc.Bind(resourceHeap);

            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            if (HRESULT hr = offlineHeap.Create(device, desc); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP renderer offline CBV/SRV/UAV heap, error code {:X}", (uint32)hr)};
            }
            offlineHeap->SetName(L"[Ymir-VDP] CBV/SRV/UAV offline heap");
            offlineHeapAlloc.Bind(offlineHeap);
        }

        // -------------------------------------------------------------------------------------------------------------

        // VDP1 command allocators and list
        for (int i = 0; i < vdp1.frames.Count(); ++i) {
            FrameContext &frame = vdp1.frames[i];
            if (HRESULT hr = frame.cmdAlloc.Create(device, D3D12_COMMAND_LIST_TYPE_COMPUTE); FAILED(hr)) {
                return util::ErrorMessage{fmt::format(
                    "Could not create VDP1 renderer command allocator #{}, error code {:X}", i, (uint32)hr)};
            }
            frame.cmdAlloc->SetName(fmt::format(L"[Ymir-VDP1] Command allocator #{}", i).c_str());
        }
        if (HRESULT hr =
                vdp1.cmdList.Create(device, vdp1.frames.GetCurrentFrame().cmdAlloc, D3D12_COMMAND_LIST_TYPE_COMPUTE);
            FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Could not create VDP1 renderer command list, error code {:X}", (uint32)hr)};
        }
        vdp1.cmdList->SetName(L"[Ymir-VDP1] Command list");

        // Generic VDP1 upload buffer
        {
            if (auto result = vdp1.uploadBuffer.Create(device, kUploadBufferSize); !result) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP1 upload buffer: {}", result.Error().message)};
            }
            vdp1.uploadBuffer.SetDebugName("VDP1");
            vdp1.uploadBuffer.GetBufferResource()->SetName(L"[Ymir-VDP1] Upload buffer");
        }

        // -------------------------------------------------------------------------------------------------------------

        // VDP2 command allocators and list
        for (int i = 0; i < vdp2.frames.Count(); ++i) {
            FrameContext &frame = vdp2.frames[i];
            if (HRESULT hr = frame.cmdAlloc.Create(device, D3D12_COMMAND_LIST_TYPE_COMPUTE); FAILED(hr)) {
                return util::ErrorMessage{fmt::format(
                    "Could not create VDP2 renderer command allocator #{}, error code {:X}", i, (uint32)hr)};
            }
            frame.cmdAlloc->SetName(fmt::format(L"[Ymir-VDP2] Command allocator #{}", i).c_str());
        }
        if (HRESULT hr =
                vdp2.cmdList.Create(device, vdp2.frames.GetCurrentFrame().cmdAlloc, D3D12_COMMAND_LIST_TYPE_COMPUTE);
            FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Could not create VDP2 renderer command list, error code {:X}", (uint32)hr)};
        }
        vdp2.cmdList->SetName(L"[Ymir-VDP2] Command list");

        // Generic VDP2 upload buffer
        {
            if (auto result = vdp2.uploadBuffer.Create(device, kUploadBufferSize); !result) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP2 upload buffer: {}", result.Error().message)};
            }
            vdp2.uploadBuffer.SetDebugName("VDP2");
            vdp2.uploadBuffer.GetBufferResource()->SetName(L"[Ymir-VDP2] Upload buffer");
        }

        // VDP2 VRAM buffer
        {
            auto builder = vdp2.vramBuffer.BufferBuilder(kVDP2VRAMSize);
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP2 VRAM buffer, error code {:X}", (uint32)hr)};
            }
            vdp2.vramBuffer->SetName(L"[Ymir-VDP2] VRAM buffer");

            vdp2.barrierTracker.InitializeBuffer(
                vdp2.vramBuffer.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

            if (!offlineHeapAlloc.Allocate(vdp2.vramSRV)) {
                return util::ErrorMessage{"Could not allocate VDP2 VRAM buffer SRV"};
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                .Format = DXGI_FORMAT_R32_TYPELESS,
                .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Buffer =
                    {
                        .FirstElement = 0,
                        .NumElements = kVDP2VRAMSize / sizeof(uint32),
                        .StructureByteStride = 0,
                        .Flags = D3D12_BUFFER_SRV_FLAG_RAW,
                    },
            };
            device->CreateShaderResourceView(vdp2.vramBuffer.GetPointer(), &srvDesc, vdp2.vramSRV.cpuHandle);
        }

        // VDP2 CRAM color buffer
        {
            auto builder = vdp2.cramColorBuffer.BufferBuilder(kVDP2CRAMColorBufferSize);
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP2 CRAM color buffer, error code {:X}", (uint32)hr)};
            }
            vdp2.cramColorBuffer->SetName(L"[Ymir-VDP2] CRAM color buffer");

            vdp2.barrierTracker.InitializeBuffer(
                vdp2.cramColorBuffer.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

            if (!offlineHeapAlloc.Allocate(vdp2.cramColorSRV)) {
                return util::ErrorMessage{"Could not allocate VDP2 CRAM color buffer SRV"};
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                .Format = DXGI_FORMAT_R8G8B8A8_UINT,
                .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Buffer =
                    {
                        .FirstElement = 0,
                        .NumElements = kVDP2CRAMColorBufferSize / sizeof(uint32),
                        .StructureByteStride = 0,
                        .Flags = D3D12_BUFFER_SRV_FLAG_NONE,
                    },
            };
            device->CreateShaderResourceView(vdp2.cramColorBuffer.GetPointer(), &srvDesc, vdp2.cramColorSRV.cpuHandle);
        }

        // VDP2 CRAM rotation coefficients buffer
        {

            auto builder = vdp2.cramRotCoeffBuffer.BufferBuilder(kVDP2CRAMRotCoeffBufferSize);
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{fmt::format(
                    "Could not create VDP2 CRAM rotation coefficients buffer, error code {:X}", (uint32)hr)};
            }
            vdp2.cramRotCoeffBuffer->SetName(L"[Ymir-VDP2] CRAM rotation coefficients buffer");

            vdp2.barrierTracker.InitializeBuffer(
                vdp2.cramRotCoeffBuffer.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

            if (!offlineHeapAlloc.Allocate(vdp2.cramRotCoeffSRV)) {
                return util::ErrorMessage{"Could not allocate VDP2 CRAM rotation coefficients buffer SRV"};
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                .Format = DXGI_FORMAT_R32_TYPELESS,
                .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Buffer =
                    {
                        .FirstElement = 0,
                        .NumElements = kVDP2CRAMRotCoeffBufferSize / sizeof(uint32),
                        .StructureByteStride = 0,
                        .Flags = D3D12_BUFFER_SRV_FLAG_RAW,
                    },
            };
            device->CreateShaderResourceView(vdp2.cramRotCoeffBuffer.GetPointer(), &srvDesc,
                                             vdp2.cramRotCoeffSRV.cpuHandle);
        }

        // Layer outputs 2D texture array
        {
            // The array contains:
            //   [0..3] NBG0-3
            //   [4..5] RBG0-1
            //      [6] Sprite
            //      [7] Transparent meshes
            // The alpha channel is used for pixel attributes:
            //   [0..2] Priority
            //      [3] (Sprite only) Color MSB
            //      [4] (Sprite only) Shadow/window flag - sprite data SD = 1
            //      [5] (Sprite only) Normal shadow flag - sprite data DC = ...111110
            //      [6] Special color calculation flag
            //      [7] Transparent flag (0=opaque, 1=transparent)
            static constexpr UINT16 kNumLayers = 4 + 2 + 1 + 1;
            static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UINT;

            auto builder = vdp2.layerOutTexture.Texture2DBuilder(kMaxResH, kMaxResV, kNumLayers);
            builder.Format(kFormat);
            builder.Flags(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create layer outputs texture array, error code {:X}", (uint32)hr)};
            }
            vdp2.layerOutTexture->SetName(L"[Ymir-VDP2] Layer outputs texture array");

            vdp2.barrierTracker.InitializeTexture(
                vdp2.layerOutTexture.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COMMON);

            if (!offlineHeapAlloc.Allocate(vdp2.layerOutSRV)) {
                return util::ErrorMessage{"Could not allocate layer outputs texture array SRV"};
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                .Format = kFormat,
                .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Texture2DArray =
                    {
                        .MostDetailedMip = 0,
                        .MipLevels = 1,
                        .FirstArraySlice = 0,
                        .ArraySize = kNumLayers,
                        .PlaneSlice = 0,
                        .ResourceMinLODClamp = 0.0f,
                    },
            };
            device->CreateShaderResourceView(vdp2.layerOutTexture.GetPointer(), &srvDesc, vdp2.layerOutSRV.cpuHandle);

            if (!offlineHeapAlloc.Allocate(vdp2.layerOutUAV)) {
                return util::ErrorMessage{"Could not allocate layer outputs texture array UAV"};
            }
            const D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{
                .Format = kFormat,
                .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY,
                .Texture2DArray =
                    {
                        .MipSlice = 0,
                        .FirstArraySlice = 0,
                        .ArraySize = kNumLayers,
                        .PlaneSlice = 0,
                    },
            };
            device->CreateUnorderedAccessView(vdp2.layerOutTexture.GetPointer(), nullptr, &uavDesc,
                                              vdp2.layerOutUAV.cpuHandle);
        }

        // RBG0-1 line color outputs 2D texture array
        {
            static constexpr UINT16 kNumLayers = 2;
            static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UINT;

            auto builder = vdp2.rbgLineColorOutTexture.Texture2DBuilder(kMaxNormalResH, kMaxNormalResV, kNumLayers);
            builder.Format(kFormat);
            builder.Flags(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create RBG line color outputs texture array, error code {:X}", (uint32)hr)};
            }
            vdp2.rbgLineColorOutTexture->SetName(L"[Ymir-VDP2] RBG line color outputs texture array");

            vdp2.barrierTracker.InitializeTexture(
                vdp2.rbgLineColorOutTexture.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COMMON);

            if (!offlineHeapAlloc.Allocate(vdp2.rbgLineColorOutSRV)) {
                return util::ErrorMessage{"Could not allocate RBG line color outputs texture array SRV"};
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                .Format = kFormat,
                .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Texture2DArray =
                    {
                        .MostDetailedMip = 0,
                        .MipLevels = 1,
                        .FirstArraySlice = 0,
                        .ArraySize = kNumLayers,
                        .PlaneSlice = 0,
                        .ResourceMinLODClamp = 0.0f,
                    },
            };
            device->CreateShaderResourceView(vdp2.rbgLineColorOutTexture.GetPointer(), &srvDesc,
                                             vdp2.rbgLineColorOutSRV.cpuHandle);

            if (!offlineHeapAlloc.Allocate(vdp2.rbgLineColorOutUAV)) {
                return util::ErrorMessage{"Could not allocate RBG line color outputs texture array UAV"};
            }
            const D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{
                .Format = kFormat,
                .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY,
                .Texture2DArray =
                    {
                        .MipSlice = 0,
                        .FirstArraySlice = 0,
                        .ArraySize = kNumLayers,
                        .PlaneSlice = 0,
                    },
            };
            device->CreateUnorderedAccessView(vdp2.rbgLineColorOutTexture.GetPointer(), nullptr, &uavDesc,
                                              vdp2.rbgLineColorOutUAV.cpuHandle);
        }

        // Color calculation window texture
        {
            static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8_UINT;

            auto builder = vdp2.colorCalcWindowTexture.Texture2DBuilder(kMaxResH, kMaxResV);
            builder.Format(kFormat);
            builder.Flags(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create color calculation window texture, error code {:X}", (uint32)hr)};
            }
            vdp2.colorCalcWindowTexture->SetName(L"[Ymir-VDP2] Color calculation window texture");

            vdp2.barrierTracker.InitializeTexture(
                vdp2.colorCalcWindowTexture.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COMMON);

            if (!offlineHeapAlloc.Allocate(vdp2.colorCalcWindowSRV)) {
                return util::ErrorMessage{"Could not allocate color calculation window texture SRV"};
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                .Format = kFormat,
                .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Texture2D =
                    {
                        .MostDetailedMip = 0,
                        .MipLevels = 1,
                        .PlaneSlice = 0,
                        .ResourceMinLODClamp = 0.0f,
                    },
            };
            device->CreateShaderResourceView(vdp2.colorCalcWindowTexture.GetPointer(), &srvDesc,
                                             vdp2.colorCalcWindowSRV.cpuHandle);

            if (!offlineHeapAlloc.Allocate(vdp2.colorCalcWindowUAV)) {
                return util::ErrorMessage{"Could not allocate color calculation window texture UAV"};
            }
            const D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{
                .Format = kFormat,
                .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
                .Texture2D =
                    {
                        .MipSlice = 0,
                        .PlaneSlice = 0,
                    },
            };
            device->CreateUnorderedAccessView(vdp2.colorCalcWindowTexture.GetPointer(), nullptr, &uavDesc,
                                              vdp2.colorCalcWindowUAV.cpuHandle);
        }

        // LNCL/BACK screen buffer
        {
            static constexpr size_t kNumEntries = 2 * kMaxResV;
            static constexpr size_t kEntrySize = sizeof(ColorR8G8B8A8);
            auto builder = vdp2.lnclBackBuffer.BufferBuilder(sizeof(vdp2.cpuLnclBack));
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create LNCL/BACK screen buffer, error code {:X}", (uint32)hr)};
            }
            vdp2.lnclBackBuffer->SetName(L"[Ymir-VDP2] LNCL/BACK screen buffer");

            vdp2.barrierTracker.InitializeBuffer(
                vdp2.lnclBackBuffer.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

            if (!offlineHeapAlloc.Allocate(vdp2.lnclBackSRV)) {
                return util::ErrorMessage{"Could not allocate LNCL/BACK screen buffer SRV"};
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                .Format = DXGI_FORMAT_UNKNOWN,
                .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Buffer =
                    {
                        .FirstElement = 0,
                        .NumElements = kNumEntries,
                        .StructureByteStride = kEntrySize,
                        .Flags = D3D12_BUFFER_SRV_FLAG_NONE,
                    },
            };
            device->CreateShaderResourceView(vdp2.lnclBackBuffer.GetPointer(), &srvDesc, vdp2.lnclBackSRV.cpuHandle);
        }

        // Composited VDP2 output texture
        {
            static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

            auto builder = vdp2.compositeOutTexture.Texture2DBuilder(kMaxResH, kMaxResV);
            builder.Format(kFormat);
            builder.Flags(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create composited output texture, error code {:X}", (uint32)hr)};
            }
            vdp2.compositeOutTexture->SetName(L"[Ymir-VDP2] Composited output texture");

            // TODO: initialize barrier tracking for this resource if needed
            // vdp2.barrierTracker.InitializeTexture(
            //     vdp2.compositeOutTexture.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            //     D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
            //     D3D12_BARRIER_LAYOUT_COMMON);

            if (!offlineHeapAlloc.Allocate(vdp2.compositeOutUAV)) {
                return util::ErrorMessage{"Could not create composited output texture UAV"};
            }
            const D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{
                .Format = kFormat,
                .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
                .Texture2D =
                    {
                        .MipSlice = 0,
                        .PlaneSlice = 0,
                    },
            };
            device->CreateUnorderedAccessView(vdp2.compositeOutTexture.GetPointer(), nullptr, &uavDesc,
                                              vdp2.compositeOutUAV.cpuHandle);
        }

        // VDP2 rotation registers buffer
        {
            auto builder = vdp2.rotRegsBuffer.BufferBuilder(sizeof(vdp2.cpuRotRegs));
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP2 rotation registers buffer, error code {:X}", (uint32)hr)};
            }
            vdp2.rotRegsBuffer->SetName(L"[Ymir-VDP2] Rotation registers buffer");

            vdp2.barrierTracker.InitializeBuffer(
                vdp2.rotRegsBuffer.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

            if (!offlineHeapAlloc.Allocate(vdp2.rotRegsSRV)) {
                return util::ErrorMessage{"Could not allocate VDP2 rotation registers buffer SRV"};
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                .Format = DXGI_FORMAT_UNKNOWN,
                .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Buffer =
                    {
                        .FirstElement = 0,
                        .NumElements = 2,
                        .StructureByteStride = sizeof(VDP2RotRegs),
                        .Flags = D3D12_BUFFER_SRV_FLAG_NONE,
                    },
            };
            device->CreateShaderResourceView(vdp2.rotRegsBuffer.GetPointer(), &srvDesc, vdp2.rotRegsSRV.cpuHandle);
        }

        // VDP2 rotation parameter base values buffer
        {
            static constexpr size_t kRotParamBasesCount = kMaxNormalResV * 2;
            auto builder = vdp2.rotParamBasesBuffer.BufferBuilder(sizeof(vdp2.cpuRotParamBases));
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{fmt::format(
                    "Could not create VDP2 rotation parameter base values buffer, error code {:X}", (uint32)hr)};
            }
            vdp2.rotParamBasesBuffer->SetName(L"[Ymir-VDP2] Rotation parameter base values buffer");

            vdp2.barrierTracker.InitializeBuffer(
                vdp2.rotParamBasesBuffer.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

            if (!offlineHeapAlloc.Allocate(vdp2.rotParamBasesSRV)) {
                return util::ErrorMessage{"Could not allocate VDP2 rotation parameter base values buffer SRV"};
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                .Format = DXGI_FORMAT_UNKNOWN,
                .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Buffer =
                    {
                        .FirstElement = 0,
                        .NumElements = kRotParamBasesCount,
                        .StructureByteStride = sizeof(VDP2RotParamBase),
                        .Flags = D3D12_BUFFER_SRV_FLAG_NONE,
                    },
            };
            device->CreateShaderResourceView(vdp2.rotParamBasesBuffer.GetPointer(), &srvDesc,
                                             vdp2.rotParamBasesSRV.cpuHandle);
        }

        // VDP2 sprite attributes 2D texture array
        {
            static constexpr UINT16 kNumLayers = 2;
            static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8_UINT;

            auto builder = vdp2.spriteAttrsTexture.Texture2DBuilder(kMaxResH, kMaxResV, kNumLayers);
            builder.Format(kFormat);
            builder.Flags(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create sprite attributes texture array, error code {:X}", (uint32)hr)};
            }
            vdp2.spriteAttrsTexture->SetName(L"[Ymir-VDP2] Sprite attributes texture array");

            vdp2.barrierTracker.InitializeTexture(
                vdp2.spriteAttrsTexture.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COMMON);

            if (!offlineHeapAlloc.Allocate(vdp2.spriteAttrsSRV)) {
                return util::ErrorMessage{"Could not allocate sprite attributes texture array SRV"};
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                .Format = kFormat,
                .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Texture2DArray =
                    {
                        .MostDetailedMip = 0,
                        .MipLevels = 1,
                        .FirstArraySlice = 0,
                        .ArraySize = kNumLayers,
                        .PlaneSlice = 0,
                        .ResourceMinLODClamp = 0.0f,
                    },
            };
            device->CreateShaderResourceView(vdp2.spriteAttrsTexture.GetPointer(), &srvDesc,
                                             vdp2.spriteAttrsSRV.cpuHandle);

            if (!offlineHeapAlloc.Allocate(vdp2.spriteAttrsUAV)) {
                return util::ErrorMessage{"Could not allocate sprite attributes texture array UAV"};
            }
            const D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{
                .Format = kFormat,
                .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY,
                .Texture2DArray =
                    {
                        .MipSlice = 0,
                        .FirstArraySlice = 0,
                        .ArraySize = kNumLayers,
                        .PlaneSlice = 0,
                    },
            };
            device->CreateUnorderedAccessView(vdp2.spriteAttrsTexture.GetPointer(), nullptr, &uavDesc,
                                              vdp2.spriteAttrsUAV.cpuHandle);
        }

        // VDP2 layer rendering parameters buffer
        {
            auto builder = vdp2.layerRenderParamsBuffer.BufferBuilder(sizeof(vdp2.cpuLayerRenderParams));
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{fmt::format(
                    "Could not create VDP2 layer rendering parameters buffer, error code {:X}", (uint32)hr)};
            }
            vdp2.layerRenderParamsBuffer->SetName(L"[Ymir-VDP2] Layer rendering parameters buffer");

            vdp2.barrierTracker.InitializeBuffer(
                vdp2.layerRenderParamsBuffer.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

            if (!offlineHeapAlloc.Allocate(vdp2.layerRenderParamsSRV)) {
                return util::ErrorMessage{"Could not allocate VDP2 layer rendering parameters buffer SRV"};
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                .Format = DXGI_FORMAT_UNKNOWN,
                .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Buffer =
                    {
                        .FirstElement = 0,
                        .NumElements = 1,
                        .StructureByteStride = sizeof(vdp2.cpuLayerRenderParams),
                        .Flags = D3D12_BUFFER_SRV_FLAG_NONE,
                    },
            };
            device->CreateShaderResourceView(vdp2.layerRenderParamsBuffer.GetPointer(), &srvDesc,
                                             vdp2.layerRenderParamsSRV.cpuHandle);
        }

        // VDP2 layer compositing parameters buffer
        {
            auto builder = vdp2.composeParamsBuffer.BufferBuilder(sizeof(vdp2.cpuComposeParams));
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{fmt::format(
                    "Could not create VDP2 layer compositing parameters buffer, error code {:X}", (uint32)hr)};
            }
            vdp2.composeParamsBuffer->SetName(L"[Ymir-VDP2] Layer compositing parameters buffer");

            vdp2.barrierTracker.InitializeBuffer(
                vdp2.composeParamsBuffer.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

            if (!offlineHeapAlloc.Allocate(vdp2.composeParamsSRV)) {
                return util::ErrorMessage{"Could not allocate VDP2 layer compositing parameters buffer SRV"};
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                .Format = DXGI_FORMAT_UNKNOWN,
                .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Buffer =
                    {
                        .FirstElement = 0,
                        .NumElements = 1,
                        .StructureByteStride = sizeof(vdp2.cpuComposeParams),
                        .Flags = D3D12_BUFFER_SRV_FLAG_NONE,
                    },
            };
            device->CreateShaderResourceView(vdp2.composeParamsBuffer.GetPointer(), &srvDesc,
                                             vdp2.composeParamsSRV.cpuHandle);
        }

        // Build shader, PSO and related resources for drawing the sprite layer
        {
            auto shaderBlobResult = LoadShader("src/vdp/cs_vdp2_render_sprite.cso");
            if (!shaderBlobResult) {
                return util::ErrorMessage{fmt::format("Could not load VDP2 sprite layer drawing compute shader: {}",
                                                      shaderBlobResult.Error().message)};
            }
            vdp2.drawSpriteShader.format = gpu::ShaderBytecodeFormat::DXIL;
            vdp2.drawSpriteShader.bytecode = shaderBlobResult.Value();
            vdp2.drawSpriteShader.entrypoint = kCSEntrypoint;
            auto result = gpu::ValidateShader(vdp2.drawSpriteShader);
            if (!result) {
                return util::ErrorMessage{fmt::format("VDP2 sprite layer drawing compute shader validation failed: {}",
                                                      result.Error().message)};
            }

            auto rootSigBuilder = vdp2.drawSpriteRootSig.Builder();
            rootSigBuilder.Add32BitConstants(0, sizeof(VDP2CommonRenderParams) / sizeof(uint32));
            rootSigBuilder.AddDescriptorTable()
                .AddSRVs(5, 1) // NOTE: starting from 1 because SPIRV-Cross assumes buffers in t0 are constant
                .AddUAVs(2, 0);
            if (HRESULT hr = rootSigBuilder.Build(device); FAILED(hr)) {
                return util::ErrorMessage{fmt::format(
                    "Could not build VDP2 sprite layer drawing root signature, error code {:X}", (uint32)hr)};
            }
            vdp2.drawSpriteRootSig->SetName(L"[Ymir-VDP2] Sprite layer drawing root signature");

            const D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{
                .pRootSignature = vdp2.drawSpriteRootSig.GetPointer(),
                .CS = ToShaderBytecode(vdp2.drawSpriteShader),
            };
            if (HRESULT hr = vdp2.drawSpritePSO.CreateCompute(device, psoDesc); FAILED(hr)) {
                return util::ErrorMessage{fmt::format(
                    "Could not build VDP2 sprite layer drawing pipeline state object, error code {:X}", (uint32)hr)};
            }
            vdp2.drawSpritePSO->SetName(L"[Ymir-VDP2] Sprite layer drawing pipeline state object");

            const D3D12_CPU_DESCRIPTOR_HANDLE srcHandles[] = {
                vdp2.layerRenderParamsSRV.cpuHandle,
                vdp2.rotRegsSRV.cpuHandle,
                vdp2.vramSRV.cpuHandle,
                vdp2.cramColorSRV.cpuHandle,
                vdp2.rotParamBasesSRV.cpuHandle,
                /* TODO: vdp1.spriteFBSRV.cpuHandle,*/ vdp2.layerOutUAV.cpuHandle,
                vdp2.spriteAttrsUAV.cpuHandle,
            };
            std::array<UINT, std::size(srcHandles)> srcSizes{};
            srcSizes.fill(1);

            if (!resourceHeapAlloc.Allocate(vdp2.drawSpriteDescs, std::size(srcHandles))) {
                return util::ErrorMessage{"Could not allocate VDP2 sprite layer drawing descriptors"};
            }

            device->CopyDescriptors(1, &vdp2.drawSpriteDescs.cpuHandle, &vdp2.drawSpriteDescs.count,
                                    std::size(srcHandles), srcHandles, srcSizes.data(), resourceHeap.GetHeapType());
        }

        // Build shader, PSO and related resources for drawing layers
        {
            auto shaderBlobResult = LoadShader("src/vdp/cs_vdp2_render_bgs.cso");
            if (!shaderBlobResult) {
                return util::ErrorMessage{fmt::format("Could not load VDP2 layer rendering compute shader: {}",
                                                      shaderBlobResult.Error().message)};
            }
            vdp2.drawBGsShader.format = gpu::ShaderBytecodeFormat::DXIL;
            vdp2.drawBGsShader.bytecode = shaderBlobResult.Value();
            vdp2.drawBGsShader.entrypoint = kCSEntrypoint;
            auto result = gpu::ValidateShader(vdp2.drawBGsShader);
            if (!result) {
                return util::ErrorMessage{
                    fmt::format("VDP2 layer rendering compute shader validation failed: {}", result.Error().message)};
            }

            auto rootSigBuilder = vdp2.drawBGsRootSig.Builder();
            rootSigBuilder.Add32BitConstants(0, sizeof(VDP2CommonRenderParams) / sizeof(uint32));
            rootSigBuilder.AddDescriptorTable()
                .AddSRVs(7, 1) // NOTE: starting from 1 because SPIRV-Cross assumes buffers in t0 are constant
                .AddUAVs(3, 0);
            if (HRESULT hr = rootSigBuilder.Build(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not build VDP2 layer rendering root signature, error code {:X}", (uint32)hr)};
            }
            vdp2.drawBGsRootSig->SetName(L"[Ymir-VDP2] Layer rendering root signature");

            const D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{
                .pRootSignature = vdp2.drawBGsRootSig.GetPointer(),
                .CS = ToShaderBytecode(vdp2.drawBGsShader),
            };
            if (HRESULT hr = vdp2.drawBGsPSO.CreateCompute(device, psoDesc); FAILED(hr)) {
                return util::ErrorMessage{fmt::format(
                    "Could not build VDP2 layer rendering pipeline state object, error code {:X}", (uint32)hr)};
            }
            vdp2.drawBGsPSO->SetName(L"[Ymir-VDP2] Layer rendering pipeline state object");

            const D3D12_CPU_DESCRIPTOR_HANDLE srcHandles[] = {
                vdp2.layerRenderParamsSRV.cpuHandle, vdp2.rotRegsSRV.cpuHandle,      vdp2.vramSRV.cpuHandle,
                vdp2.cramColorSRV.cpuHandle,         vdp2.cramRotCoeffSRV.cpuHandle, vdp2.rotParamBasesSRV.cpuHandle,
                vdp2.spriteAttrsSRV.cpuHandle,       vdp2.layerOutUAV.cpuHandle,     vdp2.rbgLineColorOutUAV.cpuHandle,
                vdp2.colorCalcWindowUAV.cpuHandle,
            };
            std::array<UINT, std::size(srcHandles)> srcSizes{};
            srcSizes.fill(1);

            if (!resourceHeapAlloc.Allocate(vdp2.drawBGsDescs, std::size(srcHandles))) {
                return util::ErrorMessage{"Could not allocate VDP2 layer rendering descriptors"};
            }

            device->CopyDescriptors(1, &vdp2.drawBGsDescs.cpuHandle, &vdp2.drawBGsDescs.count, std::size(srcHandles),
                                    srcHandles, srcSizes.data(), resourceHeap.GetHeapType());
        }

        // Build shader, PSO and related resources for compositing layers
        {
            auto shaderBlobResult = LoadShader("src/vdp/cs_vdp2_compose.cso");
            if (!shaderBlobResult) {
                return util::ErrorMessage{fmt::format("Could not load VDP2 layer compositing compute shader: {}",
                                                      shaderBlobResult.Error().message)};
            }
            vdp2.composeShader.format = gpu::ShaderBytecodeFormat::DXIL;
            vdp2.composeShader.bytecode = shaderBlobResult.Value();
            vdp2.composeShader.entrypoint = kCSEntrypoint;
            auto result = gpu::ValidateShader(vdp2.composeShader);
            if (!result) {
                return util::ErrorMessage{
                    fmt::format("VDP2 layer compositing compute shader validation failed: {}", result.Error().message)};
            }

            auto rootSigBuilder = vdp2.composeRootSig.Builder();
            rootSigBuilder.Add32BitConstants(0, sizeof(VDP2CommonRenderParams) / sizeof(uint32));
            rootSigBuilder.AddDescriptorTable()
                .AddSRVs(6, 1) // NOTE: starting from 1 because SPIRV-Cross assumes buffers in t0 are constant
                .AddUAVs(1, 0);
            if (HRESULT hr = rootSigBuilder.Build(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not build VDP2 layer compositing root signature, error code {:X}", (uint32)hr)};
            }
            vdp2.composeRootSig->SetName(L"[Ymir-VDP2] Layer compositing root signature");

            const D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{
                .pRootSignature = vdp2.composeRootSig.GetPointer(),
                .CS = ToShaderBytecode(vdp2.composeShader),
            };
            if (HRESULT hr = vdp2.composePSO.CreateCompute(device, psoDesc); FAILED(hr)) {
                return util::ErrorMessage{fmt::format(
                    "Could not build VDP2 layer compositing pipeline state object, error code {:X}", (uint32)hr)};
            }
            vdp2.composePSO->SetName(L"[Ymir-VDP2] Layer compositing pipeline state object");

            const D3D12_CPU_DESCRIPTOR_HANDLE srcHandles[] = {
                vdp2.composeParamsSRV.cpuHandle,   vdp2.layerOutSRV.cpuHandle,    vdp2.lnclBackSRV.cpuHandle,
                vdp2.rbgLineColorOutSRV.cpuHandle, vdp2.spriteAttrsSRV.cpuHandle, vdp2.colorCalcWindowSRV.cpuHandle,
                vdp2.compositeOutUAV.cpuHandle,
            };
            std::array<UINT, std::size(srcHandles)> srcSizes{};
            srcSizes.fill(1);

            if (!resourceHeapAlloc.Allocate(vdp2.composeDescs, std::size(srcHandles))) {
                return util::ErrorMessage{"Could not allocate VDP2 layer compositing descriptors"};
            }

            device->CopyDescriptors(1, &vdp2.composeDescs.cpuHandle, &vdp2.composeDescs.count, std::size(srcHandles),
                                    srcHandles, srcSizes.data(), resourceHeap.GetHeapType());
        }

        Reset();

        {
            ID3D12DescriptorHeap *heaps[] = {resourceHeap.GetPointer()};
            vdp2.cmdList->SetDescriptorHeaps(std::size(heaps), heaps);
        }

        // TODO: upload full VDP1 and VDP2 states

        return {};
    }

    void Shutdown() {
        vdp2.frames.WaitForGPU(fence, cmdQueue);
    }

    util::ValueResult<std::vector<char>> LoadShader(const char *path) {
        if (!g_fsShaders.is_file(path)) {
            return util::ErrorMessage{fmt::format("Embedded file not found: {}", path)};
        }
        auto file = g_fsShaders.open(path);
        return std::vector<char>{file.begin(), file.end()};
    }

    // -----------------------------------------------------------------------------------------------------------------
    // State

    void Reset() {
        vdp2.vramDirty.SetAll();
        vdp2.cramDirty = true;
        vdp2.nextLayerRenderLine = 0;
        vdp2.nextComposeLine = 0;
        vdp2.rotRegsDirty = true;
        vdp2.layerRenderParamsDirty = true;
        vdp2.composeParamsDirty = true;
        VDP2UpdateEnabledLayers();
    }

    // -----------------------------------------------------------------------------------------------------------------
    // VDP2 rendering

    uint32 HRes = kDefaultResH;
    uint32 VRes = kDefaultResV;
    bool exclusiveMonitor = false;

    void VDP2CacheCRAMColor(uint32 address) {
        CRAMColorCache &colorCache = vdp2.cpuCRAMColorCache;
        switch (vdpState.regs2.vramControl.colorRAMMode) {
        case 0: { // RGB 5:5:5, half CRAM
            const auto value = vdpState.mem2.ReadCRAM<uint16>(address & ~1u);
            const Color555 color5{.u16 = value};
            const Color888 color8 = ConvertRGB555to888(color5);
            ColorR8G8B8A8 &color = colorCache[address >> 1u];
            color.r = color8.r;
            color.g = color8.g;
            color.b = color8.b;
            color.a = color8.msb;
            break;
        }
        case 1: { // RGB 5:5:5, full CRAM
            const auto value = vdpState.mem2.ReadCRAM<uint16>(address & ~1u);
            const Color555 color5{.u16 = value};
            const Color888 color8 = ConvertRGB555to888(color5);
            ColorR8G8B8A8 &color = colorCache[address >> 1u];
            color.r = color8.r;
            color.g = color8.g;
            color.b = color8.b;
            color.a = color8.msb;
            break;
        }
        case 2: [[fallthrough]]; // RGB 8:8:8, full CRAM
        case 3: [[fallthrough]]; // RGB 8:8:8, full CRAM
        default: {
            const auto value = vdpState.mem2.ReadCRAM<uint32>(address & ~3u);
            const Color888 color8{.u32 = value};
            ColorR8G8B8A8 &color = colorCache[address >> 2u];
            color.r = color8.r;
            color.g = color8.g;
            color.b = color8.b;
            color.a = color8.msb;
            break;
        }
        }
    }

    void VDP2CacheAllCRAMColors() {
        CRAMColorCache &colorCache = vdp2.cpuCRAMColorCache;
        switch (vdpState.regs2.vramControl.colorRAMMode) {
        case 0: // RGB 5:5:5, half CRAM
            for (uint32 i = 0; i < 1024; ++i) {
                const auto value = vdpState.mem2.ReadCRAM<uint16>(i * sizeof(uint16));
                const Color555 color5{.u16 = value};
                const Color888 color8 = ConvertRGB555to888(color5);
                colorCache[i].r = color8.r;
                colorCache[i].g = color8.g;
                colorCache[i].b = color8.b;
                colorCache[i].a = color8.msb;
            }
            break;
        case 1: // RGB 5:5:5, full CRAM
            for (uint32 i = 0; i < 2048; ++i) {
                const auto value = vdpState.mem2.ReadCRAM<uint16>(i * sizeof(uint16));
                const Color555 color5{.u16 = value};
                const Color888 color8 = ConvertRGB555to888(color5);
                colorCache[i].r = color8.r;
                colorCache[i].g = color8.g;
                colorCache[i].b = color8.b;
                colorCache[i].a = color8.msb;
            }
            break;
        case 2: [[fallthrough]]; // RGB 8:8:8, full CRAM
        case 3: [[fallthrough]]; // RGB 8:8:8, full CRAM
        default:
            for (uint32 i = 0; i < 1024; ++i) {
                const auto value = vdpState.mem2.ReadCRAM<uint32>(i * sizeof(uint32));
                const Color888 color8{.u32 = value};
                colorCache[i].r = color8.r;
                colorCache[i].g = color8.g;
                colorCache[i].b = color8.b;
                colorCache[i].a = color8.msb;
            }
            break;
        }
    }

    void VDP2WriteVRAM(uint32 address) {
        vdp2.vramDirty.Set(address >> VDP2Resources::kVRAMDirtyBitmapChunkSizeShift);
    }

    void VDP2WriteCRAM(uint32 address) {
        vdp2.cramDirty = true;
        VDP2CacheCRAMColor(address);
    }

    void VDP2WriteReg(uint32 address, uint16 value) {
        struct DirtyFlags {
            bool render = false;
            bool compose = false;
            bool rotRegs = false;
            bool enabledLayers = false;
            bool cram = false;
        };
        static constexpr auto kDirtyFlags = [] {
            std::array<DirtyFlags, 0x11E / sizeof(uint16) + 1> arr{};

            for (uint32 addr : {
                     0x000 /*TVMD*/,   0x002 /*EXTEN*/,  0x006 /*VRSIZE*/, 0x00E /*RAMCTL*/, 0x010 /*CYCA0L*/,
                     0x012 /*CYCA0U*/, 0x014 /*CYCA1L*/, 0x016 /*CYCA1U*/, 0x018 /*CYCB0L*/, 0x01A /*CYCB0U*/,
                     0x01C /*CYCB1L*/, 0x01E /*CYCB1U*/, 0x020 /*BGON*/,   0x022 /*MZCTL*/,  0x024 /*SFSEL*/,
                     0x026 /*SFCODE*/, 0x028 /*CHCTLA*/, 0x02A /*CHCTLB*/, 0x02C /*BMPNA*/,  0x02E /*BMPNB*/,
                     0x030 /*PNCNA*/,  0x032 /*PNCNB*/,  0x034 /*PNCNC*/,  0x036 /*PNCND*/,  0x038 /*PNCR*/,
                     0x03A /*PLSZ*/,   0x03C /*MPOFN*/,  0x03E /*MPOFR*/,  0x040 /*MPABN0*/, 0x042 /*MPCDN0*/,
                     0x044 /*MPABN1*/, 0x046 /*MPCDN1*/, 0x048 /*MPABN2*/, 0x04A /*MPCDN2*/, 0x04C /*MPABN3*/,
                     0x04E /*MPCDN3*/, 0x050 /*MPABRA*/, 0x052 /*MPCDRA*/, 0x054 /*MPEFRA*/, 0x056 /*MPGHRA*/,
                     0x058 /*MPIJRA*/, 0x05A /*MPKLRA*/, 0x05C /*MPMNRA*/, 0x05E /*MPOPRA*/, 0x060 /*MPABRB*/,
                     0x062 /*MPCDRB*/, 0x064 /*MPEFRB*/, 0x066 /*MPGHRB*/, 0x068 /*MPIJRB*/, 0x06A /*MPKLRB*/,
                     0x06C /*MPMNRB*/, 0x06E /*MPOPRB*/, 0x070 /*SCXIN0*/, 0x072 /*SCXDN0*/, 0x074 /*SCYIN0*/,
                     0x076 /*SCYDN0*/, 0x078 /*ZMXIN0*/, 0x07A /*ZMXDN0*/, 0x07C /*ZMYIN0*/, 0x07E /*ZMYDN0*/,
                     0x080 /*SCXIN1*/, 0x082 /*SCXDN1*/, 0x084 /*SCYIN1*/, 0x086 /*SCYDN1*/, 0x088 /*ZMXIN1*/,
                     0x08A /*ZMXDN1*/, 0x08C /*ZMYIN1*/, 0x08E /*ZMYDN1*/, 0x090 /*SCXN2*/,  0x092 /*SCYN2*/,
                     0x094 /*SCXN3*/,  0x096 /*SCYN3*/,  0x098 /*ZMCTL*/,  0x09A /*SCRCTL*/, 0x09C /*VCSTAU*/,
                     0x09E /*VCSTAL*/, 0x0A0 /*LSTA0U*/, 0x0A2 /*LSTA0L*/, 0x0A4 /*LSTA1U*/, 0x0A6 /*LSTA1L*/,
                     0x0A8 /*LCTAU*/,  0x0AA /*LCTAL*/,  0x0AC /*BKTAU*/,  0x0AE /*BKTAL*/,  0x0B0 /*RPMD*/,
                     0x0B8 /*OVPNRA*/, 0x0BA /*OVPNRB*/, 0x0C0 /*WPSX0*/,  0x0C2 /*WPSY0*/,  0x0C4 /*WPEX0*/,
                     0x0C6 /*WPEY0*/,  0x0C8 /*WPSX1*/,  0x0CA /*WPSY1*/,  0x0CC /*WPEX1*/,  0x0CE /*WPEY1*/,
                     0x0D0 /*WCTLA*/,  0x0D2 /*WCTLB*/,  0x0D4 /*WCTLC*/,  0x0D6 /*WCTLD*/,  0x0D8 /*LWTA0U*/,
                     0x0DA /*LWTA0L*/, 0x0DC /*LWTA1U*/, 0x0DE /*LWTA1L*/, 0x0E0 /*SPCTL*/,  0x0E2 /*SDCTL*/,
                     0x0E4 /*CRAOFA*/, 0x0E6 /*CRAOFB*/, 0x0E8 /*LNCLEN*/, 0x0EA /*SFPRMD*/, 0x0EC /*CCCTL*/,
                     0x0EE /*SFCCMD*/, 0x0F0 /*PRISA*/,  0x0F2 /*PRISB*/,  0x0F4 /*PRISC*/,  0x0F6 /*PRISD*/,
                     0x0F8 /*PRINA*/,  0x0FA /*PRINB*/,  0x0FC /*PRIR*/,
                 }) {
                arr[addr / sizeof(uint16)].render = true;
            }

            for (uint32 addr : {
                     0x000 /*TVMD*/,   0x006 /*VRSIZE*/, 0x020 /*BGON*/,  0x0E0 /*SPCTL*/, 0x0E2 /*SDCTL*/,
                     0x0E8 /*LNCLEN*/, 0x0EC /*CCCTL*/,  0x100 /*CCRSA*/, 0x102 /*CCRSB*/, 0x104 /*CCRSC*/,
                     0x106 /*CCRSD*/,  0x108 /*CCRNA*/,  0x10A /*CCRNB*/, 0x10C /*CCRR*/,  0x10E /*CCRLB*/,
                     0x110 /*CLOFEN*/, 0x112 /*CLOFSL*/, 0x114 /*COAR*/,  0x116 /*COAG*/,  0x118 /*COAB*/,
                     0x11A /*COBR*/,   0x11C /*COBG*/,   0x11E /*COBB*/,
                 }) {
                arr[addr / sizeof(uint16)].compose = true;
            }

            for (uint32 addr : {0x00E /*RAMCTL*/, 0x020 /*BGON*/, 0x0B2 /*RPRCTL*/, 0x0B4 /*KTCTL*/, 0x0B6 /*KTAOF*/,
                                0x0BC /*RPTAU*/, 0x0BE /*RPTAL*/}) {
                arr[addr / sizeof(uint16)].rotRegs = true;
            }

            for (uint32 addr : {0x020 /*BGON*/, 0x028 /*CHCTLA*/, 0x02A /*CHCTLB*/}) {
                arr[addr / sizeof(uint16)].enabledLayers = true;
            }

            arr[0x00E / sizeof(uint16) /*RAMCTL*/].cram = true;

            return arr;
        }();

        if (address <= 0x11E) {
            const auto &dirtyFlags = kDirtyFlags[address / sizeof(uint16)];
            vdp2.rotRegsDirty |= dirtyFlags.rotRegs;
            vdp2.layerRenderParamsDirty |= dirtyFlags.render;
            vdp2.composeParamsDirty |= dirtyFlags.compose;

            if (dirtyFlags.enabledLayers) {
                VDP2UpdateEnabledLayers();
            }

            if (dirtyFlags.cram) {
                vdp2.cramDirty = true;
                VDP2CacheAllCRAMColors();
            }
        }
    }

    util::VoidResult<> VDP2AllocateUploadBuffer(size_t size, size_t alignment, UploadAllocation &outAlloc) {
        // Sanity check: the upload buffer can hold transfers of this size
        YMIR_DEV_ASSERT(size < vdp2.uploadBuffer.GetSize());

        if (!vdp2.uploadBuffer.Allocate(size, alignment, fence.GetCompletedValue(), outAlloc)) {
            // Block until next fence completes and retry
            const UINT64 waitValue = vdp2.uploadBuffer.FindFenceValueForAllocation(size, alignment);
            fence.Wait(INFINITE, waitValue);

            // At this point, we really should be able to allocate the buffer
            if (!vdp2.uploadBuffer.Allocate(size, alignment, fence->GetCompletedValue(), outAlloc)) {
                // TODO: consider increasing the upload buffer size or allocating overflow buffers.
                // For now we'll just log the error and fail
                YMIR_DEV_CHECK();
                std::string message =
                    fmt::format("Failed to allocate {} bytes (align {}) in VDP2 upload buffer", size, 4);
                devlog::warn<grp::dx12_vdp2>("{}", message);
                return util::ErrorMessage{std::move(message)};
            }
        }
        return {};
    }

    [[nodiscard]] util::VoidResult<> VDP2FlushVRAM() {
        if (!vdp2.vramDirty) {
            return {};
        }

        ID3D12Resource *dstResource = vdp2.vramBuffer.GetPointer();
        ID3D12Resource *uploadBufferPtr = vdp2.uploadBuffer.GetBufferResource().GetPointer();

        // Emit barrier transition
        vdp2.barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_BARRIER_SYNC_COPY,
                                             D3D12_BARRIER_ACCESS_COPY_DEST);
        vdp2.barrierTracker.Flush(vdp2.cmdList);

        // Upload all modified VRAM chunks
        size_t pos, count = 0;
        UploadAllocation alloc{};
        for (pos = vdp2.vramDirty.FindNext(count); pos < vdp2.vramDirty.Size();
             pos = vdp2.vramDirty.FindNext(count, pos + count)) {
            const uint32 vramOffset = pos << VDP2Resources::kVRAMDirtyBitmapChunkSizeShift;
            const uint32 size = count << VDP2Resources::kVRAMDirtyBitmapChunkSizeShift;

            // Get upload buffer chunk for this transfer
            if (auto result = VDP2AllocateUploadBuffer(size, 4, alloc); !result) {
                return util::ErrorMessage{"Failed to allocate upload buffer for VDP2 VRAM chunk"};
            }

            // Upload VRAM chunk
            memcpy(alloc.data, &vdpState.mem2.VRAM[vramOffset], size);
            vdp2.cmdList->CopyBufferRegion(dstResource, vramOffset, uploadBufferPtr, alloc.offset, size);
        }
        vdp2.vramDirty.ClearAll();

        return {};
    }

    [[nodiscard]] util::VoidResult<> VDP2FlushCRAM() {
        if (!vdp2.cramDirty) {
            return {};
        }
        vdp2.cramDirty = false;

        ID3D12Resource *uploadBufferPtr = vdp2.uploadBuffer.GetBufferResource().GetPointer();

        UploadAllocation alloc{};

        // Update color cache
        {
            const size_t size = sizeof(CRAMColorCache);
            if (auto result = VDP2AllocateUploadBuffer(size, 4, alloc); !result) {
                return util::ErrorMessage{fmt::format("Failed to allocate upload buffer for VDP2 CRAM color cache: {}",
                                                      result.Error().message)};
            }
            memcpy(alloc.data, vdp2.cpuCRAMColorCache.data(), size);

            ID3D12Resource *dstResource = vdp2.cramColorBuffer.GetPointer();

            // Emit barrier transition
            vdp2.barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_BARRIER_SYNC_COPY,
                                                 D3D12_BARRIER_ACCESS_COPY_DEST);
            vdp2.barrierTracker.Flush(vdp2.cmdList);

            vdp2.cmdList->CopyBufferRegion(dstResource, 0, uploadBufferPtr, alloc.offset, size);
        }

        // Update rotation coefficients view
        const VDP2Regs &regs2 = vdpState.regs2;
        if ((regs2.bgEnabled[4] || regs2.bgEnabled[5]) && regs2.vramControl.colorRAMCoeffTableEnable) {
            const size_t size = kVDP2CRAMRotCoeffBufferSize;
            if (auto result = VDP2AllocateUploadBuffer(size, 4, alloc); !result) {
                return util::ErrorMessage{
                    fmt::format("Failed to allocate upload buffer for VDP2 CRAM rotation coefficients: {}",
                                result.Error().message)};
            }
            memcpy(alloc.data, &vdpState.mem2.CRAM[kVDP2CRAMSize / 2], size);

            ID3D12Resource *dstResource = vdp2.cramRotCoeffBuffer.GetPointer();

            // Emit barrier transition
            vdp2.barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_BARRIER_SYNC_COPY,
                                                 D3D12_BARRIER_ACCESS_COPY_DEST);
            vdp2.barrierTracker.Flush(vdp2.cmdList);

            vdp2.cmdList->CopyBufferRegion(dstResource, 0, uploadBufferPtr, alloc.offset, size);
        }

        return {};
    }

    void VDP2UpdateCommonRenderParams() {
        // vdp2.cpuCommonRenderParams.startY is updated by the line rendering functions.

        const VDP1Regs &regs1 = vdpState.regs1;
        const VDP2Regs &regs2 = vdpState.regs2;
        VDP2CommonRenderParams &params = vdp2.cpuCommonRenderParams;

        params.displayParams.displayEnable = regs2.TVMD.DISP;
        params.displayParams.borderColorMode = regs2.TVMD.BDCLMD;
        params.displayParams.interlaceMode = static_cast<HLSLuint>(regs2.TVMD.LSMDn);
        params.displayParams.oddField = regs2.TVSTAT.ODD;
        params.displayParams.exclusiveMonitor = exclusiveMonitor;
        params.displayParams.colorRAMMode = regs2.vramControl.colorRAMMode;
        params.displayParams.hiResH = bit::test<1>(regs2.TVMD.HRESOn);
        params.displayParams.palMode = regs2.TVSTAT.PAL;
        params.displayParams.hresMode = regs2.TVMD.HRESOn;
        params.displayParams.vresMode = regs2.TVMD.VRESOn;
        params.displayParams.dblInterlaceEnable = regs1.dblInterlaceEnable;
        params.displayParams.dblInterlaceDrawLine = regs1.dblInterlaceDrawLine;

        params.layerParams.layerEnabled = PackBools<HLSLuint>(vdpState.state2.layerEnabled);
        params.layerParams.bgEnabled = PackBools<HLSLuint>(regs2.bgEnabled);
        params.layerParams.lineColorEnableRBG0 = regs2.bgParams[0].lineColorScreenEnable;
        params.layerParams.lineColorEnableRBG1 = regs2.bgParams[1].lineColorScreenEnable;
        params.layerParams.mosaicH = regs2.mosaicH - 1;
        params.layerParams.mosaicV = regs2.mosaicV - 1;
        params.layerParams.rotParamMode = static_cast<HLSLuint>(regs2.commonRotParams.rotParamMode);

        params.spriteParams.rotate = regs1.fbRotEnable;
        params.spriteParams.pixel8Bits = regs1.pixel8Bits;
        params.spriteParams.type = regs2.spriteParams.type;
        params.spriteParams.fbSizeH = std::countr_zero(regs1.fbSizeH) - 9;
        params.spriteParams.inHalfResH = false;
        params.spriteParams.outHalfResH = false;
        if (!regs1.hdtvEnable && !regs1.fbRotEnable) {
            if (regs1.pixel8Bits) {
                params.spriteParams.inHalfResH = (regs2.TVMD.HRESOn & 0b110) == 0b000;
            } else {
                params.spriteParams.outHalfResH = (regs2.TVMD.HRESOn & 0b110) == 0b010;
            }
        }
        params.spriteParams.mixedFormat = regs2.spriteParams.mixedFormat;
        params.spriteParams.colorCalcEnable = regs2.spriteParams.colorCalcEnable;
        params.spriteParams.colorCalcValue = regs2.spriteParams.colorCalcValue;
        params.spriteParams.colorCalcCond = static_cast<HLSLuint>(regs2.spriteParams.colorCalcCond);
        params.spriteParams.colorDataOffset = regs2.spriteParams.colorDataOffset >> 8u;
        params.spriteParams.useSpriteWindow = regs2.spriteParams.useSpriteWindow;
        params.spriteParams.windowEnabled = regs2.spriteParams.spriteWindowEnabled;
        params.spriteParams.windowInverted = regs2.spriteParams.spriteWindowInverted;
        params.spriteParams.displayFB = vdpState.displayFB;

        params.spritePriosRatios.x = 0;
        params.spritePriosRatios.y = 0;
        for (uint32 i = 0; i < 4; i++) {
            params.spritePriosRatios.x |= regs2.spriteParams.priorities[i] << (8 * i);
            params.spritePriosRatios.x |= regs2.spriteParams.colorCalcRatios[i] << (8 * i + 3);

            params.spritePriosRatios.y |= regs2.spriteParams.priorities[i + 4] << (8 * i);
            params.spritePriosRatios.y |= regs2.spriteParams.colorCalcRatios[i + 4] << (8 * i + 3);
        }

        params.vcellScroll.tableAddress = regs2.vcellScrollTableAddress;
        params.vcellScroll.inc = regs2.vcellScrollInc >> 2u;

        params.windows.spriteWindowLogic = regs2.spriteParams.windowSet.logic == WindowLogic::And;
        params.windows.spriteW0Enable = regs2.spriteParams.windowSet.enabled[0];
        params.windows.spriteW0Invert = regs2.spriteParams.windowSet.inverted[0];
        params.windows.spriteW1Enable = regs2.spriteParams.windowSet.enabled[1];
        params.windows.spriteW1Invert = regs2.spriteParams.windowSet.inverted[1];

        params.windows.colorCalcWindowLogic = regs2.colorCalcParams.windowSet.logic == WindowLogic::And;
        params.windows.colorCalcW0Enable = regs2.colorCalcParams.windowSet.enabled[0];
        params.windows.colorCalcW0Invert = regs2.colorCalcParams.windowSet.inverted[0];
        params.windows.colorCalcW1Enable = regs2.colorCalcParams.windowSet.enabled[1];
        params.windows.colorCalcW1Invert = regs2.colorCalcParams.windowSet.inverted[1];
        params.windows.colorCalcSWEnable = regs2.colorCalcParams.windowSet.enabled[2];
        params.windows.colorCalcSWInvert = regs2.colorCalcParams.windowSet.inverted[2];

        params.enhancements.deinterlace = enhancements.deinterlace;
        params.enhancements.transparentMeshes = enhancements.transparentMeshes;

        // NOTE: this is uploaded as 32-bit root constants, not through the upload buffer.
        // No uploads or barriers are needed here.
    }

    util::VoidResult<> VDP2UpdateLayerRenderParams() {
        if (!vdp2.layerRenderParamsDirty) {
            return {};
        }
        vdp2.layerRenderParamsDirty = false;

        const VDP2Regs &regs2 = vdpState.regs2;

        // These can be either 0 or 8, but we'll condense them to single bits
        auto packVRAMDataOffsets = [](const std::array<uint32, 4> values) {
            uint32 value = 0;
            for (size_t i = 0; i < values.size(); ++i) {
                if (values[i] != 0u) {
                    value |= 1u << i;
                }
            }
            return value;
        };

        // NBG0-3
        for (int i = 0; i < 4; ++i) {
            const BGParams &bgParams = regs2.bgParams[i + 1];
            const NBGLayerState &bgState = vdpState.state2.nbgLayerStates[i];

            const bool bitmap = bgParams.bitmap;

            NBGParams &renderParams = vdp2.cpuLayerRenderParams.nbg[i];
            renderParams.base.enabled = regs2.bgEnabled[i];
            renderParams.base.enableTransparency = bgParams.enableTransparency;
            renderParams.base.bitmap = bgParams.bitmap;
            renderParams.base.priorityNumber = bgParams.priorityNumber;
            renderParams.base.priorityMode = static_cast<HLSLuint>(bgParams.priorityMode);
            renderParams.base.specialFunctionSelect = bgParams.specialFunctionSelect;
            renderParams.base.cellSizeShift = bgParams.cellSizeShift;
            renderParams.base.colorFormat = static_cast<HLSLuint>(bgParams.colorFormat);
            renderParams.base.cramOffset = bgParams.cramOffset;
            renderParams.base.supplScrollCharNum = bgParams.supplScrollCharNum;
            renderParams.base.supplPalNum = bitmap ? bgParams.supplBitmapPalNum : bgParams.supplScrollPalNum;
            renderParams.base.supplSpecialColorCalc =
                bitmap ? bgParams.supplBitmapSpecialColorCalc : bgParams.supplScrollSpecialColorCalc;
            renderParams.base.supplSpecialPriority =
                bitmap ? bgParams.supplBitmapSpecialPriority : bgParams.supplScrollSpecialPriority;
            renderParams.base.mosaicEnable = bgParams.mosaicEnable;
            renderParams.base.colorCalcEnable = bgParams.colorCalcEnable;
            renderParams.base.extChar = bgParams.extChar;
            renderParams.base.twoWordChar = bgParams.twoWordChar;
            renderParams.base.patNameAccess = PackBools<HLSLuint>(bgParams.patNameAccess);
            renderParams.base.charPatAccess = PackBools<HLSLuint>(bgParams.charPatAccess);
            renderParams.base.charPatDelay = PackBools<HLSLuint>(bgParams.charPatDelay);
            renderParams.base.vramDataOffset = packVRAMDataOffsets(bgParams.vramDataOffset);
            renderParams.base.specialColorCalcMode = static_cast<HLSLuint>(bgParams.specialColorCalcMode);
            renderParams.base.pageShift = {bgParams.pageShiftH, bgParams.pageShiftV};
            renderParams.base.bitmapSize = {bgParams.bitmapSizeH, bgParams.bitmapSizeV};
            renderParams.base.bitmapBaseAddress = bgParams.bitmapBaseAddress;
            renderParams.base.windowParams.base.windowLogicAnd = bgParams.windowSet.logic == WindowLogic::And;
            renderParams.base.windowParams.base.window0Enable = bgParams.windowSet.enabled[0];
            renderParams.base.windowParams.base.window0Invert = bgParams.windowSet.inverted[0];
            renderParams.base.windowParams.base.window1Enable = bgParams.windowSet.enabled[1];
            renderParams.base.windowParams.base.window1Invert = bgParams.windowSet.inverted[1];
            renderParams.base.windowParams.spriteWindowEnable = bgParams.windowSet.enabled[2];
            renderParams.base.windowParams.spriteWindowInvert = bgParams.windowSet.inverted[2];

            renderParams.scrollAmount = {bgParams.scrollAmountH, bgParams.scrollAmountV};
            renderParams.scrollInc = {bgState.scrollIncH, bgParams.scrollIncV};
            renderParams.pageBaseAddresses = bgParams.pageBaseAddresses;
            renderParams.vcellScrollEnable = bgParams.vcellScrollEnable;
            renderParams.lineScrollXEnable = bgParams.lineScrollXEnable;
            renderParams.lineScrollYEnable = bgParams.lineScrollYEnable;
            renderParams.lineZoomEnable = bgParams.lineZoomEnable;
            renderParams.lineScrollInterval = bgParams.lineScrollInterval;
            renderParams.lineScrollTableAddress = bgState.lineScrollTableAddress;
            renderParams.vcellScrollOffset = bgState.vcellScrollOffset;
            renderParams.vcellScrollDelay = bgState.vcellScrollDelay;
            renderParams.vcellScrollRepeat = bgState.vcellScrollRepeat;
        }

        // RBG0-1 / RotParam A-B
        for (int i = 0; i < 2; ++i) {
            const BGParams &bgParams = regs2.bgParams[i];
            const RotationParams &rotParams = regs2.rotParams[i];
            const RotationParamState &rotState = vdpState.state2.rotParamStates[i];

            const bool bitmap = bgParams.bitmap;

            RBGParams &renderParams = vdp2.cpuLayerRenderParams.rbg[i];
            renderParams.base.enabled = regs2.bgEnabled[i + 4];
            renderParams.base.enableTransparency = bgParams.enableTransparency;
            renderParams.base.bitmap = bgParams.bitmap;
            renderParams.base.priorityNumber = bgParams.priorityNumber;
            renderParams.base.priorityMode = static_cast<HLSLuint>(bgParams.priorityMode);
            renderParams.base.specialFunctionSelect = bgParams.specialFunctionSelect;
            renderParams.base.cellSizeShift = bgParams.cellSizeShift;
            renderParams.base.colorFormat = static_cast<HLSLuint>(bgParams.colorFormat);
            renderParams.base.cramOffset = bgParams.cramOffset;
            renderParams.base.supplScrollCharNum = bgParams.supplScrollCharNum;
            renderParams.base.supplPalNum = bitmap ? bgParams.supplBitmapPalNum : bgParams.supplScrollPalNum;
            renderParams.base.supplSpecialColorCalc =
                bitmap ? bgParams.supplBitmapSpecialColorCalc : bgParams.supplScrollSpecialColorCalc;
            renderParams.base.supplSpecialPriority =
                bitmap ? bgParams.supplBitmapSpecialPriority : bgParams.supplScrollSpecialPriority;
            renderParams.base.mosaicEnable = bgParams.mosaicEnable;
            renderParams.base.colorCalcEnable = bgParams.colorCalcEnable;
            renderParams.base.extChar = bgParams.extChar;
            renderParams.base.twoWordChar = bgParams.twoWordChar;
            renderParams.base.patNameAccess = PackBools<HLSLuint>(bgParams.patNameAccess);
            renderParams.base.charPatAccess = PackBools<HLSLuint>(bgParams.charPatAccess);
            renderParams.base.charPatDelay = PackBools<HLSLuint>(bgParams.charPatDelay);
            renderParams.base.vramDataOffset = packVRAMDataOffsets(bgParams.vramDataOffset);
            renderParams.base.specialColorCalcMode = static_cast<HLSLuint>(bgParams.specialColorCalcMode);
            renderParams.base.pageShift = {rotParams.pageShiftH, rotParams.pageShiftV};
            renderParams.base.bitmapBaseAddress = rotParams.bitmapBaseAddress;
            renderParams.base.windowParams.base.windowLogicAnd = bgParams.windowSet.logic == WindowLogic::And;
            renderParams.base.windowParams.base.window0Enable = bgParams.windowSet.enabled[0];
            renderParams.base.windowParams.base.window0Invert = bgParams.windowSet.inverted[0];
            renderParams.base.windowParams.base.window1Enable = bgParams.windowSet.enabled[1];
            renderParams.base.windowParams.base.window1Invert = bgParams.windowSet.inverted[1];
            renderParams.base.windowParams.spriteWindowEnable = bgParams.windowSet.enabled[2];
            renderParams.base.windowParams.spriteWindowInvert = bgParams.windowSet.inverted[2];

            renderParams.screenOverProcess = static_cast<HLSLuint>(rotParams.screenOverProcess);
            renderParams.screenOverPatternName = rotParams.screenOverPatternName;
            renderParams.pageBaseAddresses[0] = vdpState.state2.rbgPageBaseAddresses[0][i];
            renderParams.pageBaseAddresses[1] = vdpState.state2.rbgPageBaseAddresses[1][i];
        }

        // Windows 0 and 1
        for (int i = 0; i < 2; ++i) {
            const WindowParams &windowParams = regs2.windowParams[i];
            VDP2GlobalWindowParams &renderParams = vdp2.cpuLayerRenderParams.windows[i];
            renderParams.start = {windowParams.startX, windowParams.startY};
            renderParams.end = {windowParams.endX, windowParams.endY};
            renderParams.lineWindowTableAddress = windowParams.lineWindowTableAddress;
            renderParams.lineWindowTableEnable = windowParams.lineWindowTableEnable;
        }

        VDP2LayerWindowParams &rotWindows = vdp2.cpuLayerRenderParams.rotWindows;
        rotWindows.windowLogicAnd = regs2.commonRotParams.windowSet.logic == WindowLogic::And;
        rotWindows.window0Enable = regs2.commonRotParams.windowSet.enabled[0];
        rotWindows.window0Invert = regs2.commonRotParams.windowSet.inverted[0];
        rotWindows.window1Enable = regs2.commonRotParams.windowSet.enabled[1];
        rotWindows.window1Invert = regs2.commonRotParams.windowSet.inverted[1];

        VDP2LineBackScreenParams &lnclParams = vdp2.cpuLayerRenderParams.lineScreenParams;
        lnclParams.baseAddress = regs2.lineScreenParams.baseAddress;
        lnclParams.perLine = regs2.lineScreenParams.perLine;

        VDP2LineBackScreenParams &backParams = vdp2.cpuLayerRenderParams.backScreenParams;
        backParams.baseAddress = regs2.backScreenParams.baseAddress;
        backParams.perLine = regs2.backScreenParams.perLine;

        // Special function codes
        vdp2.cpuLayerRenderParams.specialFunctionCodes =
            PackBools<uint32>(regs2.specialFunctionCodes[0].colorMatches) |
            (PackBools<uint32>(regs2.specialFunctionCodes[1].colorMatches) << 8u);

        // Update buffer
        {
            ID3D12Resource *dstResource = vdp2.layerRenderParamsBuffer.GetPointer();
            ID3D12Resource *uploadBufferPtr = vdp2.uploadBuffer.GetBufferResource().GetPointer();
            const size_t size = sizeof(vdp2.cpuLayerRenderParams);
            UploadAllocation alloc{};
            if (auto result = VDP2AllocateUploadBuffer(size, 4, alloc); !result) {
                return util::ErrorMessage{
                    fmt::format("Failed to allocate upload buffer for VDP2 layer rendering parameters: {}",
                                result.Error().message)};
            }
            memcpy(alloc.data, &vdp2.cpuLayerRenderParams, size);

            // Emit barrier transition
            vdp2.barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_BARRIER_SYNC_COPY,
                                                 D3D12_BARRIER_ACCESS_COPY_DEST);
            vdp2.barrierTracker.Flush(vdp2.cmdList);

            vdp2.cmdList->CopyBufferRegion(dstResource, 0, uploadBufferPtr, alloc.offset, size);
        }

        return {};
    }

    util::VoidResult<> VDP2UpdateRotRegs() {
        if (!vdp2.rotRegsDirty) {
            return {};
        }
        vdp2.rotRegsDirty = false;

        VDP2Regs &regs2 = vdpState.regs2;
        if (!regs2.bgEnabled[4] && !regs2.bgEnabled[5]) {
            // Skip if no RBGs are enabled
            return {};
        }

        for (uint32 i = 0; i < 2; ++i) {
            VDP2RotRegs &dst = vdp2.cpuRotRegs[i];
            RotationParams &src = regs2.rotParams[i];
            const auto &vramCtl = regs2.vramControl;

            auto isCoeff = [](RotDataBankSel sel) { return sel == RotDataBankSel::Coefficients; };

            dst.coeffTableEnable = src.coeffTableEnable;
            dst.coeffUseLineColorData = src.coeffUseLineColorData;
            dst.coeffTableCRAM = vramCtl.colorRAMCoeffTableEnable;
            dst.coeffDataSize = src.coeffDataSize;
            dst.coeffDataMode = static_cast<HLSLuint>(src.coeffDataMode);
            dst.coeffDataAccess = 0;
            if (isCoeff(vramCtl.rotDataBankSelA0)) {
                dst.coeffDataAccess |= 1u << 0u;
            }
            if (isCoeff(vramCtl.partitionVRAMA ? vramCtl.rotDataBankSelA1 : vramCtl.rotDataBankSelA0)) {
                dst.coeffDataAccess |= 1u << 1u;
            }
            if (isCoeff(vramCtl.rotDataBankSelB0)) {
                dst.coeffDataAccess |= 1u << 2u;
            }
            if (isCoeff(vramCtl.partitionVRAMB ? vramCtl.rotDataBankSelB1 : vramCtl.rotDataBankSelB0)) {
                dst.coeffDataAccess |= 1u << 3u;
            }
            dst.coeffDataPerDot = vramCtl.perDotRotationCoeffs;
        }

        // Update buffer
        {
            ID3D12Resource *dstResource = vdp2.rotRegsBuffer.GetPointer();
            ID3D12Resource *uploadBufferPtr = vdp2.uploadBuffer.GetBufferResource().GetPointer();
            const size_t size = sizeof(vdp2.cpuRotRegs);

            UploadAllocation alloc{};
            if (auto result = VDP2AllocateUploadBuffer(size, 4, alloc); !result) {
                return util::ErrorMessage{fmt::format(
                    "Failed to allocate upload buffer for VDP2 rotation registers: {}", result.Error().message)};
            }
            memcpy(alloc.data, &vdp2.cpuRotRegs, size);

            // Emit barrier transition
            vdp2.barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_BARRIER_SYNC_COPY,
                                                 D3D12_BARRIER_ACCESS_COPY_DEST);
            vdp2.barrierTracker.Flush(vdp2.cmdList);

            vdp2.cmdList->CopyBufferRegion(dstResource, 0, uploadBufferPtr, alloc.offset, size);
        }

        return {};
    }

    util::VoidResult<> VDP2UpdateComposeParams() {
        if (!vdp2.composeParamsDirty) {
            return {};
        }
        vdp2.composeParamsDirty = false;

        const VDP2Regs &regs2 = vdpState.regs2;

        auto &params = vdp2.cpuComposeParams;
        params.colorCalcEnable = 0                                               //
                                 | (regs2.spriteParams.colorCalcEnable << 0)     //
                                 | (regs2.bgParams[0].colorCalcEnable << 1)      //
                                 | (regs2.bgParams[1].colorCalcEnable << 2)      //
                                 | (regs2.bgParams[2].colorCalcEnable << 3)      //
                                 | (regs2.bgParams[3].colorCalcEnable << 4)      //
                                 | (regs2.bgParams[4].colorCalcEnable << 5)      //
                                 | (regs2.backScreenParams.colorCalcEnable << 6) //
                                 | (regs2.lineScreenParams.colorCalcEnable << 7) //
            ;
        params.extendedColorCalc = regs2.colorCalcParams.extendedColorCalcEnable && regs2.TVMD.HRESOn < 2;
        params.useAdditiveBlend = regs2.colorCalcParams.useAdditiveBlend;
        params.useSecondScreenRatio = regs2.colorCalcParams.useSecondScreenRatio;
        params.colorOffsetEnable = PackBools<HLSLuint>(regs2.colorOffsetEnable);
        params.colorOffsetSelect = PackBools<HLSLuint>(regs2.colorOffsetSelect);
        params.lineColorEnable = 0                                                 //
                                 | (regs2.spriteParams.lineColorScreenEnable << 0) //
                                 | (regs2.bgParams[0].lineColorScreenEnable << 1)  //
                                 | (regs2.bgParams[1].lineColorScreenEnable << 2)  //
                                 | (regs2.bgParams[2].lineColorScreenEnable << 3)  //
                                 | (regs2.bgParams[3].lineColorScreenEnable << 4)  //
                                 | (regs2.bgParams[4].lineColorScreenEnable << 5)  //
            ;

        params.colorOffsetA.r = bit::sign_extend<9>(regs2.colorOffset[0].r);
        params.colorOffsetA.g = bit::sign_extend<9>(regs2.colorOffset[0].g);
        params.colorOffsetA.b = bit::sign_extend<9>(regs2.colorOffset[0].b);

        params.colorOffsetB.r = bit::sign_extend<9>(regs2.colorOffset[1].r);
        params.colorOffsetB.g = bit::sign_extend<9>(regs2.colorOffset[1].g);
        params.colorOffsetB.b = bit::sign_extend<9>(regs2.colorOffset[1].b);

        for (int i = 0; i < 5; ++i) {
            params.bgColorCalcRatios[i] = regs2.bgParams[0].colorCalcRatio;
        }
        params.backLineColorCalcRatios[0] = regs2.backScreenParams.colorCalcRatio;
        params.backLineColorCalcRatios[1] = regs2.lineScreenParams.colorCalcRatio;

        // Update buffer
        {
            ID3D12Resource *dstResource = vdp2.composeParamsBuffer.GetPointer();
            ID3D12Resource *uploadBufferPtr = vdp2.uploadBuffer.GetBufferResource().GetPointer();
            const size_t size = sizeof(vdp2.cpuComposeParams);

            UploadAllocation alloc{};
            if (auto result = VDP2AllocateUploadBuffer(size, 4, alloc); !result) {
                return util::ErrorMessage{
                    fmt::format("Failed to allocate upload buffer for VDP2 layer compositing parameters: {}",
                                result.Error().message)};
            }
            memcpy(alloc.data, &vdp2.cpuComposeParams, size);

            // Emit barrier transition
            vdp2.barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_BARRIER_SYNC_COPY,
                                                 D3D12_BARRIER_ACCESS_COPY_DEST);
            vdp2.barrierTracker.Flush(vdp2.cmdList);

            vdp2.cmdList->CopyBufferRegion(dstResource, 0, uploadBufferPtr, alloc.offset, size);
        }

        return {};
    }

    void VDP2CalcAccessPatterns() {
        vdp2.layerRenderParamsDirty |= vdpState.regs2.accessPatternsDirty;
        vdpState.state2.CalcAccessPatterns(vdpState.regs2, vdp2.accessPatternsConfig);
    }

    void VDP2InitNBGs() {
        const VDP2Regs &regs2 = vdpState.regs2;

        for (uint32 i = 0; i < 4; ++i) {
            const BGParams &bgParams = regs2.bgParams[i + 1];
            NBGLayerState &nbgState = vdpState.state2.nbgLayerStates[i];

            // NOTE: fracScrollX/Y are computed from scratch in the shader
            nbgState.scrollIncH = bgParams.scrollIncH;

            if (i < 2) {
                nbgState.lineScrollTableAddress = bgParams.lineScrollTableAddress;
            }
        }

        vdp2.layerRenderParamsDirty = true;
    }

    void VDP2UpdateEnabledLayers() {
        vdpState.state2.UpdateEnabledBGs(vdpState.regs2, vdp2.debugRenderOptions);
    }

    void VDP2CalcVCellScrollDelay() {
        vdp2.layerRenderParamsDirty |= vdpState.regs2.accessPatternsDirty;
        vdpState.state2.CalcVCellScrollDelay(vdpState.regs2);
    }

    void VDP2DrawLineColorBackScreens(uint32 y) {
        const VDP2Regs &regs = vdpState.regs2;

        // Read line color screen color
        {
            const LineBackScreenParams &lineParams = regs.lineScreenParams;
            const uint32 lnclY = lineParams.perLine ? y : 0;
            const uint32 address = lineParams.baseAddress + lnclY * sizeof(uint16);
            const uint32 cramAddress = vdpState.mem2.ReadVRAM<uint16>(address);
            vdp2.cpuLnclBack[0][y] = vdp2.cpuCRAMColorCache[cramAddress & 0x7FF];
        }

        // Read back screen color
        {
            const LineBackScreenParams &backParams = regs.backScreenParams;
            const uint32 backY = backParams.perLine ? y : 0;
            const uint32 address = backParams.baseAddress + backY * sizeof(Color555);
            const Color555 color5{.u16 = vdpState.mem2.ReadVRAM<uint16>(address)};
            const Color888 color8 = ConvertRGB555to888(color5);
            vdp2.cpuLnclBack[1][y].r = color8.r;
            vdp2.cpuLnclBack[1][y].g = color8.g;
            vdp2.cpuLnclBack[1][y].b = color8.b;
            vdp2.cpuLnclBack[1][y].a = color8.msb;
        }
    }

    util::VoidResult<> VDP2UploadLineColorBackScreens() {
        ID3D12Resource *dstResource = vdp2.lnclBackBuffer.GetPointer();
        ID3D12Resource *uploadBufferPtr = vdp2.uploadBuffer.GetBufferResource().GetPointer();
        const size_t size = sizeof(vdp2.cpuLnclBack);

        UploadAllocation alloc{};
        if (auto result = VDP2AllocateUploadBuffer(size, 4, alloc); !result) {
            return util::ErrorMessage{
                fmt::format("Failed to allocate upload buffer for VDP2 LNCL/BACK screens: {}", result.Error().message)};
        }
        memcpy(alloc.data, &vdp2.cpuLnclBack, size);

        // Emit barrier transition
        vdp2.barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_BARRIER_SYNC_COPY,
                                             D3D12_BARRIER_ACCESS_COPY_DEST);
        vdp2.barrierTracker.Flush(vdp2.cmdList);

        vdp2.cmdList->CopyBufferRegion(dstResource, 0, uploadBufferPtr, alloc.offset, size);

        return {};
    }

    void VDP2UpdateRotationParameterBases(uint16 y) {
        VDP2Regs &regs2 = vdpState.regs2;
        if (!regs2.bgEnabled[4] && !regs2.bgEnabled[5]) {
            // Skip if no RBGs are enabled
            return;
        }

        const bool readAll = y == 0;

        const uint32 baseAddress = regs2.commonRotParams.baseAddress & 0xFFF7C; // mask bit 6 (shifted left by 1)
        for (uint32 i = 0; i < 2; ++i) {
            VDP2RotParamBase &base = vdp2.cpuRotParamBases[i * kMaxNormalResV + y];
            RotationParams &src = regs2.rotParams[i];

            const uint32 address = baseAddress + i * 0x80;

            base.tableAddress = address;

            if (readAll || src.readXst) {
                base.Xst = bit::extract_signed<6, 28, sint32>(vdpState.mem2.ReadVRAM<uint32>(address + 0x00));
                src.readXst = false;
            } else {
                const VDP2RotParamBase &prevBase = vdp2.cpuRotParamBases[i * kMaxNormalResV + y - 1];
                base.Xst =
                    prevBase.Xst + bit::extract_signed<6, 18, sint32>(vdpState.mem2.ReadVRAM<uint32>(address + 0x0C));
            }

            if (readAll || src.readYst) {
                base.Yst = bit::extract_signed<6, 28, sint32>(vdpState.mem2.ReadVRAM<uint32>(address + 0x04));
                src.readYst = false;
            } else {
                const VDP2RotParamBase &prevBase = vdp2.cpuRotParamBases[i * kMaxNormalResV + y - 1];
                base.Yst =
                    prevBase.Yst + bit::extract_signed<6, 18, sint32>(vdpState.mem2.ReadVRAM<uint32>(address + 0x10));
            }

            if (readAll || src.readKAst) {
                const uint32 KAst = bit::extract<6, 31>(vdpState.mem2.ReadVRAM<uint32>(address + 0x54));
                base.KA = src.coeffTableAddressOffset + KAst;
                src.readKAst = false;
            } else {
                const VDP2RotParamBase &prevBase = vdp2.cpuRotParamBases[i * kMaxNormalResV + y - 1];
                base.KA = prevBase.KA + bit::extract_signed<6, 25>(vdpState.mem2.ReadVRAM<uint32>(address + 0x58));
            }
        }
    }

    util::VoidResult<> VDP2UploadRotationParameterBases() {
        ID3D12Resource *dstResource = vdp2.rotParamBasesBuffer.GetPointer();
        ID3D12Resource *uploadBufferPtr = vdp2.uploadBuffer.GetBufferResource().GetPointer();
        const size_t size = sizeof(vdp2.cpuRotParamBases);

        UploadAllocation alloc{};
        if (auto result = VDP2AllocateUploadBuffer(size, 4, alloc); !result) {
            return util::ErrorMessage{fmt::format(
                "Failed to allocate upload buffer for VDP2 rotation parameter bases: {}", result.Error().message)};
        }
        memcpy(alloc.data, &vdp2.cpuRotParamBases, size);

        // Emit barrier transition
        vdp2.barrierTracker.TransitionBuffer(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_BARRIER_SYNC_COPY,
                                             D3D12_BARRIER_ACCESS_COPY_DEST);
        vdp2.barrierTracker.Flush(vdp2.cmdList);

        vdp2.cmdList->CopyBufferRegion(dstResource, 0, uploadBufferPtr, alloc.offset, size);

        return {};
    }

    void VDP2UpdateState() {
        if (auto result = VDP2FlushVRAM(); !result) {
            devlog::warn<grp::dx12_vdp2>("VDP2 VRAM flush failed: {}", result.Error().message);
        }
        if (auto result = VDP2FlushCRAM(); !result) {
            devlog::warn<grp::dx12_vdp2>("VDP2 CRAM flush failed: {}", result.Error().message);
        }
        VDP2UpdateCommonRenderParams();
        VDP2UpdateLayerRenderParams();
        VDP2UpdateRotRegs();
        VDP2UpdateComposeParams();
    }

    void VDP2BeginFrame() {
        auto &cmdList = vdp2.cmdList;

        vdp2.nextLayerRenderLine = 0;
        vdp2.nextComposeLine = 0;

        VDP2CalcAccessPatterns();
        VDP2InitNBGs();

        VDP2UpdateState();
    }

    void VDP2RenderLayerLines(uint32 y) {
        // Bail out if there's nothing to render
        if (y < vdp2.nextLayerRenderLine) {
            return;
        }

        auto &cmdList = vdp2.cmdList;

        const bool deinterlace = enhancements.deinterlace && vdpState.regs2.TVMD.IsInterlaced();
        const uint32 yShift = deinterlace ? 1u : 0u;

        const uint32 startY = vdp2.nextLayerRenderLine;

        // Determine how many lines to draw and update next scanline counter
        const uint32 baseNumLines = y - startY + 1;
        const uint32 numLines = baseNumLines << yShift;
        vdp2.nextLayerRenderLine = y + 1;

        vdp2.cpuCommonRenderParams.startY = startY << yShift;

        // ---------------------------------------------------------------------

        // Transition resources for rendering layers
        vdp2.barrierTracker.TransitionBuffer(vdp2.layerRenderParamsBuffer.GetPointer(),
                                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                             D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        vdp2.barrierTracker.TransitionBuffer(vdp2.vramBuffer.GetPointer(),
                                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                             D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        vdp2.barrierTracker.TransitionBuffer(vdp2.cramColorBuffer.GetPointer(),
                                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                             D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);

        // Compute rotation parameters if any RBGs are enabled
        if (vdpState.regs2.bgEnabled[4] || vdpState.regs2.bgEnabled[5]) {
            VDP2UploadRotationParameterBases();
        }

        // ---------------------------------------------------------------------

        // Transition resources for drawing the sprite layer
        if (vdp2.cpuCommonRenderParams.spriteParams.rotate) {
            vdp2.barrierTracker.TransitionBuffer(
                vdp2.rotRegsBuffer.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            vdp2.barrierTracker.TransitionBuffer(
                vdp2.rotParamBasesBuffer.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        }
        vdp2.barrierTracker.TransitionTexture(vdp2.layerOutTexture.GetPointer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                              D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
                                              D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS);
        vdp2.barrierTracker.TransitionTexture(vdp2.spriteAttrsTexture.GetPointer(),
                                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                                              D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
                                              D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS);
        vdp2.barrierTracker.Flush(cmdList);

        // Draw sprite layer
        cmdList->SetPipelineState(vdp2.drawSpritePSO.GetPointer());
        cmdList->SetComputeRootSignature(vdp2.drawSpriteRootSig.GetPointer());
        cmdList->SetComputeRoot32BitConstants(0, sizeof(vdp2.cpuCommonRenderParams) / sizeof(uint32),
                                              &vdp2.cpuCommonRenderParams, 0);
        cmdList->SetComputeRootDescriptorTable(1, vdp2.drawSpriteDescs.gpuHandle);
        cmdList->Dispatch((HRes + 31) / 32, numLines, enhancements.transparentMeshes ? 2 : 1);

        // ---------------------------------------------------------------------

        // Transition resources for drawing background layers
        if (vdpState.regs2.bgEnabled[4] || vdpState.regs2.bgEnabled[5]) {
            vdp2.barrierTracker.TransitionBuffer(
                vdp2.cramRotCoeffBuffer.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            vdp2.barrierTracker.TransitionBuffer(
                vdp2.rotRegsBuffer.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            vdp2.barrierTracker.TransitionBuffer(
                vdp2.rotParamBasesBuffer.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        }
        vdp2.barrierTracker.TransitionTexture(
            vdp2.spriteAttrsTexture.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COMMON);
        vdp2.barrierTracker.TransitionTexture(vdp2.layerOutTexture.GetPointer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                              D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
                                              D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS);
        vdp2.barrierTracker.TransitionTexture(vdp2.rbgLineColorOutTexture.GetPointer(),
                                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                                              D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
                                              D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS);
        vdp2.barrierTracker.TransitionTexture(vdp2.colorCalcWindowTexture.GetPointer(),
                                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                                              D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
                                              D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS);
        vdp2.barrierTracker.Flush(cmdList);

        // Draw NBGs and RBGs
        cmdList->SetPipelineState(vdp2.drawBGsPSO.GetPointer());
        cmdList->SetComputeRootSignature(vdp2.drawBGsRootSig.GetPointer());
        cmdList->SetComputeRoot32BitConstants(0, sizeof(vdp2.cpuCommonRenderParams) / sizeof(uint32),
                                              &vdp2.cpuCommonRenderParams, 0);
        cmdList->SetComputeRootDescriptorTable(1, vdp2.drawBGsDescs.gpuHandle);
        cmdList->Dispatch(HRes / 32, numLines, 1);
    }

    void VDP2ComposeLines(uint32 y) {
        // Bail out if there's nothing to render
        if (y < vdp2.nextComposeLine) {
            return;
        }

        auto &cmdList = vdp2.cmdList;

        vdp2.cpuCommonRenderParams.startY = vdp2.nextComposeLine;
        VDP2UploadLineColorBackScreens();

        // Determine how many lines to draw and update next scanline counter
        const uint32 numLines = y - vdp2.nextComposeLine + 1;
        vdp2.nextComposeLine = y + 1;

        // ---------------------------------------------------------------------

        // Transition resources for compositing layers
        vdp2.barrierTracker.TransitionBuffer(vdp2.composeParamsBuffer.GetPointer(),
                                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                             D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        vdp2.barrierTracker.TransitionTexture(
            vdp2.layerOutTexture.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COMMON);
        vdp2.barrierTracker.TransitionTexture(
            vdp2.rbgLineColorOutTexture.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COMMON);
        vdp2.barrierTracker.TransitionTexture(
            vdp2.colorCalcWindowTexture.GetPointer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_COMMON);
        vdp2.barrierTracker.TransitionBuffer(vdp2.lnclBackBuffer.GetPointer(),
                                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                             D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        vdp2.barrierTracker.Flush(cmdList);

        // Compose final image
        cmdList->SetPipelineState(vdp2.composePSO.GetPointer());
        cmdList->SetComputeRootSignature(vdp2.composeRootSig.GetPointer());
        cmdList->SetComputeRoot32BitConstants(0, sizeof(vdp2.cpuCommonRenderParams) / sizeof(uint32),
                                              &vdp2.cpuCommonRenderParams, 0);
        cmdList->SetComputeRootDescriptorTable(1, vdp2.composeDescs.gpuHandle);
        cmdList->Dispatch((HRes + 31) / 32, numLines, 1);
    }

    void VDP2RenderLine(uint32 y) {
        VDP2CalcVCellScrollDelay();
        VDP2DrawLineColorBackScreens(y);
        VDP2UpdateRotationParameterBases(y);
        vdpState.state2.UpdateRotationPageBaseAddresses(vdpState.regs2);

        // When Y=0, the changes happened during vblank (or, more precisely, between the last Y of the previous frame
        // and the first line of this frame). Otherwise, the changes happened between Y-1 and Y. Therefore, we need to
        // render lines up to Y-1 then sync the state, unless Y=0, in which case we just sync the state.

        if (y > 0) {
            const bool renderLayers = vdp2.vramDirty || vdp2.cramDirty || vdp2.rotRegsDirty ||
                                      vdp2.layerRenderParamsDirty || vdp2.composeParamsDirty;
            const bool compose = vdp2.composeParamsDirty;
            if (renderLayers) {
                VDP2RenderLayerLines(y - 1);
            }
            if (compose) {
                VDP2ComposeLines(y - 1);
            }
        }

        VDP2UpdateState();
    }

    void VDP2EndFrame() {
        const uint32 vShift = vdpState.regs2.TVMD.IsInterlaced() ? 1u : 0u;
        const uint32 vres = VRes >> vShift;
        VDP2RenderLayerLines(vres - 1);
        VDP2ComposeLines(VRes - 1);

        auto &cmdList = vdp2.cmdList;

        // Close and submit command list
        cmdList->Close();
        cmdQueue->ExecuteCommandLists(1, cmdList.GetAddressOfBase());

        // Advance frame
        FrameContext &currFrame = vdp2.frames.GetCurrentFrame();
        vdp2.uploadBuffer.EndFrame(vdp2.frames.currFenceValue + 1);
        vdp2.frames.MoveToNextFrame(fence, cmdQueue);

        // Setup command list
        FrameContext &nextFrame = vdp2.frames.GetCurrentFrame();
        ID3D12DescriptorHeap *heaps[] = {resourceHeap.GetPointer()};
        cmdList->Reset(nextFrame.cmdAlloc.GetPointer(), nullptr);
        cmdList->SetDescriptorHeaps(std::size(heaps), heaps);
    }
};

// ---------------------------------------------------------------------------------------------------------------------

Direct3D12VDPRenderer::Direct3D12VDPRenderer(VDPState &state, const config::VDP2DebugRender &vdp2DebugRenderOptions,
                                             const config::VDP2AccessPatternsConfig &vdp2AccessPatternsConfig)
    : HardwareVDPRendererBase(VDPRendererType::Direct3D12)
    , m_impl(std::make_unique<Impl>(state, vdp2AccessPatternsConfig, vdp2DebugRenderOptions, m_enhancements)) {}

Direct3D12VDPRenderer::~Direct3D12VDPRenderer() {
    m_impl->Shutdown();
}

util::VoidResult<> Direct3D12VDPRenderer::Initialize(ID3D12Device *device) {
    return m_impl->Initialize(device);
}

util::ObjectResult<Direct3D12VDPRenderer>
Direct3D12VDPRenderer::Create(VDPState &state, const config::VDP2DebugRender &vdp2DebugRenderOptions,
                              const config::VDP2AccessPatternsConfig &vdp2AccessPatternsConfig, ID3D12Device *device) {
    if (device == nullptr) {
        return util::ErrorMessage{"No Direct3D 12 device instance provided"};
    }
    std::unique_ptr<Direct3D12VDPRenderer> renderer{
        new Direct3D12VDPRenderer(state, vdp2DebugRenderOptions, vdp2AccessPatternsConfig)};
    util::VoidResult<> result = renderer->Initialize(device);
    if (!result) {
        return result.Error();
    }
    return renderer;
}

// -----------------------------------------------------------------------------
// Configuration

bool Direct3D12VDPRenderer::IsValid() const {
    return true;
}

void Direct3D12VDPRenderer::Reset(bool hard) {
    m_impl->Reset();
}

// -----------------------------------------------------------------------------
// Save states

void Direct3D12VDPRenderer::PreSaveStateSync() {}

void Direct3D12VDPRenderer::PostLoadStateSync() {
    // TODO: m_impl->vdp1.vramDirty.SetAll();

    m_impl->VDP2UpdateEnabledLayers();
    m_impl->vdp2.vramDirty.SetAll();
    m_impl->vdp2.cramDirty = true;
    m_impl->vdp2.rotRegsDirty = true;
    m_impl->vdp2.layerRenderParamsDirty = true;
    m_impl->vdp2.composeParamsDirty = true;
}

void Direct3D12VDPRenderer::SaveState(savestate::VDPSaveState::VDPRendererSaveState &state) {}

bool Direct3D12VDPRenderer::ValidateState(const savestate::VDPSaveState::VDPRendererSaveState &state) const {
    return true;
}

void Direct3D12VDPRenderer::LoadState(const savestate::VDPSaveState::VDPRendererSaveState &state) {}

// -----------------------------------------------------------------------------
// VDP1 memory and register writes

void Direct3D12VDPRenderer::VDP1WriteVRAM(uint32 address, uint8 value) {
    // TODO: mark as dirty
}

void Direct3D12VDPRenderer::VDP1WriteVRAM(uint32 address, uint16 value) {
    // TODO: mark as dirty
}

void Direct3D12VDPRenderer::VDP1SyncFB() {
    // TODO: wait until VDP1 rendering has caught up
}

void Direct3D12VDPRenderer::VDP1DebugSyncFB() {
    // TODO: loosely wait until VDP1 rendering has caught up, maybe
}

void Direct3D12VDPRenderer::VDP1WriteFB(uint32 address, uint8 value) {
    // TODO: mark as dirty
}

void Direct3D12VDPRenderer::VDP1WriteFB(uint32 address, uint16 value) {
    // TODO: mark as dirty
}

void Direct3D12VDPRenderer::VDP1WriteReg(uint32 address, uint16 value) {
    // TODO: mark as dirty
}

// -------------------------------------------------------------------------
// VDP2 memory and register writes

void Direct3D12VDPRenderer::VDP2WriteVRAM(uint32 address, uint8 value) {
    m_impl->VDP2WriteVRAM(address);
}

void Direct3D12VDPRenderer::VDP2WriteVRAM(uint32 address, uint16 value) {
    // The address is always word-aligned, so the value will never straddle two chunks
    m_impl->VDP2WriteVRAM(address);
}

void Direct3D12VDPRenderer::VDP2WriteCRAM(uint32 address, uint8 value) {
    m_impl->VDP2WriteCRAM(address);
}

void Direct3D12VDPRenderer::VDP2WriteCRAM(uint32 address, uint16 value) {
    // The address is always word-aligned
    m_impl->VDP2WriteCRAM(address);
}

void Direct3D12VDPRenderer::VDP2WriteReg(uint32 address, uint16 value) {
    m_impl->VDP2WriteReg(address, value);
}

// -----------------------------------------------------------------------------
// Debugger

void Direct3D12VDPRenderer::UpdateEnabledLayers() {
    m_impl->VDP2UpdateEnabledLayers();
}

// -----------------------------------------------------------------------------
// Utilities

void Direct3D12VDPRenderer::DumpExtraVDP1Framebuffers(std::ostream &out) const {
    // TODO: pause the world, download mesh buffers, copy to output
}

// -----------------------------------------------------------------------------
// Rendering process

void Direct3D12VDPRenderer::VDP1EraseFramebuffer(uint64 cycles) {
    // TODO: execute operation
}

void Direct3D12VDPRenderer::VDP1SwapFramebuffer() {
    // TODO: execute operation
    Callbacks.VDP1FramebufferSwap();
}

void Direct3D12VDPRenderer::VDP1BeginFrame() {
    // TODO: prepare new VDP1 frame
}

void Direct3D12VDPRenderer::VDP1ExecuteCommand(uint32 cmdAddress, VDP1Command::Control control) {
    // TODO: execute operation
}

void Direct3D12VDPRenderer::VDP1EndFrame() {
    // TODO: finish VDP1 frame
    Callbacks.VDP1DrawFinished();
}

void Direct3D12VDPRenderer::VDP2SetResolution(uint32 h, uint32 v, bool exclusive) {
    m_impl->HRes = h;
    m_impl->VRes = v;
    m_impl->exclusiveMonitor = exclusive;
    Callbacks.VDP2ResolutionChanged(h, v);
}

void Direct3D12VDPRenderer::VDP2SetField(bool odd) {
    // Nothing to do. We're using the main VDP2 state for this.
}

void Direct3D12VDPRenderer::VDP2LatchTVMD() {
    // Nothing to do. We're using the main VDP2 state for this.
}

void Direct3D12VDPRenderer::VDP2BeginFrame() {
    m_impl->VDP2BeginFrame();
}

void Direct3D12VDPRenderer::VDP2RenderLine(uint32 y) {
    m_impl->VDP2RenderLine(y);
}

void Direct3D12VDPRenderer::VDP2EndFrame() {
    m_impl->VDP2EndFrame();
    Callbacks.VDP2DrawFinished();
}

} // namespace ymir::vdp
