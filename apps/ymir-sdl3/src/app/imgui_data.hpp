#pragma once

#include <imgui.h>

namespace app {

struct YmirImGuiData {
    struct Fonts {
        struct {
            ImFont *regular = nullptr;
            ImFont *bold = nullptr;
        } sansSerif;

        struct {
            ImFont *regular = nullptr;
            ImFont *bold = nullptr;
        } monospace;

        ImFont *display = nullptr;
    } fonts;

    struct FontSizes {
        float small = 14.0f;
        float medium = 16.0f;
        float large = 20.0f;
        float xlarge = 28.0f;

        float display = 64.0f;
        float displaySmall = 24.0f;
    } fontSizes;

    struct Colors {
        ImVec4 good{0.25f, 1.00f, 0.41f, 1.00f};
        ImVec4 notice{1.00f, 0.71f, 0.25f, 1.00f};
        ImVec4 warn{1.00f, 0.41f, 0.25f, 1.00f};

        ImVec4 purple{0.93f, 0.51f, 1.00f, 1.00f};
        ImVec4 blue{0.51f, 0.51f, 1.00f, 1.00f};
        ImVec4 cyan{0.25f, 0.81f, 1.00f, 1.00f};
        ImVec4 green{0.25f, 1.00f, 0.41f, 1.00f};
        ImVec4 yellow{1.00f, 0.88f, 0.25f, 1.00f};
        ImVec4 orange{1.00f, 0.71f, 0.25f, 1.00f};
        ImVec4 red{1.00f, 0.41f, 0.25f, 1.00f};
    } colors;

    float displayScale = 1.0f;
};

inline YmirImGuiData *GetYmirImGuiData() {
    return static_cast<YmirImGuiData *>(ImGui::GetIO().UserData);
}

} // namespace app
