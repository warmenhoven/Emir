#ifndef YMIR_VDP_VDP2_COMPOSE_PARAMS_HLSLI
#define YMIR_VDP_VDP2_COMPOSE_PARAMS_HLSLI

// See C++ code for documentation on the fields

struct ComposeParams {
    uint colorCalcEnable;
    uint colorOffsetEnable;
    uint colorOffsetSelect;
    uint lineColorEnable;
    int3 colorOffsetA;
    int3 colorOffsetB;
    uint bgColorCalcRatios[5];
    uint backLineColorCalcRatios[2];
};

#endif
