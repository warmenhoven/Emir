#include "vdp1_defs.hlsli"
#include "vdp1_common_params.hlsli"
#include "vdp1_polydraw_params.hlsli"

// Shader specialization macros:
// - POLYSPEC_ANTIALIAS: 0=no antialiasing (hole-filling); 1=enabled
// - POLYSPEC_TEXTURED:  0=solid color; 1=textured
// - POLYSPEC_MESH_MODE: 0=solid; 1=checkerboard mesh; 2=transparent mesh
// - POLYSPEC_SHADING_GOURAUD  [CMDPMOD.2]: 0=flat shading; 1=gouraud shading
// - POLYSPEC_SHADING_HALF_SRC [CMDPMOD.1]: 0=don't modify source color; 1=halve source color
// - POLYSPEC_SHADING_HALF_DST [CMDPMOD.0]: 0=don't modify destination color; 1=halve destination color

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

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
}
