#include "vdp1_defs.hlsli"
#include "vdp1_common_params.hlsli"
#include "vdp1_polydraw_params.hlsli"

#include "util/bit_ops.hlsli"
#include "util/data_ops.hlsli"

// Shader specialization macros:
// - POLYSPEC_TRANSPARENT_MESH:
//     0 = checkerboard mesh
//     1 = transparent mesh
// - POLYSPEC_SHADING_MODE:
//     0 = Copy (Replace, Half-Luminance)
//     1 = Right-shift (Shadow)
//     2 = OIT (Half-Transparency)
//     3 = MSB
//
// Implementation notes:
// - Shading modes derived from CMDPMOD bits 0..1:
//    00 (0)  Replace            dst = src
//    01 (1)  Shadow             if (dst.msb) { dst = dst >> 1 }
//    10 (2)  Half-Luminance     dst = src >> 1
//    11 (3)  Half-Transparency  if (dst.msb) { dst = (dst + src) >> 1 } else { dst = src }
// - Inputs:
//   - Span parameters list
//   - Precomputed span length and prefix sums for pixel indexing
// - id.x is a pixel index into the span sequence
//   - For example, if the span list contains 3 spans with lengths 10, 12, 14 and skips 0, 0, 10:
//     - index  0 -> span 0 pixel 0
//     - index  7 -> span 0 pixel 7
//     - index  9 -> span 0 pixel 9
//     - index 10 -> span 1 pixel 0
//     - index 15 -> span 1 pixel 5
//     - index 21 -> span 1 pixel 11
//     - index 22 -> span 2 pixel 10
//     - index 25 -> span 2 pixel 13 (last)
//     - index 26 -> out of bounds, discarded
// - Spans are drawn parallel using order-independent algorithms depending on the blending mode
//   - MSB applies the bit directly to FBRAM with InterlockedOr (or set bits in a dedicated buffer; check which is faster)
//   - Replace and Half-Luminance use InterlockedMax with a sequence number to write the latest version of a pixel to the output
//   - Shadow increments per-pixel shift counters with InterlockedAdd
//   - Half-Transparency uses aper-pixel linked lists for order-independent transparency
// - The output merger shader applies the output of this shader to the output FBRAM in 32-bit units (2 or 4 pixels at a time)
//   - Skipped for MSB (unless using a dedicated buffer)

#define POLYSPEC_SHADING_MODE_COPY  0
#define POLYSPEC_SHADING_MODE_SHIFT 1
#define POLYSPEC_SHADING_MODE_OIT   2
#define POLYSPEC_SHADING_MODE_MSB   3

// Modify these to adjust IntelliSense highlighting
#ifdef __INTELLISENSE__
#define POLYSPEC_TRANSPARENT_MESH 1
#define POLYSPEC_SHADING_MODE     0
#endif

cbuffer RenderParamsBuffer : register(b0) {
    CommonRenderParams g_commonParams;
    PolyDrawParams g_polyDrawParams;
}

StructuredBuffer<PolySpan> g_spanParams : register(t1);
Buffer<uint> g_spanPrefixSums : register(t2);
StructuredBuffer<CommandParams> g_commandParams : register(t3);
ByteAddressBuffer g_vram : register(t4);

#if POLYSPEC_SHADING_MODE == POLYSPEC_SHADING_MODE_OIT

// Half-Transparency uses per-pixel linked lists for order-independent transparency
RWBuffer<uint> g_listHeads : register(u1);
RWStructuredBuffer<OITFragment> g_fragments : register(u2);
RWByteAddressBuffer g_counter : register(u3);

#elif POLYSPEC_SHADING_MODE == POLYSPEC_SHADING_MODE_MSB

// MSB writes directly to FBRAM
RWByteAddressBuffer g_fbramOut : register(u1);

#else

// All other modes write to the internal output buffer
RWBuffer<uint> g_internalSpriteOut : register(u1);

#endif

// ---------------------------------------------------------------------------------------------------------------------
// Parameters

