#pragma once

#include <ymir/util/callback.hpp>

#include <ymir/core/types.hpp>

// -----------------------------------------------------------------------------
// Forward declarations

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
using CBDirect3D12FrameCopyRequestCallback =
    util::OptionalCallback<ID3D12Resource *(ID3D12Fence *computeFence, uint64 fenceValue)>;

/// @brief Callbacks specific to the Direct3D 12 VDP renderer.
struct Direct3D12RendererCallbacks {
    CBDirect3D12FrameCopyRequestCallback FrameCopyRequest;
};

} // namespace ymir::vdp
