#include "scu_interrupt_trace_view.hpp"

#include <app/imgui_data.hpp>

namespace app::ui {

SCUInterruptTraceView::SCUInterruptTraceView(SCUTracer &tracer)
    : m_tracer(tracer) {}

void SCUInterruptTraceView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    const float paddingWidth = ImGui::GetStyle().FramePadding.x;
    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();

    constexpr const char *kNames[] = {"VDP2 VBlank IN",      "VDP2 VBlank OUT",     "VDP2 HBlank IN",
                                      "SCU Timer 0",         "SCU Timer 1",         "SCU DSP End",
                                      "SCSP Sound Request",  "SMPC System Manager", "SMPC PAD Interrupt",
                                      "SCU Level 2 DMA End", "SCU Level 1 DMA End", "SCU Level 0 DMA End",
                                      "SCU DMA-illegal",     "VDP1 Sprite Draw End"};

    ImGui::BeginGroup();

    ImGui::Checkbox("Enable", &m_tracer.traceInterrupts);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("You must also enable tracing in Debug > Enable tracing (F11)");
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        m_tracer.ClearInterrupts();
    }

    if (ImGui::BeginTable("intr_trace", 4,
                          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_PreferSortDescending);
        ImGui::TableSetupColumn("Lv", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                paddingWidth * 2 + hexCharWidth * 1);
        ImGui::TableSetupColumn("Ack", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                paddingWidth * 2 + ImGui::CalcTextSize("yes").x);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableHeadersRow();

        const size_t count = m_tracer.interrupts.Count();
        for (size_t i = 0; i < count; i++) {
            auto *sort = ImGui::TableGetSortSpecs();
            bool reverse = false;
            if (sort != nullptr && sort->SpecsCount == 1) {
                reverse = sort->Specs[0].SortDirection == ImGuiSortDirection_Descending;
            }

            auto trace = reverse ? m_tracer.interrupts.ReadReverse(i) : m_tracer.interrupts.Read(i);

            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                ImGui::Text("%u", trace.counter);
                ImGui::PopFont();
            }
            if (ImGui::TableNextColumn()) {
                ImGui::Text("%X", trace.level);
            }
            if (ImGui::TableNextColumn()) {
                if (trace.acknowledged) {
                    ImGui::TextUnformatted("yes");
                }
            }
            if (ImGui::TableNextColumn()) {
                if (trace.index < std::size(kNames)) {
                    ImGui::Text("%s", kNames[trace.index]);
                } else if (trace.index >= 16) {
                    ImGui::Text("External %X", trace.index - 16);
                } else {
                    ImGui::Text("Invalid (%X)", trace.index);
                }
            }
        }

        ImGui::EndTable();
    }

    ImGui::EndGroup();
}

} // namespace app::ui
