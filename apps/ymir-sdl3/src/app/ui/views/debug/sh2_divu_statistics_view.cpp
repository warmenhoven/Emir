#include "sh2_divu_statistics_view.hpp"

#include <app/imgui_data.hpp>

#include <cinttypes>

using namespace ymir;

namespace app::ui {

SH2DivisionUnitStatisticsView::SH2DivisionUnitStatisticsView(SH2Tracer &tracer)
    : m_tracer(tracer) {}

void SH2DivisionUnitStatisticsView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();

    ImGui::BeginGroup();

    ImGui::PushStyleVarX(ImGuiStyleVar_CellPadding, 8.0f);
    if (ImGui::BeginTable("div_stats", 4, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextRow();

        if (ImGui::TableNextColumn()) {
            ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.medium);
            ImGui::Text("%" PRIu64, m_tracer.divStats.div32s);
            ImGui::PopFont();
            ImGui::TextUnformatted("32x32 divisions");
        }

        if (ImGui::TableNextColumn()) {
            ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.medium);
            ImGui::Text("%" PRIu64, m_tracer.divStats.div64s);
            ImGui::PopFont();
            ImGui::TextUnformatted("64x32 divisions");
        }

        if (ImGui::TableNextColumn()) {
            ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.medium);
            ImGui::Text("%" PRIu64, m_tracer.divStats.overflows);
            ImGui::PopFont();
            ImGui::TextUnformatted("overflows");
        }

        if (ImGui::TableNextColumn()) {
            ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.medium);
            ImGui::Text("%" PRIu64, m_tracer.divStats.interrupts);
            ImGui::PopFont();
            ImGui::TextUnformatted("interrupts");
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    ImGui::EndGroup();
}

} // namespace app::ui
