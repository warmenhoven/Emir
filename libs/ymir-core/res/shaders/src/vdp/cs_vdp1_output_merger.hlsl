#include "vdp1_defs.hlsli"
#include "vdp1_common_params.hlsli"

#include "util/bit_ops.hlsli"

cbuffer CommonRenderParamsBuffer : register(b0) {
    CommonRenderParams g_commonParams;
}

RWByteAddressBuffer fbramOut : register(u0);
RWBuffer<uint> internalSpriteOut : register(u1);
RWBuffer<uint> internalSpriteMSB : register(u2);

// ---------------------------------------------------------------------------------------------------------------------
// Parameters

static const uint2 fbSize = uint2(
    512u << BitExtract(g_commonParams.displayParams, 0, 1),
    256u << BitExtract(g_commonParams.displayParams, 1, 1)
);
static const bool pixel8Bits = BitTest(g_commonParams.displayParams, 2);
static const bool doubleDensity = BitTest(g_commonParams.displayParams, 3);
static const bool dblInterlaceEnable = BitTest(g_commonParams.displayParams, 4);
static const bool dblInterlaceDrawLine = BitTest(g_commonParams.displayParams, 5);
static const uint drawFB = BitExtract(g_commonParams.displayParams, 7, 1);

static const bool deinterlace = BitTest(g_commonParams.enhancements, 0);

// ---------------------------------------------------------------------------------------------------------------------
// Entrypoint

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    // TODO: work on 32-bit units at a time
    // fbramOut.Store(id.x * 4 + id.y * 1024 + drawFB * 262144, drawFB ? 0xBEEFDEAD : 0xDEADBEEF);
}