static const uint2 fbSize = uint2(
    512u << BitExtract(g_commonParams.displayParams, 0, 1),
    256u << BitExtract(g_commonParams.displayParams, 1, 1)
);
static const bool pixel8Bits = BitTest(g_commonParams.displayParams, 2);
static const bool doubleDensity = BitTest(g_commonParams.displayParams, 3);
static const bool dblInterlaceEnable = BitTest(g_commonParams.displayParams, 4);
static const uint dblInterlaceDrawLine = BitExtract(g_commonParams.displayParams, 5, 1);
static const bool evenOddCoordSelect = BitTest(g_commonParams.displayParams, 6);

static const bool deinterlace = BitTest(g_commonParams.enhancements, 0);

// ---------------------------------------------------------------------------------------------------------------------
// Helpers

// Searches for the span containing the given pixel index.
// Returns 0xFFFFFFFF if out of range.
uint GetSpanIndex(uint pixelIndex) {
    if (pixelIndex >= g_spanPrefixSums[g_polyDrawParams.numSpans]) {
        return 0xFFFFFFFF;
    }

    // Binary search for smallest span index where pixelIndex >= prefixSum.
    // The span prefix sums array always contains [0, ..., total length].
    // If it contains [0, 3, 5], we want to return:
    // - index 0 for pixelIndex in [0..2]
    // - index 1 for pixelIndex in [3..4]
    // - out of bounds for any other pixelIndex
    uint lb = 0;
    uint ub = g_polyDrawParams.numSpans;
    while (lb != ub) {
        const uint midpoint = (lb + ub) >> 1u;
        const uint value = g_spanPrefixSums[midpoint];
        if (pixelIndex == value) {
            return midpoint;
        }
        if (pixelIndex > value) {
            lb = midpoint + 1u;
        } else {
            ub = midpoint;
        }
    }
    return lb - 1u;
}

// ---------------------------------------------------------------------------------------------------------------------
// DDA steppers

// Steps over the texels of a texture.
struct TextureStepper {
    int num;
    int den;
    int accum;

    int value;
    int inc;

    int baseAccum;
    int baseValue;

    void Setup(uint length, int start, int end, bool hss = false, int hssSelect = 0) {
        if (hss) {
            start >>= 1;
            end >>= 1;
        }
        const int delta = end - start;
        const uint absDelta = abs(delta);

        value = start;
        inc = delta >= 0 ? +1 : -1;
        if (hss) {
            value <<= 1;
            value |= hssSelect;
            inc <<= 1;
        }

        num = absDelta;
        den = length;
        if (length <= absDelta) {
            ++num;
            accum = absDelta - (length << 1);
            if (delta >= 0) {
                ++accum;
            }
        } else {
            --den;
            accum = length - (length << 1);
            if (delta < 0) {
                ++accum;
            }
        }
        num <<= 1;
        den <<= 1;
        baseAccum = accum;
        baseValue = value;
    }

    // Retrieves the current texture coordinate value.
    uint Value() {
        return value;
    }

    // Moves to the pixel at the specified step.
    void SetPixel(uint step) {
        accum = baseAccum + num * step;
        value = baseValue;
        if (accum >= 0) {
            const int count = (accum / den) + 1;
            value += inc * count;
            accum -= den * count;
        }
    }
};

// -----------------------------------------------------------------------------

// Iterates over a gouraud gradient of a single color channel.
struct GouraudChannelStepper {
    int num;
    int den;
    int accum;

    int value;
    int intInc;
    int fracInc;

    int baseValue;
    int baseAccum;

    void Setup(uint length, int start, int end) {
        const int delta = end - start;
        const uint absDelta = abs(delta);

        value = start;
        intInc = 0;
        fracInc = delta >= 0 ? +1 : -1;

        num = absDelta;
        den = length;
        if (length <= absDelta) {
            ++num;
            accum = absDelta - (length << 1);
            if (delta >= 0) {
                ++accum;
            }
        } else {
            --den;
            accum = -int(length);
            if (delta < 0) {
                ++accum;
            }
        }
        num <<= 1;
        den <<= 1;

        if (den != 0) {
            while (accum >= 0) {
                value += fracInc;
                accum -= den;
            }

            while (num >= den) {
                intInc += fracInc;
                num -= den;
            }
        }
        accum = ~accum;

        baseValue = value;
        baseAccum = accum;
    }

