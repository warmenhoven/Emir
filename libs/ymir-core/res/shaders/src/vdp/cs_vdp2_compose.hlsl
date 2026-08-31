#include "vdp2_common_params.hlsli"
#include "vdp2_compose_params.hlsli"

#include "vdp2_defs.hlsli"

#include "util/bit_ops.hlsli"
#include "util/data_ops.hlsli"

cbuffer CommonRenderParams : register(b0) {
    CommonRenderParams g_commonParams;
}

StructuredBuffer<ComposeParams> composeParams : register(t1);
Texture2DArray<uint4> layerIn : register(t2);
Buffer<uint> lnclBackIn : register(t3);
Texture2DArray<uint4> rbgLineColorIn : register(t4);
Texture2DArray<uint> spriteAttrsIn : register(t5);
Texture2D<uint> colorCalcWindowIn : register(t6);

RWTexture2D<float4> compositeOut : register(u0);

// ---------------------------------------------------------------------------------------------------------------------
// Definitions

static const uint kBGLayerNBG0 = 0;
static const uint kBGLayerNBG1 = 1;
static const uint kBGLayerNBG2 = 2;
static const uint kBGLayerNBG3 = 3;
static const uint kBGLayerRBG0 = 4;
static const uint kBGLayerRBG1 = 5;
static const uint kBGLayerSprite = 6;
static const uint kBGLayerMesh = 7;
static const uint kBGLayerInvalid = 8;

static const uint kLayerSprite = 0;
static const uint kLayerRBG0 = 1;
static const uint kLayerNBG0_RBG1 = 2;
static const uint kLayerNBG1_EXBG = 3;
static const uint kLayerNBG2 = 4;
static const uint kLayerNBG3 = 5;
static const uint kLayerBack = 6;
static const uint kLayerLine = 7; // not used in the stack, but referenced by parameters
static const uint kLayerMesh = 8; // not used in the stack, but referenced by parameters

static const uint kSpriteCCCondPriorityLE = 0;
static const uint kSpriteCCCondPriorityEQ = 1;
static const uint kSpriteCCCondPriorityGE = 2;
static const uint kSpriteCCCondColorMSB = 3;

// ---------------------------------------------------------------------------------------------------------------------
// Parameters

static const uint interlaceMode = BitExtract(g_commonParams.displayParams, 2, 2);
static const uint oddField = BitExtract(g_commonParams.displayParams, 4, 1);
static const bool exclusiveMonitor = BitTest(g_commonParams.displayParams, 5);

static const bool deinterlace = BitTest(g_commonParams.enhancements, 0);
static const bool transparentMeshes = BitTest(g_commonParams.enhancements, 1);

// ---------------------------------------------------------------------------------------------------------------------
// Utilities

uint GetLoResInputY(uint y) {
    if (deinterlace && interlaceMode >= kInterlaceModeSingleDensity && !exclusiveMonitor) {
        return y >> 1;
    } else {
        return y;
    }
}

uint GetOutputY(uint y) {
    if (!deinterlace && interlaceMode >= kInterlaceModeSingleDensity && !exclusiveMonitor) {
        return (y << 1) | oddField;
    } else {
        return y;
    }
}

bool IsBGLayerEnabled(uint bgLayer) {
    return BitTest(g_commonParams.layerParams, bgLayer + 6);
}

bool IsLayerEnabled(uint layer) {
    return BitTest(g_commonParams.layerParams, layer);
}

uint GetBGLayerIndex(uint layer) {
    switch (layer) {
        case kLayerSprite:
            return kBGLayerSprite;
        case kLayerMesh:
            return kBGLayerMesh;
        case kLayerRBG0:
            return kBGLayerRBG0;
        case kLayerNBG0_RBG1:
            return IsBGLayerEnabled(kBGLayerRBG1) ? kBGLayerRBG1 : kBGLayerNBG0;
        case kLayerNBG1_EXBG:
            return kBGLayerNBG1;
        case kLayerNBG2:
            return kBGLayerNBG2;
        case kLayerNBG3:
            return kBGLayerNBG3;
        default:
            return kBGLayerInvalid; // go out of bounds intentionally to read blanks
    }
}

