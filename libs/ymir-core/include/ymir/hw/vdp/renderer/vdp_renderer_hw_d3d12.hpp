#pragma once

/**
@file
@brief VDP1 and VDP2 renderer implementation using Direct3D 12.

Requires Shader Model 6.0.
*/

#include <ymir/hw/vdp/renderer/vdp_renderer_hw_base.hpp>

#include <ymir/util/callback.hpp>
#include <ymir/util/result.hpp>

#include <memory>

// -----------------------------------------------------------------------------
// Forward declarations

struct ID3D12Device;
struct ID3D12Resource;
struct ID3D12Fence;

// -----------------------------------------------------------------------------

namespace ymir::vdp {

/// @brief Type of callback invoked when the Direct3D 12 VDP renderer requests a frame to copy the finished output frame
/// into. The returned resource (if any) must be a `ymir::vdp::kMaxResH` by `ymir::vdp::kMaxResV` `R8G8B8A8_UNORM` 2D
/// texture in the `COPY_DEST` state and not referenced by any in-flight frames when returned. The frontend may choose
/// to perform a CPU wait for the texture to be free if necessary.
///
/// @param[in] computeFence the compute fence to wait on
/// @param[in] fenceValue the fence value to wait for
/// @return a pointer to a 2D texture with `ymir::vdp::kMaxResH` by `ymir::vdp::kMaxResV` pixels and using
/// R8G8B8A8_UNORM pixel format. Return `nullptr` to omit the copy for this frame.
using Direct3D12GetFrameCopyTargetCallback =
    util::OptionalCallback<ID3D12Resource *(ID3D12Fence *computeFence, uint64 fenceValue)>;

/// @brief VDP renderer implementation using Direct3D 12.
/// Requires a valid `ID3D12Device *` instance, which has its reference count increments to ensure all managed objects
/// kept alive for the lifetime of the renderer.
class Direct3D12VDPRenderer : public HardwareVDPRendererBase {
    Direct3D12VDPRenderer(VDPState &state, const config::VDP2DebugRender &vdp2DebugRenderOptions,
                          const config::VDP2AccessPatternsConfig &vdp2AccessPatternsConfig);

    util::VoidResult<> Initialize(ID3D12Device *device);

public:
    ~Direct3D12VDPRenderer();

    static util::ObjectResult<Direct3D12VDPRenderer>
    Create(VDPState &state, const config::VDP2DebugRender &vdp2DebugRenderOptions,
           const config::VDP2AccessPatternsConfig &vdp2AccessPatternsConfig, ID3D12Device *device);

    // -------------------------------------------------------------------------
    // Configuration

    /// @brief Sets the frame copy callback.
    /// @param[in] cbFrameCopy the frame copy callback
    void SetFrameCopyCallback(Direct3D12GetFrameCopyTargetCallback cbFrameCopy);

    // -------------------------------------------------------------------------
    // Basics

    bool IsValid() const override;

    void Reset(bool hard) override;

    // -------------------------------------------------------------------------
    // Save states

    void PreSaveStateSync() override;
    void PostLoadStateSync() override;

    void SaveState(savestate::VDPSaveState::VDPRendererSaveState &state) override;
    bool ValidateState(const savestate::VDPSaveState::VDPRendererSaveState &state) const override;
    void LoadState(const savestate::VDPSaveState::VDPRendererSaveState &state) override;

    // -------------------------------------------------------------------------
    // VDP1 memory and register writes

    void VDP1WriteVRAM(uint32 address, uint8 value) override;
    void VDP1WriteVRAM(uint32 address, uint16 value) override;
    void VDP1SyncFB() override;
    void VDP1DebugSyncFB() override;
    void VDP1WriteFB(uint32 address, uint8 value) override;
    void VDP1WriteFB(uint32 address, uint16 value) override;
    void VDP1WriteReg(uint32 address, uint16 value) override;

    // -------------------------------------------------------------------------
    // VDP2 memory and register writes

    void VDP2WriteVRAM(uint32 address, uint8 value) override;
    void VDP2WriteVRAM(uint32 address, uint16 value) override;
    void VDP2WriteCRAM(uint32 address, uint8 value) override;
    void VDP2WriteCRAM(uint32 address, uint16 value) override;
    void VDP2WriteReg(uint32 address, uint16 value) override;

    // -------------------------------------------------------------------------
    // Debugger

    void UpdateEnabledLayers() override;

    // -------------------------------------------------------------------------
    // Utilities

    void DumpExtraVDP1Framebuffers(std::ostream &out) const override;

    // -------------------------------------------------------------------------
    // Rendering process

    void VDP1EraseFramebuffer(uint64 cycles) override;
    void VDP1SwapFramebuffer() override;
    void VDP1BeginFrame() override;
    void VDP1ExecuteCommand(uint32 cmdAddress, VDP1Command::Control control) override;
    void VDP1EndFrame() override;

    void VDP2SetResolution(uint32 h, uint32 v, bool exclusive) override;
    void VDP2SetField(bool odd) override;
    void VDP2LatchTVMD() override;
    void VDP2BeginFrame() override;
    void VDP2RenderLine(uint32 y) override;
    void VDP2EndFrame() override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ymir::vdp
