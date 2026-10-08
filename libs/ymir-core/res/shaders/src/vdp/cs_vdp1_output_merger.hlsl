#include "vdp1_defs.hlsli"
#include "vdp1_common_params.hlsli"

#include "util/bit_ops.hlsli"
#include "util/data_ops.hlsli"

// Shader specialization macros:
// - POLYSPEC_TRANSPARENT_MESH:
//     0 = disabled (checkerboard)
//     1 = enabled (50% alpha); processes both normal and mesh pixels simultaneously
// - POLYSPEC_MERGE_MODE:
//     0 = Copy (Replace, Half-Luminance)
//     1 = Right-shift (Shadow)
//     2 = OIT (Half-Transparency)
//
// Implementation notes:
// - Works on 32-bit units at a time
// - The Z ID selects the field to draw for deinterlaced rendering (0=main, 1=alternate)

#define POLYSPEC_SHADING_MODE_COPY  0
#define POLYSPEC_SHADING_MODE_SHIFT 1
#define POLYSPEC_SHADING_MODE_OIT   2

// Modify these to adjust IntelliSense highlighting
#ifdef __INTELLISENSE__
#define POLYSPEC_TRANSPARENT_MESH 1
#define POLYSPEC_MERGE_MODE       0
#endif

// TODO: figure out how shadow and transparent pixels on the transparent mesh layer should be rendered
// - also broken on the software renderer

cbuffer RenderParamsBuffer : register(b0) {
    CommonRenderParams g_commonParams;
}

#if POLYSPEC_MERGE_MODE == POLYSPEC_SHADING_MODE_OIT
StructuredBuffer<OITFragment> g_fragments : register(t1);

RWByteAddressBuffer g_fbramOut : register(u1);
RWBuffer<uint> g_listHeads : register(u2);
#else
RWByteAddressBuffer g_fbramOut : register(u1);
RWBuffer<uint> g_internalSpriteOut : register(u2);
#endif

// ---------------------------------------------------------------------------------------------------------------------
// Parameters

static const uint2 fbSize = uint2(
    512u << BitExtract(g_commonParams.displayParams, 0, 1),
    256u << BitExtract(g_commonParams.displayParams, 1, 1)
);
static const bool pixel8Bits = BitTest(g_commonParams.displayParams, 2);
static const uint drawFB = BitExtract(g_commonParams.displayParams, 7, 1);

static const uint fbOffset = drawFB * kVDP1FBSize;

// ---------------------------------------------------------------------------------------------------------------------
// Mergers

#if POLYSPEC_MERGE_MODE == POLYSPEC_SHADING_MODE_COPY
// ----------------------------------------------------------------------------
// Copy (Replace, Half-Luminance)