    // Skips the specified number of pixels.
    void Skip(int steps) {
        value += intInc * steps;
        accum -= num * steps;
        if (den != 0) {
            while (accum < 0) {
                value += fracInc;
                accum += den;
            }
        }
    }

    // Blends the given base color value with the current gouraud shading value.
    // The color value must be a 5-bit value.
    uint Blend(int color) {
        return clamp(value + color - 16, 0, 31);
    }
};

// -----------------------------------------------------------------------------

struct GouraudStepper {
    GouraudChannelStepper stepperR;
    GouraudChannelStepper stepperG;
    GouraudChannelStepper stepperB;

    // Sets up gouraud shading with the given length and start and end colors.
    void Setup(uint length, uint3 gouraudStart, uint3 gouraudEnd) {
        stepperR.Setup(length, gouraudStart.r, gouraudEnd.r);
        stepperG.Setup(length, gouraudStart.g, gouraudEnd.g);
        stepperB.Setup(length, gouraudStart.b, gouraudEnd.b);
    }

    // Skips the specified number of pixels.
    void Skip(int steps) {
        if (steps > 0) {
            stepperR.Skip(steps);
            stepperG.Skip(steps);
            stepperB.Skip(steps);
        }
    }

    // Blends the given base color with the current gouraud shading values.
    uint4 Blend(uint4 baseColor) {
        return uint4(
            stepperR.Blend(baseColor.r),
            stepperG.Blend(baseColor.g),
            stepperB.Blend(baseColor.b),
            baseColor.a
        );
    }
};

// -----------------------------------------------------------------------------

struct LineStepper {
    int num;
    int den;
    int accum;
    int accumTarget;

    int2 majInc;
    int2 minInc;

    int2 pos;
    int2 start;

    uint dmaj;
    uint step;

    int2 aaInc;

    void Setup(int2 coord1, int2 coord2, bool antiAlias = false) {
        pos = coord1;
        start = coord1;

        int2 delta = coord2 - coord1;
        int2 absDelta = abs(delta);
        dmaj = max(absDelta.x, absDelta.y);
        step = 0;

        const bool xMajor = absDelta.x >= absDelta.y;
        if (xMajor) {
            majInc.x = delta.x >= 0 ? +1 : -1;
            majInc.y = 0;
            minInc.x = 0;
            minInc.y = delta.y >= 0 ? +1 : -1;
        } else {
            majInc.x = 0;
            majInc.y = delta.y >= 0 ? +1 : -1;
            minInc.x = delta.x >= 0 ? +1 : -1;
            minInc.y = 0;
            delta.xy = delta.yx;
            absDelta.xy = absDelta.yx;
        }
        num = absDelta.y << 1;
        den = absDelta.x << 1;
        accum = absDelta.x + 1;
        accumTarget = 0;
        if (!antiAlias && delta.x < 0) {
            ++accumTarget;
        }
        accum += num;

        pos -= majInc;

        if (antiAlias) {
            --accum;
            --accumTarget;
            const bool samesign = (coord1.x > coord2.x) == (coord1.y > coord2.y);
            if (xMajor) {
                aaInc.x = samesign ? 0 : -majInc.x;
                aaInc.y = samesign ? -minInc.y : 0;
            } else {
                aaInc.x = samesign ? 0 : -minInc.x;
                aaInc.y = samesign ? -majInc.y : 0;
            }
        }

        // NOTE: Shifting counters by this amount forces them to have 13 bits without the need for masking
        // FIXME: breaks SetStep
        // static const int kShift = 32 - 13;
        //
        // num <<= kShift;
        // den <<= kShift;
        // accum <<= kShift;
        // accumTarget <<= kShift;
    }