bool IsColorCalcEnabled(uint layer, uint2 pos) {
    const bool enabled = BitTest(composeParams[0].colorCalcEnable, layer);
    if (layer >= kLayerBack) {
        // Back and line screen layers use the enable bit alone
        return enabled;
    }
    if (!enabled) {
        // Color calculation is disabled for this layer
        return false;
    }
    if (colorCalcWindowIn[pos] != 0) {
        return false;
    }
    const bool restrictedColorCalc = BitTest(g_commonParams.layerParams, 24);
    if (layer == kLayerSprite) {
        // Sprites use condition modes based on priority or color MSB
        const uint layerAttrs = layerIn[uint3(pos, kLayerIndexSprite)].a;
        if (restrictedColorCalc && BitTest(layerAttrs, kPixelAttrBitSpecColorCalc)) {
            return false;
        }
        const uint attrs = spriteAttrsIn[uint3(pos, 0)];
        const uint priority = BitExtract(attrs, 0, 3);
        const uint value = BitExtract(g_commonParams.spriteParams, 11, 3);
        const uint cond = BitExtract(g_commonParams.spriteParams, 14, 2);
        switch (cond) {
            case kSpriteCCCondPriorityLE:
                return priority <= value;
            case kSpriteCCCondPriorityEQ:
                return priority == value;
            case kSpriteCCCondPriorityGE:
                return priority >= value;
            case kSpriteCCCondColorMSB:
                return BitTest(attrs, kSpriteAttrBitColorMSB);
        }
        return false;
    }
    // BG layers use the per-pixel special color calculation flag
    const uint bgLayer = GetBGLayerIndex(layer);
    const uint attrs = layerIn[uint3(pos.xy, bgLayer)].a;
    if (restrictedColorCalc && BitTest(attrs, kPixelAttrBitPaletteFormat)) {
        return false;
    }
    return BitTest(attrs, kPixelAttrBitSpecColorCalc);
}

bool IsLineColorEnabled(uint layer, uint2 pos) {
    return BitTest(composeParams[0].lineColorEnable, layer);
}

uint3 Color888(uint val32) {
    return uint3(
        BitExtract(val32, 0u, 8u),
        BitExtract(val32, 8u, 8u),
        BitExtract(val32, 16u, 8u)
    );
}

uint3 GetLineColor(uint layer, uint2 pos) {
    if (layer == kLayerRBG0 || (layer == kLayerNBG0_RBG1 && IsBGLayerEnabled(kBGLayerRBG1))) {
        return rbgLineColorIn[uint3(pos, layer - kLayerRBG0)].rgb;
    }
    return Color888(lnclBackIn[GetLoResInputY(pos.y)]);
}

int GetColorCalcRatio(uint layer, uint2 pos) {
    switch (layer) {
        case kLayerSprite:
            return spriteAttrsIn[uint3(pos, 0)];
        case kLayerRBG0:
        case kLayerNBG0_RBG1:
        case kLayerNBG1_EXBG:
        case kLayerNBG2:
        case kLayerNBG3:
            return composeParams[0].bgColorCalcRatios[layer - kLayerRBG0];
        case kLayerBack:
        case kLayerLine:
            if (IsColorCalcEnabled(layer, pos)) {
                return composeParams[0].backLineColorCalcRatios[1];
            } else {
                return composeParams[0].backLineColorCalcRatios[0];
            }
        default:
            return 31;
    }
}

bool IsColorOffsetEnabled(uint layer) {
    return BitTest(composeParams[0].colorOffsetEnable, layer);
}

int3 GetColorOffset(uint layer) {
    const bool selB = BitTest(composeParams[0].colorOffsetSelect, layer);
    return selB ? composeParams[0].colorOffsetB : composeParams[0].colorOffsetA;
}

