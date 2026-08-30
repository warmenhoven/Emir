#ifndef YMIR_VDP_VDP2_RENDER_PARAMS_WINDOW_HLSLI
#define YMIR_VDP_VDP2_RENDER_PARAMS_WINDOW_HLSLI

// See C++ code for documentation on the fields

struct GlobalWindowParams {
    uint2 start;
    uint2 end;
    uint lineWindowTableAddress;
    bool lineWindowTableEnable;
};

struct LayerWindowParams {
    bool windowLogicAnd;
    bool window0Enable;
    bool window0Invert;
    bool window1Enable;
    bool window1Invert;
};

struct LayerWindowParamsS {
    LayerWindowParams base;
    bool spriteWindowEnable;
    bool spriteWindowInvert;
};

#endif
