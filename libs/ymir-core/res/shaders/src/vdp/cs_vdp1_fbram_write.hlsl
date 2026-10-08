#include "vdp1_defs.hlsli"
#include "vdp1_common_params.hlsli"
#include "vdp1_write_params.hlsli"

#include "util/bit_ops.hlsli"

struct FBRAMWriteParams {
    uint writeCount;
};

cbuffer CommonRenderParamsBuffer : register(b0) {
    CommonRenderParams g_commonParams;
    FBRAMWriteParams g_writeParams;
}

StructuredBuffer<FBRAMWrite> g_fbramWrites : register(t1);

RWByteAddressBuffer g_fbramOut : register(u1);

// ---------------------------------------------------------------------------------------------------------------------
// Parameters

static const bool doubleDensity = BitTest(g_commonParams.displayParams, 3);
static const uint drawFB = BitExtract(g_commonParams.displayParams, 7, 1);
static const uint drawFBOffset = drawFB * kVDP1FBSize;

static const bool deinterlace = BitTest(g_commonParams.enhancements, 0);
static const bool transparentMeshes = BitTest(g_commonParams.enhancements, 1);

// ---------------------------------------------------------------------------------------------------------------------
// Entrypoint

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint index = id.x;
    if (index >= g_writeParams.writeCount) {
        return;
    }

    const FBRAMWrite write = g_fbramWrites[index];

    // The address is relative to the start of the current CPU-visible framebuffer
    const uint address = write.address + drawFBOffset;
    uint value = g_fbramOut.Load(address);
    value &= write.andMask;
    value |= write.orMask;

    // Write updated value to FBRAM.
    // Clear corresponding pixels in the mesh buffers.
    // Replicate to the deinterlace buffers.
    g_fbramOut.Store(address, value);
    if (transparentMeshes) {
        uint meshValue = g_fbramOut.Load(address + kVDP1FBRAMSize * 2);
        meshValue &= write.andMask;
        g_fbramOut.Store(address + kVDP1FBRAMSize * 2, meshValue);
    }
    if (deinterlace && doubleDensity) {
        g_fbramOut.Store(address + kVDP1FBRAMSize, value);
        if (transparentMeshes) {
            uint meshValue = g_fbramOut.Load(address + kVDP1FBRAMSize * 3);
            meshValue &= write.andMask;
            g_fbramOut.Store(address + kVDP1FBRAMSize * 3, meshValue);
        }
    }
}
