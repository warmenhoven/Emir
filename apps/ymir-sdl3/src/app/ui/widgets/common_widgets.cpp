#include "common_widgets.hpp"

#include <app/imgui_data.hpp>

#include <app/ui/fonts/IconsMaterialSymbols.h>

#include <imgui.h>

namespace app::ui::widgets {

void ExplanationTooltip(const char *explanation, bool sameLine) {
    if (sameLine) {
        ImGui::SameLine();
    }
    ImGui::TextDisabled(ICON_MS_HELP);
    if (ImGui::BeginItemTooltip()) {
        const YmirImGuiData *imguiData = GetYmirImGuiData();
        ImGui::PushTextWrapPos(450.0f * imguiData->displayScale);
        ImGui::TextUnformatted(explanation);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void WarningTooltip(const char *explanation, bool sameLine) {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    if (sameLine) {
        ImGui::SameLine();
    }
    ImGui::TextColored(imguiData->colors.notice, ICON_MS_WARNING);
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(450.0f * imguiData->displayScale);
        ImGui::TextUnformatted(explanation);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

} // namespace app::ui::widgets