    // Sets the slope step to the specified coordinate.
    // Clamped to the length of the line.
    void SetStep(uint targetStep) {
        targetStep = min(targetStep, dmaj);

        const int stepDelta = targetStep + 1 - step;
        if (stepDelta == 0) {
            return;
        }

        step = targetStep + 1;
        pos += majInc * stepDelta;

        // TODO: mask to 13 bits

        accum -= num * stepDelta;
        if (den != 0) {
            const int count = (accumTarget - accum + den) / den;
            accum += den * count;
            pos += minInc * count;
        }
    }

    // Determines if the current step needs antialiasing.
    bool NeedsAA() {
        return step > 1 && accum - den + num > accumTarget;
    }

    // Retrieves the current X and Y coordinates.
    int2 Coord() {
        return pos /*& 0x7FF*/;
    }

    // Returns the X and Y coordinates of the antialiased pixel.
    int2 AACoord() {
        return pos + aaInc;
    }

    // Retrieves the total number of steps in the slope, that is, the longest of the vertical and horizontal spans.
    uint Length() {
        return dmaj;
    }
};

void ReadTexel(uint u, uint v, uint charAddress, uint charSizeH, uint colorMode, uint colorData, out uint color, out bool transparent, out bool hasEndCode) {
    const uint charIndex = u + v * charSizeH;

    switch (colorMode) {
        case 0: // 4 bpp, 16 colors, bank mode
            color = Read8(g_vram, charAddress + (charIndex >> 1));
            color = (color >> ((~u & 1) * 4)) & 0xF;
            hasEndCode = color == 0xF;
            transparent = color == 0x0;
            color |= colorData & 0xFFF0;
            break;
        case 1: // 4 bpp, 16 colors, lookup table mode
            color = Read8(g_vram, charAddress + (charIndex >> 1));
            color = (color >> ((~u & 1) * 4)) & 0xF;
            hasEndCode = color == 0xF;
            transparent = color == 0x0;
            color = Read16(g_vram, color * 2 + colorData * 8);
            break;
        case 2: // 8 bpp, 64 colors, bank mode
            color = Read8(g_vram, charAddress + charIndex);
            transparent = color == 0x00;
            hasEndCode = color == 0xFF;
            color &= 0x3F;
            color |= colorData & 0xFFC0;
            break;
        case 3: // 8 bpp, 128 colors, bank mode
            color = Read8(g_vram, charAddress + charIndex);
            transparent = color == 0x00;
            hasEndCode = color == 0xFF;
            color &= 0x7F;
            color |= colorData & 0xFF80;
            break;
        case 4: // 8 bpp, 256 colors, bank mode
            color = Read8(g_vram, charAddress + charIndex);
            transparent = color == 0x00;
            hasEndCode = color == 0xFF;
            color |= colorData & 0xFF00;
            break;
        case 5: // 16 bpp, 32768 colors, RGB mode
            color = Read16(g_vram, (charAddress & ~0xF) + charIndex * 2);
            transparent = !BitTest(color, 15);
            hasEndCode = color == 0x7FFF;
            break;
    }
}

struct OutData {
    uint cmdIndex;
#if POLYSPEC_SHADING_MODE == POLYSPEC_SHADING_MODE_COPY || POLYSPEC_SHADING_MODE == POLYSPEC_SHADING_MODE_OIT
    uint value;
#endif
};

