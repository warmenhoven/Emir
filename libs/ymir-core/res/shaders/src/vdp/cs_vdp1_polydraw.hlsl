#include "vdp1_defs.hlsli"
#include "vdp1_common_params.hlsli"
#include "vdp1_polydraw_params.hlsli"

#include "util/bit_ops.hlsli"

// Shader specialization macros:
// - POLYSPEC_TEXTURED: 0=solid color; 1=textured
// - POLYSPEC_TRANSPARENT_MESH: 0=checkerboard mesh; 1=transparent mesh
// - POLYSPEC_SHADING_GOURAUD  [CMDPMOD.2]: 0=flat shading; 1=gouraud shading
// - POLYSPEC_SHADING_HALF_SRC [CMDPMOD.1]: 0=don't modify source color; 1=halve source color ("half-luminance")
// - POLYSPEC_SHADING_HALF_DST [CMDPMOD.0]: 0=don't modify destination color; 1=halve destination color ("shadow")

// Modify these to adjust IntelliSense highlighting
#ifdef __INTELLISENSE__
#define POLYSPEC_TEXTURED         0
#define POLYSPEC_TRANSPARENT_MESH 0
#define POLYSPEC_SHADING_GOURAUD  0
#define POLYSPEC_SHADING_HALF_SRC 0
#define POLYSPEC_SHADING_HALF_DST 0
#endif

cbuffer CommonRenderParamsBuffer : register(b0) {
    CommonRenderParams g_commonParams;
    PolyDrawParams g_polyDrawParams;
}

StructuredBuffer<PolySpan> spans : register(t1);
Buffer<uint> spanPrefixSums : register(t2);

RWBuffer<uint> internalSpriteOut : register(u0);

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
static const bool evenOddCoordSelect = BitTest(g_commonParams.displayParams, 6);
static const uint drawFB = BitExtract(g_commonParams.displayParams, 7, 1);
static const bool antialias = BitTest(g_commonParams.displayParams, 8);

static const bool deinterlace = BitTest(g_commonParams.enhancements, 0);

static const uint2 sysClip = uint2(
    BitExtract(g_polyDrawParams.sysClip, 0, 16),
    BitExtract(g_polyDrawParams.sysClip, 16, 16)
);
static const uint2 userClip0 = uint2(
    BitExtract(g_polyDrawParams.userClip0, 0, 16),
    BitExtract(g_polyDrawParams.userClip0, 16, 16)
);
static const uint2 userClip1 = uint2(
    BitExtract(g_polyDrawParams.userClip1, 0, 16),
    BitExtract(g_polyDrawParams.userClip1, 16, 16)
);

// ---------------------------------------------------------------------------------------------------------------------
// Entrypoint

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    // TODO: implement

    // POLYSPEC_SHADING_HALF_DST and POLYSPEC_SHADING_HALF_SRC specify the blending mode:
    //  DST=0 SRC=0  Replace            dst = src
    //  DST=0 SRC=1  Half-Luminance     dst = src >> 1
    //  DST=1 SRC=0  Shadow             if (dst.msb) { dst = dst >> 1 }
    //  DST=1 SRC=1  Half-Transparency  if (dst.msb) { dst = (dst + src) >> 1 } else { dst = src }

    // Common implementation details:
    // - inputs:
    //   - span parameters list
    //     - start and end coordinates and gouraud colors
    //     - span length in pixels
    //     - texture V coordinate
    //     - horizontal flip bit
    //   - precomputed span length and prefix sums to aid pixel-level indexing
    // - id.x is a pixel-level index into the span sequence
    //   - for example, if the span list contains 3 spans with lengths 10, 12, 14:
    //     - index  0 -> span 0 pixel 0
    //     - index  7 -> span 0 pixel 7
    //     - index  9 -> span 0 pixel 9
    //     - index 10 -> span 1 pixel 0
    //     - index 15 -> span 1 pixel 5
    //     - index 21 -> span 1 pixel 11
    //     - index 22 -> span 2 pixel 0
    //     - index 35 -> span 2 pixel 13 (last)
    //     - index 36 -> out of bounds, discarded
    // - draw spans in parallel into internalSpriteOut
    // - run a second shader to combine that into the output FBRAM (2 or 4 pixels at a time to fit into 32-bit values)

    // Possible implementation for Replace and Half-Luminance (and maybe Shadow):
    // - combine 8/16-bit sprite data output with the pixel index into a single 32-bit value to be written to the intermediate output buffer
    //   - top bits contain the pixel sequence number (index into span array)
    // - use InterlockedMax to plot the latest pixel to the framebuffer

    // Half-Transparency needs an order-independent transparency implementation and different inputs and outputs.
    // TODO: investigate alternatives:
    // see https://github.com/nvpro-samples/vk_order_independent_transparency
    // - Linked List
    // - Loop32
    // - Spinlock
}