void Merge8(uint2 pos, uint field) {
    const uint2 inPos = uint2(pos.x * 4, pos.y);
    const uint inOffset = inPos.x + inPos.y * fbSize.x + field * fbSize.x * fbSize.y;

    // Read internal outputs
    const uint out0 = g_internalSpriteOut[inOffset + 0];
    const uint out1 = g_internalSpriteOut[inOffset + 1];
    const uint out2 = g_internalSpriteOut[inOffset + 2];
    const uint out3 = g_internalSpriteOut[inOffset + 3];

    // Get sequence numbers
    const uint seqNum0 = BitExtract(out0, 16, 16);
    const uint seqNum1 = BitExtract(out1, 16, 16);
    const uint seqNum2 = BitExtract(out2, 16, 16);
    const uint seqNum3 = BitExtract(out3, 16, 16);
    bool hasPixel0 = seqNum0 != 0;
    bool hasPixel1 = seqNum1 != 0;
    bool hasPixel2 = seqNum2 != 0;
    bool hasPixel3 = seqNum3 != 0;
#if POLYSPEC_TRANSPARENT_MESH
    const uint inMeshOffset = inOffset + fbSize.x * fbSize.y * 2;
    const uint meshOut0 = g_internalSpriteOut[inMeshOffset + 0];
    const uint meshOut1 = g_internalSpriteOut[inMeshOffset + 1];
    const uint meshOut2 = g_internalSpriteOut[inMeshOffset + 2];
    const uint meshOut3 = g_internalSpriteOut[inMeshOffset + 3];
    const uint meshSeqNum0 = BitExtract(meshOut0, 16, 16);
    const uint meshSeqNum1 = BitExtract(meshOut1, 16, 16);
    const uint meshSeqNum2 = BitExtract(meshOut2, 16, 16);
    const uint meshSeqNum3 = BitExtract(meshOut3, 16, 16);
    hasPixel0 = hasPixel0 || meshSeqNum0 != 0;
    hasPixel1 = hasPixel1 || meshSeqNum1 != 0;
    hasPixel2 = hasPixel2 || meshSeqNum2 != 0;
    hasPixel3 = hasPixel3 || meshSeqNum3 != 0;
#endif
    if (!hasPixel0 && !hasPixel1 && !hasPixel2 && !hasPixel3) {
        // Nothing written to these pixels
        return;
    }

    // Clear internal outputs
    g_internalSpriteOut[inOffset + 0] = 0;
    g_internalSpriteOut[inOffset + 1] = 0;
    g_internalSpriteOut[inOffset + 2] = 0;
    g_internalSpriteOut[inOffset + 3] = 0;
#if POLYSPEC_TRANSPARENT_MESH
    g_internalSpriteOut[inMeshOffset + 0] = 0;
    g_internalSpriteOut[inMeshOffset + 1] = 0;
    g_internalSpriteOut[inMeshOffset + 2] = 0;
    g_internalSpriteOut[inMeshOffset + 3] = 0;
#endif

    const uint outOffset = inPos.x + inPos.y * fbSize.x + field * kVDP1FBRAMSize;
    uint fbramValue = g_fbramOut.Load(outOffset + fbOffset);
    if (seqNum0 != 0) {
        fbramValue &= ~0xFFu;
        fbramValue |= BitExtract(out0, 0, 8);
    }
    if (seqNum1 != 0) {
        fbramValue &= ~0xFF00u;
        fbramValue |= BitExtract(out1, 0, 8) << 8u;
    }
    if (seqNum2 != 0) {
        fbramValue &= ~0xFF0000u;
        fbramValue |= BitExtract(out2, 0, 8) << 16u;
    }
    if (seqNum3 != 0) {
        fbramValue &= ~0xFF000000u;
        fbramValue |= BitExtract(out3, 0, 8) << 24u;
    }
    g_fbramOut.Store(outOffset + fbOffset, fbramValue);
#if POLYSPEC_TRANSPARENT_MESH
    const uint outMeshOffset = outOffset + kVDP1FBRAMSize * 2;
    fbramValue = g_fbramOut.Load(outMeshOffset + fbOffset);
    if (hasPixel0) {
        fbramValue &= ~0xFFu;
        if (meshSeqNum0 > seqNum0) {
            fbramValue |= BitExtract(meshOut0, 0, 8);
        }
    }
    if (hasPixel1) {
        fbramValue &= ~0xFF00u;
        if (meshSeqNum1 > seqNum1) {
            fbramValue |= BitExtract(meshOut1, 0, 8) << 8u;
        }
    }
    if (hasPixel2) {
        fbramValue &= ~0xFF0000u;
        if (meshSeqNum2 > seqNum2) {
            fbramValue |= BitExtract(meshOut2, 0, 8) << 16u;
        }
    }
    if (hasPixel3) {
        fbramValue &= ~0xFF000000u;
        if (meshSeqNum3 > seqNum3) {
            fbramValue |= BitExtract(meshOut3, 0, 8) << 24u;
        }
    }
    g_fbramOut.Store(outMeshOffset + fbOffset, fbramValue);
#endif
}