uint4 GetLayerOutput(uint layer, uint2 pos) {
    switch (layer) {
        case kLayerSprite:
        case kLayerMesh:
        case kLayerRBG0:
        case kLayerNBG0_RBG1:
        case kLayerNBG1_EXBG:
        case kLayerNBG2:
        case kLayerNBG3:
            return layerIn[uint3(pos.xy, GetBGLayerIndex(layer))];
        case kLayerBack:
            return uint4(Color888(lnclBackIn[GetLoResInputY(pos.y) + kMaxResV]), 0); // the attribute byte doesn't matter
        case kLayerLine:
            return uint4(Color888(lnclBackIn[GetLoResInputY(pos.y)]), 0); // the attribute byte doesn't matter
        default:
            return kTransparentPixel; // should never happpen
    }
}

struct Attributes {
    uint priority;
    bool specColorCalc;
};

Attributes ToAttributes(uint pixelData) {
    Attributes attrs;
    attrs.priority = BitExtract(pixelData, 0, 3);
    attrs.specColorCalc = BitTest(pixelData, kPixelAttrBitSpecColorCalc);
    return attrs;
}

// ---------------------------------------------------------------------------------------------------------------------
// Compositor

uint3 Compose(uint2 basePos) {
    const uint2 pos = uint2(basePos.x, GetOutputY(basePos.y));

    // Clear screen if display is disabled
    const bool displayEnabled = BitTest(g_commonParams.displayParams, 0);
    if (!displayEnabled) {
        const bool borderColorMode = BitTest(g_commonParams.displayParams, 1);
        if (borderColorMode) {
            // Use back screen color
            return Color888(lnclBackIn[GetLoResInputY(pos.y) + kMaxResV]);
        }
        return uint3(0, 0, 0);
    }

    uint layerStack[3] = { kLayerBack, kLayerBack, kLayerBack };
    uint layerPrios[3] = { 0, 0, 0 };

    for (uint layer = 0; layer < 6; layer++) {
        // Skip disabled layers
        if (!IsLayerEnabled(layer)) {
            continue;
        }

        const uint4 layerOutput = GetLayerOutput(layer, pos);
        const Attributes attrs = ToAttributes(layerOutput.a);

        // Priority zero means transparent pixel
        if (attrs.priority == 0) {
            continue;
        }

        // Skip normal shadow sprite layer pixels
        if (layer == kLayerSprite) {
            const uint spriteAttrs = spriteAttrsIn[uint3(pos, 0)];
            if (BitTest(spriteAttrs, kSpriteAttrBitNormalShadow)) {
                continue;
            }
        }

        // Insert the layer into the appropriate position in the stack
        // - Higher priority beats lower priority
        // - If same priority, lower Layer index beats higher Layer index
        // - layerStack[0] is topmost (first) layer
        for (int i = 0; i < 3; i++) {
            if (attrs.priority > layerPrios[i] || (attrs.priority == layerPrios[i] && layer < layerStack[i])) {
                // Push layers back
                for (int j = 2; j > i; j--) {
                    layerStack[j] = layerStack[j - 1];
                    layerPrios[j] = layerPrios[j - 1];
                }
                layerStack[i] = layer;
                layerPrios[i] = attrs.priority;
                break;
            }
        }
    }

    // Find sprite mesh layer stack position
    uint meshLayer = 0xFF;
    uint3 meshPixel;
    if (transparentMeshes && IsLayerEnabled(kLayerSprite)) {
        const uint4 meshOutput = GetLayerOutput(kLayerMesh, pos);
        meshPixel = meshOutput.rgb;
        const Attributes meshAttrs = ToAttributes(meshOutput.a);
        const uint meshSpriteAttrs = spriteAttrsIn[uint3(pos, 1)];
        if (meshAttrs.priority > 0 && !BitTest(meshSpriteAttrs, kSpriteAttrBitNormalShadow)) {
            for (uint i = 0; i < 3; i++) {
                // The sprite layer has the highest priority on ties, so the priority check can be simplified.
                // Sprite pixels drawn of top of mesh pixels erase the corresponding pixels from the mesh layer,
                // therefore the mesh layer can be considered always on top of the sprite layer.
                if (meshAttrs.priority >= layerPrios[i]) {
                    meshLayer = i;
                    break;
                }
            }
        }
    }

    uint3 output = { 0, 0, 0 };

    const bool layer0LineColorEnabled = IsLineColorEnabled(layerStack[0], pos);
    const bool extendedColorCalc = BitTest(g_commonParams.layerParams, 25);

    uint3 layer0Pixel = GetLayerOutput(layerStack[0], pos).rgb;
    uint3 layer1Pixel = GetLayerOutput(layerStack[1], pos).rgb;

    if (extendedColorCalc) {
        if (IsColorCalcEnabled(layerStack[1], pos)) {
            uint3 layer2Pixel = GetLayerOutput(layerStack[2], pos).rgb;

            // Blend layer 2 with sprite mesh layer colors
            // TODO: apply color calculation effects
            if (transparentMeshes && meshLayer == 2) {
                layer2Pixel = (layer2Pixel + meshPixel) >> 1;
            }

            layer1Pixel = (layer1Pixel + layer2Pixel) >> 1;
        }

        if (layer0LineColorEnabled) {
            const uint3 lineColor = GetLineColor(layerStack[0], basePos);
            if (IsColorCalcEnabled(kLayerLine, pos)) {
                layer1Pixel = (layer1Pixel + lineColor) >> 1;
            } else {
                layer1Pixel = lineColor;
            }
        }
    } else if (layer0LineColorEnabled) {
        layer1Pixel = GetLineColor(layerStack[0], basePos);
    }

    // Blend layer 1 with sprite mesh layer colors
    // TODO: apply color calculation effects
    if (transparentMeshes && meshLayer == 1) {
        layer1Pixel = (layer1Pixel + meshPixel) >> 1;
    }

    if (IsColorCalcEnabled(layerStack[0], pos)) {
        const bool useAdditiveBlend = BitTest(g_commonParams.layerParams, 26);
        if (useAdditiveBlend) {
            output = min(layer0Pixel + layer1Pixel, 255);
        } else {
            const bool useSecondScreenRatio = BitTest(g_commonParams.layerParams, 27);
            const uint ratioLayer = useSecondScreenRatio ? layerStack[1] : layerStack[0];
            const int ratio = GetColorCalcRatio(ratioLayer, pos);
            output = int3(layer1Pixel) + (((int3(layer0Pixel) - int3(layer1Pixel)) * ratio) >> 5);
        }
    } else {
        output = layer0Pixel;
    }

    // Blend layer 0 with sprite mesh layer colors
    // TODO: apply color calculation effects
    if (transparentMeshes && meshLayer == 0) {
        output = (output + meshPixel) >> 1;
    }

    // Apply sprite shadow if sprite layer has a shadow pixel and is on top of the topmost layer
    const uint4 spriteOutput = GetLayerOutput(kLayerSprite, pos);
    const uint spritePriority = BitExtract(spriteOutput.a, 0, 3);
    if (spritePriority >= layerPrios[0]) {
        const uint spriteAttrs = spriteAttrsIn[uint3(pos, 0)];
        const bool useSpriteWindow = BitTest(g_commonParams.spriteParams, 19);
        const bool isNormalShadow = BitTest(spriteAttrs, kSpriteAttrBitNormalShadow);
        const bool isMSBShadow = !useSpriteWindow && BitTest(spriteAttrs, kSpriteAttrBitShadowWindow);
        if (isNormalShadow || isMSBShadow) {
            output >>= 1;
        }
    }

    if (IsColorOffsetEnabled(layerStack[0])) {
        const int3 offset = GetColorOffset(layerStack[0]);
        output = clamp(int3(output) + offset, 0, 255);
    }

    return output;
}

// ---------------------------------------------------------------------------------------------------------------------
// Entrypoint

[numthreads(32, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint2 drawCoord = uint2(id.x, id.y + g_commonParams.startY);
    const uint2 outCoord = uint2(drawCoord.x, GetOutputY(drawCoord.y));
    const uint3 outColor = Compose(drawCoord);
    compositeOut[outCoord] = float4(outColor / 255.0, 1.0f);
}
