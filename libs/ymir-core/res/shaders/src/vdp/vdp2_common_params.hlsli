#ifndef YMIR_VDP_VDP2_COMMON_PARAMS_HLSLI
#define YMIR_VDP_VDP2_COMMON_PARAMS_HLSLI

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