void WriteOutput(int2 coord, OutData data) {
    const CommandParams cmdParams = g_commandParams[data.cmdIndex];

    // Clip to system area
    const uint2 sysClip = uint2(
        BitExtract(cmdParams.sysClip, 0, 16),
        BitExtract(cmdParams.sysClip, 16, 16)
    );
    if (any(coord < 0) || any(coord > sysClip)) {
        return;
    }

    // Clip to user area
    const bool userClippingEnable = BitTest(cmdParams.cmdpmodcolr, 10);
    if (userClippingEnable) {
        const bool clippingMode = BitTest(cmdParams.cmdpmodcolr, 9);
        const uint2 userClip0 = uint2(
            BitExtract(cmdParams.userClip0, 0, 16),
            BitExtract(cmdParams.userClip0, 16, 16)
        );
        const uint2 userClip1 = uint2(
            BitExtract(cmdParams.userClip1, 0, 16),
            BitExtract(cmdParams.userClip1, 16, 16)
        );
        if ((any(coord < userClip0) || any(coord > userClip1)) != clippingMode) {
            return;
        }
    }

    // Mesh checkerboard test
    const bool meshEnable = BitTest(cmdParams.cmdpmodcolr, 8);
    if (!POLYSPEC_TRANSPARENT_MESH && meshEnable && BitTest(coord.x ^ coord.y, 0)) {
        return;
    }

    uint outOffset = 0;

#if POLYSPEC_SHADING_MODE == POLYSPEC_SHADING_MODE_MSB
    const uint drawFB = BitExtract(g_commonParams.displayParams, 7, 1);
    uint fbOffset = drawFB * kVDP1FBSize;
#endif

    // Interlace line selection
    if (!deinterlace && doubleDensity && dblInterlaceEnable && (coord.y & 1) != dblInterlaceDrawLine) {
        return;
    }
    if (deinterlace && doubleDensity && (coord.y & 1) != 0) {
#if POLYSPEC_SHADING_MODE == POLYSPEC_SHADING_MODE_MSB
        fbOffset += kVDP1FBRAMSize;
#else
        outOffset += fbSize.x * fbSize.y;
#endif
    }
    if ((deinterlace && doubleDensity) || dblInterlaceEnable) {
        coord.y >>= 1;
    }

    outOffset += coord.y * fbSize.x + coord.x;

#if POLYSPEC_TRANSPARENT_MESH
    if (meshEnable) {
        outOffset += fbSize.x * fbSize.y * 2;
    }
#endif

#if POLYSPEC_SHADING_MODE == POLYSPEC_SHADING_MODE_COPY
    // -------------------------------------------------------------------------
    // Replace or Half-Luminance

    // Output pixel with the highest sequence number

    InterlockedMax(g_internalSpriteOut[outOffset], data.value);

#elif POLYSPEC_SHADING_MODE == POLYSPEC_SHADING_MODE_SHIFT
    // -------------------------------------------------------------------------
    // Shadow

    // Output value is the number of shifts to apply to underlying pixels.
    // Output merger applies the shift to pixels with MSB=1.

    InterlockedAdd(g_internalSpriteOut[outOffset], 1);

#elif POLYSPEC_SHADING_MODE == POLYSPEC_SHADING_MODE_OIT
    // -------------------------------------------------------------------------
    // Half-Transparency

    // Append entry to pixel's node list

    uint nodeIndex;
    g_counter.InterlockedAdd(0, 1, nodeIndex);

    uint oldHead;
    InterlockedExchange(g_listHeads[outOffset], nodeIndex, oldHead);

    OITFragment node;
    node.data = data.value;
    node.next = oldHead;
    g_fragments[nodeIndex] = node;

#elif POLYSPEC_SHADING_MODE == POLYSPEC_SHADING_MODE_MSB
    // -------------------------------------------------------------------------
    // MSB

    // Apply MSB bit directly to FBRAM

    if (pixel8Bits) {
        outOffset &= ~1u;
    } else {
        outOffset <<= 1u;
    }

    WriteOr16(g_fbramOut, outOffset + fbOffset, 0x8000);

#endif

}

