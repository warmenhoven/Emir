#ifndef YMIR_VDP_VDP2_DEFS_HLSLI
#define YMIR_VDP_VDP2_DEFS_HLSLI

static const uint kVDP1FBRAMSize = 256 * 1024;

static const uint kResolutionsH[4] = { 320, 352, 640, 704 };
static const uint kResolutionsV[4] = { 224, 240, 256, 256 };

static const uint kInterlaceModeNone = 0;
static const uint kInterlaceModeInvalid = 1;
static const uint kInterlaceModeSingleDensity = 2;
static const uint kInterlaceModeDoubleDensity = 3;

static const uint kColorFormatPalette16 = 0;
static const uint kColorFormatPalette256 = 1;
static const uint kColorFormatPalette2048 = 2;
static const uint kColorFormatRGB555 = 3;
static const uint kColorFormatRGB888 = 4;

static const uint kPriorityModeScreen = 0;
static const uint kPriorityModeCharacter = 1;
static const uint kPriorityModeDot = 2;

static const uint kSpecColorCalcModeScreen = 0;
static const uint kSpecColorCalcModeCharacter = 1;
static const uint kSpecColorCalcModeDot = 2;
static const uint kSpecColorCalcModeColorMSB = 3;

static const uint kPageSizes[2][2] = { { 13, 14 }, { 11, 12 } };

static const uint kMaxNormalResH = 352;
static const uint kMaxNormalResV = 256;

static const uint kMaxResH = 704;
static const uint kMaxResV = 512;

static const uint kRotParamLinePitch = kMaxNormalResH;
static const uint kRotParamEntryStride = kRotParamLinePitch * kMaxNormalResV;

static const uint kRotParamA = 0;
static const uint kRotParamB = 1;

static const uint kRotParamModeA = 0;
static const uint kRotParamModeB = 1;
static const uint kRotParamModeCoeff = 2;
static const uint kRotParamModeWindow = 3;

static const uint kCoeffDataModeScaleCoeffXY = 0;
static const uint kCoeffDataModeScaleCoeffX = 1;
static const uint kCoeffDataModeScaleCoeffY = 2;
static const uint kCoeffDataModeViewpointX = 3;

static const uint kScreenOverProcessRepeat = 0;
static const uint kScreenOverProcessRepeatChar = 1;
static const uint kScreenOverProcessTransparent = 2;
static const uint kScreenOverProcessFixed512 = 3;

static const uint kPixelAttrBitPaletteFormat = 6;
static const uint kPixelAttrBitSpecColorCalc = 7;

static const uint kSpriteAttrBitColorCalcRatio = 0; // bits 0 to 4
static const uint kSpriteAttrBitColorMSB = 5;
static const uint kSpriteAttrBitShadowWindow = 6;
static const uint kSpriteAttrBitNormalShadow = 7;

static const uint kColorGradScreenSprite = 0;
static const uint kColorGradScreenRBG0 = 1;
static const uint kColorGradScreenNBG0_RBG0 = 2;
static const uint kColorGradScreenNBG1_EXBG = 4;
static const uint kColorGradScreenNBG2 = 5;
static const uint kColorGradScreenNBG3 = 6;

static const uint kLayerIndexNBG0 = 0;
static const uint kLayerIndexNBG1 = 1;
static const uint kLayerIndexNBG2 = 2;
static const uint kLayerIndexNBG3 = 3;
static const uint kLayerIndexRBG0 = 4;
static const uint kLayerIndexRBG1 = 5;
static const uint kLayerIndexSprite = 6;
static const uint kLayerIndexMesh = 7;

static const uint4 kTransparentPixel = uint4(0, 0, 0, 0);

#endif