void Merge16(uint2 pos, uint field) {
    const uint2 inPos = uint2(pos.x * 2, pos.y);
    const uint inOffset = inPos.x + inPos.y * fbSize.x + field * fbSize.x * fbSize.y;

    // Read internal outputs
    const uint out0 = g_internalSpriteOut[inOffset + 0];
    const uint out1 = g_internalSpriteOut[inOffset + 1];

    // Get sequence numbers
    const uint seqNum0 = BitExtract(out0, 16, 16);
    const uint seqNum1 = BitExtract(out1, 16, 16);
    bool hasPixel0 = seqNum0 != 0;
    bool hasPixel1 = seqNum1 != 0;
#if POLYSPEC_TRANSPARENT_MESH
    const uint inMeshOffset = inOffset + fbSize.x * fbSize.y * 2;
    const uint meshOut0 = g_internalSpriteOut[inMeshOffset + 0];
    const uint meshOut1 = g_internalSpriteOut[inMeshOffset + 1];
    const uint meshSeqNum0 = BitExtract(meshOut0, 16, 16);
    const uint meshSeqNum1 = BitExtract(meshOut1, 16, 16);
    hasPixel0 = hasPixel0 || meshSeqNum0 != 0;
    hasPixel1 = hasPixel1 || meshSeqNum1 != 0;
#endif
    if (!hasPixel0 && !hasPixel1) {
        // Nothing written to these pixels
        return;
    }

    // Clear internal outputs
    g_internalSpriteOut[inOffset + 0] = 0;
    g_internalSpriteOut[inOffset + 1] = 0;
#if POLYSPEC_TRANSPARENT_MESH
    g_internalSpriteOut[inMeshOffset + 0] = 0;
    g_internalSpriteOut[inMeshOffset + 1] = 0;
#endif

    const uint outOffset = (inPos.x + inPos.y * fbSize.x) * 2 + field * kVDP1FBRAMSize;
    uint fbramValue = g_fbramOut.Load(outOffset + fbOffset);
    if (seqNum0 != 0) {
        fbramValue &= ~0xFFFFu;
        fbramValue |= ByteSwap16(out0);
    }
    if (seqNum1 != 0) {
        fbramValue &= ~0xFFFF0000u;
        fbramValue |= ByteSwap16(out1) << 16u;
    }
    g_fbramOut.Store(outOffset + fbOffset, fbramValue);
#if POLYSPEC_TRANSPARENT_MESH
    const uint outMeshOffset = outOffset + kVDP1FBRAMSize * 2;
    fbramValue = g_fbramOut.Load(outMeshOffset + fbOffset);
    if (hasPixel0) {
        fbramValue &= ~0xFFFFu;
        if (meshSeqNum0 > seqNum0) {
            fbramValue |= ByteSwap16(meshOut0);
        }
    }
    if (hasPixel1) {
        fbramValue &= ~0xFFFF0000u;
        if (meshSeqNum1 > seqNum1) {
            fbramValue |= ByteSwap16(meshOut1) << 16u;
        }
    }
    g_fbramOut.Store(outMeshOffset + fbOffset, fbramValue);
#endif
}

#elif POLYSPEC_MERGE_MODE == POLYSPEC_SHADING_MODE_SHIFT
// ----------------------------------------------------------------------------
// Right-shift (Shadow)

void Merge16Single(uint2 pos, uint field) {
    const uint2 inPos = uint2(pos.x * 2, pos.y);
    const uint inOffset = inPos.x + inPos.y * fbSize.x + field * fbSize.x * fbSize.y;

    // Read and clear internal outputs
    const uint shift0 = min(g_internalSpriteOut[inOffset + 0], 5);
    const uint shift1 = min(g_internalSpriteOut[inOffset + 1], 5);
    if (shift0 == 0 && shift1 == 0) {
        // Nothing written to these pixels
        return;
    }
    g_internalSpriteOut[inOffset + 0] = 0;
    g_internalSpriteOut[inOffset + 1] = 0;

    const uint outOffset = (inPos.x + inPos.y * fbSize.x) * 2 + field * kVDP1FBRAMSize;
    uint fbramValue = g_fbramOut.Load(outOffset + fbOffset);
    if (shift0 != 0) {
        uint4 color = Uint16ToColor555(ByteSwap16(fbramValue));
        if (color.a != 0u) {
            color.rgb >>= shift0;
            fbramValue &= ~0xFFFFu;
            fbramValue |= ByteSwap16(Color555ToUint16(color));
        }
    }
    if (shift1 != 0) {
        uint4 color = Uint16ToColor555(ByteSwap16(fbramValue >> 16u));
        if (color.a != 0u) {
            color.rgb >>= shift1;
            fbramValue &= ~0xFFFF0000u;
            fbramValue |= ByteSwap16(Color555ToUint16(color)) << 16u;
        }
    }
    g_fbramOut.Store(outOffset + fbOffset, fbramValue);
}