// ---------------------------------------------------------------------------------------------------------------------
// Entrypoint

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint spanIndex = GetSpanIndex(id.x);
    if (spanIndex == 0xFFFFFFFF) {
        return;
    }

    const PolySpan span = g_spanParams[spanIndex];
    const uint spanStep = id.x - g_spanPrefixSums[spanIndex] + BitExtract(span.skip_cmdIndex, 0, 16);
    const uint cmdIndex = BitExtract(span.skip_cmdIndex, 16, 16);
    const CommandParams cmdParams = g_commandParams[cmdIndex];

    const bool antialias = BitTest(span.attrs, 0);
    const bool textured = BitTest(span.attrs, 1);
    const uint cmdcolr = BitExtract(cmdParams.cmdpmodcolr, 16, 16);

    LineStepper lineStepper;
    lineStepper.Setup(span.coord0, span.coord1, antialias);
    lineStepper.SetStep(spanStep);

    uint spriteData;
    if (textured) {
        // ---------------------------------------------------------------------
        // Textured polygon

        TextureStepper uStepper;
        const uint charSizeH = max(BitExtract(cmdParams.cmdsizesrca, 8, 6) << 3, 1);
        const bool flipH = BitTest(span.attrs, 2);
        const uint colorMode = BitExtract(cmdParams.cmdpmodcolr, 3, 3);
        const bool transparentPixelDisable = BitTest(cmdParams.cmdpmodcolr, 6);
        const bool endCodesEnabled = !BitTest(cmdParams.cmdpmodcolr, 7);
        const bool useHighSpeedShrink = BitTest(cmdParams.cmdpmodcolr, 12) && lineStepper.Length() < charSizeH - 1;
        const bool evenOddCoordSelect = BitTest(g_commonParams.displayParams, 6);

        int uStart = 0;
        int uEnd = charSizeH - 1;
        if (flipH) {
            int tmp = uStart;
            uStart = uEnd;
            uEnd = tmp;
        }

        uStepper.Setup(lineStepper.Length() + 1, uStart, uEnd, useHighSpeedShrink, evenOddCoordSelect);
        uStepper.SetPixel(spanStep);

        uint endCodeIndex;
        bool checkEndCodes;
        if (endCodesEnabled && !useHighSpeedShrink) {
            endCodeIndex = BitExtract(span.attrs, 13, 10);
            checkEndCodes = endCodeIndex < charSizeH;
        } else {
            checkEndCodes = false;
        }

        const uint texU = uStepper.Value();
        if (checkEndCodes && (flipH ? (texU <= endCodeIndex) : (texU >= endCodeIndex))) {
            // Past end code range
            return;
        }

        const uint texV = BitExtract(span.attrs, 3, 10);
        const uint charAddr = BitExtract(cmdParams.cmdsizesrca, 16, 16) << 3u;
        bool transparent;
        bool hasEndCode;
        ReadTexel(texU, texV, charAddr, charSizeH, colorMode, cmdcolr, spriteData, transparent, hasEndCode);

        if ((hasEndCode && endCodesEnabled) || (transparent && !transparentPixelDisable)) {
            // Transparent pixel
            return;
        }
    } else {
        // -------------------------------------------------------------------------
        // Solid color polygon

        spriteData = cmdcolr;
        if (pixel8Bits) {
            spriteData &= 0xFFu;
        }
    }

    OutData data;
    data.cmdIndex = cmdIndex;

#if POLYSPEC_SHADING_MODE == POLYSPEC_SHADING_MODE_COPY || POLYSPEC_SHADING_MODE == POLYSPEC_SHADING_MODE_OIT
    // =========================================================================
    // Replace, Half-Luminance or Half-Transparency

    const uint shadingMode = BitExtract(cmdParams.cmdpmodcolr, 0, 2);
    const bool gouraudEnable = BitTest(cmdParams.cmdpmodcolr, 2);

    // Modify source color depending on the mode
    if (!pixel8Bits && (gouraudEnable || shadingMode == kColorBlendModeHalfLuminance)) {
        uint4 srcColor = Uint16ToColor555(spriteData);

        if (gouraudEnable) {
            // Apply gouraud shading
            GouraudStepper gouraud;
            gouraud.Setup(lineStepper.Length() + 1, span.gouraud0, span.gouraud1);
            gouraud.Skip(spanStep);
            srcColor = gouraud.Blend(srcColor);
        }

        if (shadingMode == kColorBlendModeHalfLuminance) {
            // Apply half-luminance
            srcColor.r >>= 1u;
            srcColor.g >>= 1u;
            srcColor.b >>= 1u;
        }

        spriteData = Color555ToUint16(srcColor);
    }

    data.value = spriteData | ((spanIndex + 1u) << 16u);
#endif

    const int2 coord = lineStepper.Coord();
    WriteOutput(lineStepper.Coord(), data);
    if (antialias && lineStepper.NeedsAA()) {
        WriteOutput(lineStepper.AACoord(), data);
    }
}
