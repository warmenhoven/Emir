#ifndef YMIR_VDP_VDP1_DEFS_HLSLI
#define YMIR_VDP_VDP1_DEFS_HLSLI

// Size of a single VDP1 framebuffer
static const uint kVDP1FBSize = 256 * 1024;
// Size of the entire VDP1 FBRAM
static const uint kVDP1FBRAMSize = kVDP1FBSize * 2;

// CMDPMOD bits 0..1
static const uint kColorBlendModeReplace = 0;
static const uint kColorBlendModeShadow = 1;
static const uint kColorBlendModeHalfLuminance = 2;
static const uint kColorBlendModeHalfTransparency = 3;

struct OITFragment {
    uint data; // packed sprite data + sequence number
    uint next; // pointer to next fragment; 0xFFFFFFFF = end of list
};

#endif
