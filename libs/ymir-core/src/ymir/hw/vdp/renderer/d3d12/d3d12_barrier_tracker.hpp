#pragma once

#include <ymir/gpu/d3d12/d3d12_commands.hpp>

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <d3d12.h>

namespace ymir::vdp {

/// @brief Determines if a barrier access grants write access.
/// NOTE: Only concerned with bits valid for compute tasks.
/// @param[in] access the access bitmask to check
/// @return `true` if any of the provided access bits grant write access, `false` if read-only.
inline bool IsBarrierAccessWrite(D3D12_BARRIER_ACCESS access) {
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

    /// @brief Stops tracking a buffer.
    /// @param[in] resource the buffer resource
    void DeleteBuffer(ID3D12Resource *resource) {
        m_currentBufferStates.erase(resource);
        m_desiredBufferStates.erase(resource);
        m_uavBufferBarriers.erase(resource);
    }

    /// @brief Stops tracking a texture.
    /// @param[in] resource the texture resource
    void DeleteTexture(ID3D12Resource *resource) {
        m_currentTextureStates.erase(resource);
        m_desiredTextureStates.erase(resource);
        m_uavTextureBarriers.erase(resource);
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

    /// @brief Registers a buffer UAV barrier.
    /// @param[in] buffer pointer to the buffer resource
    /// @return this barrier set
    BarrierTracker &UAVBuffer(ID3D12Resource *buffer) {
        m_uavBufferBarriers.insert(buffer);
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

    /// @brief Registers a texture UAV barrier.
    /// @param[in] texture pointer to the texture resource
    /// @return this barrier set
    BarrierTracker &UAVTexture(ID3D12Resource *texture) {
        m_uavTextureBarriers.insert(texture);
        return *this;
    }

    /// @brief Emits a barrier command into the command list if any relevant changes to barriers are detected and
    /// updates the current states of all affected barriers.
    /// @param[in] cmdList the command list
    void Flush(gpu::d3d12::D3D12GraphicsCommandList &cmdList) {
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
            for (ID3D12Resource *buffer : m_uavBufferBarriers) {
                bufferBarriers.push_back({
                    .SyncBefore = D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                    .SyncAfter = D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                    .AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
                    .AccessAfter = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
                    .pResource = buffer,
                    .Offset = 0,
                    .Size = UINT64_MAX,
                });
            }
            m_uavBufferBarriers.clear();
            if (!bufferBarriers.empty()) {
                groups.push_back({
                    .Type = D3D12_BARRIER_TYPE_BUFFER,
                    .NumBarriers = static_cast<UINT32>(bufferBarriers.size()),
                    .pBufferBarriers = bufferBarriers.data(),
                });
            }

            static constexpr D3D12_BARRIER_SUBRESOURCE_RANGE kTexRangeAll{
                .IndexOrFirstMipLevel = 0xFFFFFFFF,
                .NumMipLevels = 0,
            };

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
            for (ID3D12Resource *texture : m_uavTextureBarriers) {
                textureBarriers.push_back({
                    .SyncBefore = D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                    .SyncAfter = D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                    .AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
                    .AccessAfter = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
                    .LayoutBefore = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,
                    .LayoutAfter = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,
                    .pResource = texture,
                    .Subresources = kTexRangeAll,
                    .Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE,
                });
            }
            m_uavTextureBarriers.clear();
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

            for (ID3D12Resource *resource : m_uavBufferBarriers) {
                barriers.push_back({
                    .Type = D3D12_RESOURCE_BARRIER_TYPE_UAV,
                    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                    .UAV =
                        {
                            .pResource = resource,
                        },
                });
            }
            m_uavBufferBarriers.clear();

            for (ID3D12Resource *resource : m_uavTextureBarriers) {
                barriers.push_back({
                    .Type = D3D12_RESOURCE_BARRIER_TYPE_UAV,
                    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                    .UAV =
                        {
                            .pResource = resource,
                        },
                });
            }
            m_uavTextureBarriers.clear();

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

    std::unordered_set<ID3D12Resource *> m_uavBufferBarriers;
    std::unordered_set<ID3D12Resource *> m_uavTextureBarriers;

    /// @brief Retrieves a pointer to the specified command list if enhanced barriers are supported.
    /// @param[in] cmdList the command list
    /// @param[in] enhancedBarriers whether enhanced barriers are supported
    /// @return a pointer to the command list converted to `ID3D12GraphicsCommandList7` for enhanced barriers
    /// operations, or `nullptr` if the feature is not supported by the device
    ID3D12GraphicsCommandList7 *GetCommandListForEnhancedBarriers(gpu::d3d12::D3D12GraphicsCommandList &cmdList) const {
        return m_enhancedBarriers ? cmdList.As7() : nullptr;
    }
};

} // namespace ymir::vdp
