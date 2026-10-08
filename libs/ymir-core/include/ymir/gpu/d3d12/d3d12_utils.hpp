#pragma once

/**
@file
@brief Utility functions for Direct3D 12.
*/

#include <d3d12.h>

#include <ymir/util/inline.hpp>

namespace ymir::gpu::d3d12 {

FORCE_INLINE UINT GetElementSize(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_UNKNOWN: return 4u;
    case DXGI_FORMAT_R8_TYPELESS: return 1u;
    case DXGI_FORMAT_R8_UINT: return 1u;
    case DXGI_FORMAT_R8_SINT: return 1u;
    case DXGI_FORMAT_R8_UNORM: return 1u;
    case DXGI_FORMAT_R8_SNORM: return 1u;
    case DXGI_FORMAT_R16_TYPELESS: return 2u;
    case DXGI_FORMAT_R16_UINT: return 2u;
    case DXGI_FORMAT_R16_SINT: return 2u;
    case DXGI_FORMAT_R16_UNORM: return 2u;
    case DXGI_FORMAT_R16_SNORM: return 2u;
    case DXGI_FORMAT_R32_TYPELESS: return 4u;
    case DXGI_FORMAT_R32_UINT: return 4u;
    case DXGI_FORMAT_R32_SINT: return 4u;
    case DXGI_FORMAT_R32_FLOAT: return 4u;
    case DXGI_FORMAT_R8G8_TYPELESS: return 2u;
    case DXGI_FORMAT_R8G8_UINT: return 2u;
    case DXGI_FORMAT_R8G8_SINT: return 2u;
    case DXGI_FORMAT_R8G8_UNORM: return 2u;
    case DXGI_FORMAT_R8G8_SNORM: return 2u;
    case DXGI_FORMAT_R16G16_TYPELESS: return 4u;
    case DXGI_FORMAT_R16G16_UINT: return 4u;
    case DXGI_FORMAT_R16G16_SINT: return 4u;
    case DXGI_FORMAT_R16G16_FLOAT: return 4u;
    case DXGI_FORMAT_R32G32_TYPELESS: return 8u;
    case DXGI_FORMAT_R32G32_UINT: return 8u;
    case DXGI_FORMAT_R32G32_SINT: return 8u;
    case DXGI_FORMAT_R32G32_FLOAT: return 8u;
    case DXGI_FORMAT_R8G8B8A8_UINT: return 4u;
    case DXGI_FORMAT_R8G8B8A8_SINT: return 4u;
    case DXGI_FORMAT_R8G8B8A8_UNORM: return 4u;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return 4u;
    case DXGI_FORMAT_R8G8B8A8_SNORM: return 4u;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return 4u;
    case DXGI_FORMAT_B8G8R8A8_UNORM: return 4u;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return 4u;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: return 4u;
    case DXGI_FORMAT_B8G8R8X8_UNORM: return 4u;
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return 4u;
    case DXGI_FORMAT_B5G5R5A1_UNORM: return 4u;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return 4u;
    case DXGI_FORMAT_R10G10B10A2_UINT: return 4u;
    case DXGI_FORMAT_R10G10B10A2_UNORM: return 4u;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return 8u;
    case DXGI_FORMAT_R16G16B16A16_UINT: return 8u;
    case DXGI_FORMAT_R16G16B16A16_SINT: return 8u;
    case DXGI_FORMAT_R16G16B16A16_UNORM: return 8u;
    case DXGI_FORMAT_R16G16B16A16_SNORM: return 8u;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return 8u;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: return 16u;
    case DXGI_FORMAT_R32G32B32A32_UINT: return 16u;
    case DXGI_FORMAT_R32G32B32A32_SINT: return 16u;
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return 16u;
    default: return 4u;
    }
}

} // namespace ymir::gpu::d3d12