void Merge8(uint2 pos, uint field) {
    // Shadow does not apply to 8-bit mode.
}

void Merge16(uint2 pos, uint field) {
    Merge16Single(pos, field);
#if POLYSPEC_TRANSPARENT_MESH
    Merge16Single(pos, field + 2);
#endif
}

#elif POLYSPEC_MERGE_MODE == POLYSPEC_SHADING_MODE_OIT
// ----------------------------------------------------------------------------
// OIT (Half-Transparency)

uint HalfTransparentBlend(uint baseColor, uint listHead) {
    // Early exit if nothing was written to the pixel
    if (listHead == 0xFFFFFFFF) {
        return baseColor;
    }

    // Collect fragments
    // TODO: what if the cap is exceeded?
    OITFragment frags[32];
    uint count = 0;
    uint curr = listHead;
    while (curr != 0xFFFFFFFF && count < 32) {
        frags[count++] = g_fragments[curr];
        curr = frags[count - 1].next;
    }

    // Sort by sequence number in descending order (latest to oldest)
    for (uint i = 1; i < count; ++i) {
        OITFragment key = frags[i];
        int j = i - 1;
        while (j >= 0 && BitExtract(frags[j].data, 16, 16) < BitExtract(key.data, 16, 16)) {
            frags[j + 1] = frags[j];
            j--;
        }
        frags[j + 1] = key;
    }

    // Blend colors
    uint4 finalColor = Uint16ToColor555(baseColor);
    for (uint k = 0; k < count; ++k) {
        uint4 fragColor = Uint16ToColor555(frags[k].data);
        if (finalColor.a != 0) {
            finalColor.rgb = (finalColor.rgb + fragColor.rgb) >> 1u;
        } else {
            finalColor = fragColor;
        }
    }

    return Color555ToUint16(finalColor);
}

void Merge16Single(uint2 pos, uint field) {
    const uint2 inPos = uint2(pos.x * 2, pos.y);
    const uint inOffset = inPos.x + inPos.y * fbSize.x + field * fbSize.x * fbSize.y;

    const uint2 heads = uint2(
        g_listHeads[inOffset + 0],
        g_listHeads[inOffset + 1]
    );

    // Early exit if nothing was written to either pixel
    if (all(heads == 0xFFFFFFFF)) {
        return;
    }

    // Clear heads
    g_listHeads[inOffset + 0] = 0xFFFFFFFF;
    g_listHeads[inOffset + 1] = 0xFFFFFFFF;

    // Get base FBRAM value
    const uint fbramAddress = (inPos.x + inPos.y * fbSize.x) * 2 + fbOffset + field * kVDP1FBRAMSize;
    uint fbramValue = g_fbramOut.Load(fbramAddress);

    // Modify
    fbramValue =
        (ByteSwap16(HalfTransparentBlend(ByteSwap16(fbramValue >> 16u), heads[1])) << 16u) |
         ByteSwap16(HalfTransparentBlend(ByteSwap16(fbramValue), heads[0]));

    // Write back
    g_fbramOut.Store(fbramAddress, fbramValue);
}

void Merge8(uint2 pos, uint field) {
    // Half-Transparency does not apply to 8-bit mode.
}

void Merge16(uint2 pos, uint field) {
    Merge16Single(pos, field);
#if POLYSPEC_TRANSPARENT_MESH
    Merge16Single(pos, field + 2);
#endif
}

#endif

// ---------------------------------------------------------------------------------------------------------------------
// Entrypoint

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (pixel8Bits) {
        Merge8(id.xy, id.z);
    } else {
        Merge16(id.xy, id.z);
    }
}
