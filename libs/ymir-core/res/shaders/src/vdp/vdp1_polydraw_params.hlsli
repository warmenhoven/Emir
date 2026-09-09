#ifndef YMIR_VDP_VDP1_POLYDRAW_PARAMS_HLSLI
#define YMIR_VDP_VDP1_POLYDRAW_PARAMS_HLSLI

// See C++ code for documentation on the fields

struct PolyDrawParams {
    uint sysClip;
    uint userClip0;
    uint userClip1;
};

struct PolySpan {
    int2 coord0;
    int2 coord1;
    uint length;

    uint gouraud0;
    uint gouraud1;

    uint cmdpmod;
    uint cmdcolr;
    uint cmdsrca;
    uint cmdsize;

    uint texV;
    bool flipH;
};

#endif
