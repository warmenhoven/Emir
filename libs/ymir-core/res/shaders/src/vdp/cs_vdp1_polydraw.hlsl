#include "vdp1_defs.hlsli"
#include "vdp1_common_params.hlsli"
#include "vdp1_polydraw_params.hlsli"

// Shader specialization macros:
// - POLYSPEC_ANTIALIAS: 0=no antialiasing (hole-filling); 1=enabled
// - POLYSPEC_TEXTURED:  0=solid color; 1=textured
// - POLYSPEC_MESH_MODE: 0=solid; 1=checkerboard mesh; 2=transparent mesh
// - POLYSPEC_SHADING_GOURAUD  [CMDPMOD.2]: 0=flat shading; 1=gouraud shading
// - POLYSPEC_SHADING_HALF_SRC [CMDPMOD.1]: 0=don't modify source color; 1=halve source color ("half-luminance")
// - POLYSPEC_SHADING_HALF_DST [CMDPMOD.0]: 0=don't modify destination color; 1=halve destination color ("shadow")

// Modify these to adjust IntelliSense highlighting
#ifdef __INTELLISENSE__
#define POLYSPEC_ANTIALIAS        0
#define POLYSPEC_TEXTURED         0
#define POLYSPEC_MESH_MODE        0
#define POLYSPEC_SHADING_GOURAUD  0
#define POLYSPEC_SHADING_HALF_SRC 0
#define POLYSPEC_SHADING_HALF_DST 0
#endif

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
    // - draw spans in parallel into a fragment buffer
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
