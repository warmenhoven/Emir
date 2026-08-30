#ifndef YMIR_VDP_VDP2_RENDER_PARAMS_COMMON_HLSLI
#define YMIR_VDP_VDP2_RENDER_PARAMS_COMMON_HLSLI

// See C++ code for documentation on the fields

struct CommonRenderParams {
    uint startY;
    uint displayParams;
    uint layerParams;
    uint rotParams;
    uint spriteParams;
    uint2 spritePriosRatios;
    uint vcellScroll;
    uint windows;
    uint enhancements;
};

#endif
