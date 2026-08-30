#ifndef YMIR_VDP_VDP2_RENDER_PARAMS_ROTATION_HLSLI
#define YMIR_VDP_VDP2_RENDER_PARAMS_ROTATION_HLSLI

#include "vdp2_render_params_window.hlsli"

// See C++ code for documentation on the fields

struct RotRegs {
    bool coeffTableEnable;
    bool coeffTableCRAM;
    uint coeffDataSize;
    uint coeffDataMode;
    uint coeffDataAccess;
    bool coeffDataPerDot;
    bool coeffUseLineColorData;
};

struct RotParamBase {
    uint tableAddress;
    int Xst, Yst;
    uint KA;
};

#endif
