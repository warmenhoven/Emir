#ifndef YMIR_VDP_VDP2_WRITE_PARAMS_HLSLI
#define YMIR_VDP_VDP2_WRITE_PARAMS_HLSLI

// See C++ code for documentation on the fields

struct FBRAMWrite {
    uint address;
    uint andMask;
    uint orMask;
};

#endif
