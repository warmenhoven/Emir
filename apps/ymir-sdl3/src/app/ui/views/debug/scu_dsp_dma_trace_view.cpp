#include "scu_dsp_dma_trace_view.hpp"

#include <app/imgui_data.hpp>

namespace app::ui {

SCUDSPDMATraceView::SCUDSPDMATraceView(SCUTracer &tracer)
    : m_tracer(tracer) {}

void SCUDSPDMATraceView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    const float paddingWidth = ImGui::GetStyle().FramePadding.x;
    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();

    ImGui::BeginGroup();

    ImGui::Checkbox("Enable", &m_tracer.traceDSPDMA);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("You must also enable tracing in Debug > Enable tracing (F11)");
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        m_tracer.ClearDSPDMATransfers();
    }

    if (ImGui::BeginTable("dsp_dma_trace", 4,
                          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_PreferSortDescending);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                paddingWidth * 2 + hexCharWidth * 10);
        ImGui::TableSetupColumn("Destination", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                paddingWidth * 2 + hexCharWidth * 10);
        ImGui::TableSetupColumn("Len", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                paddingWidth * 2 + hexCharWidth * 3);
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableHeadersRow();

        const size_t count = m_tracer.dspDmaTransfers.Count();
        for (size_t i = 0; i < count; i++) {
            auto *sort = ImGui::TableGetSortSpecs();
            bool reverse = false;
            if (sort != nullptr && sort->SpecsCount == 1) {
                reverse = sort->Specs[0].SortDirection == ImGuiSortDirection_Descending;
            }

            auto trace = reverse ? m_tracer.dspDmaTransfers.ReadReverse(i) : m_tracer.dspDmaTransfers.Read(i);

            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                ImGui::Text("%u", trace.counter);
                ImGui::PopFont();
            }
            if (ImGui::TableNextColumn()) {
                if (trace.toD0) {
                    switch (trace.addrDSP) {
                    case 0: ImGui::TextUnformatted("Data RAM 0"); break;
                    case 1: ImGui::TextUnformatted("Data RAM 1"); break;
                    case 2: ImGui::TextUnformatted("Data RAM 2"); break;
                    case 3: ImGui::TextUnformatted("Data RAM 3"); break;
                    default: ImGui::Text("Invalid (%u)", trace.addrDSP); break;
                    }
                } else {
                    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                    ImGui::Text("%07X", trace.addrD0);
                    ImGui::PopFont();
                    ImGui::SameLine();
                    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.small);
                    if (trace.addrInc > 0) {
                        if (trace.hold) {
                            ImGui::TextDisabled("(+%d)", trace.addrInc);
                        } else {
                            ImGui::TextDisabled("+%d", trace.addrInc);
                        }
                    } else if (trace.addrInc < 0) {
                        if (trace.hold) {
                            ImGui::TextDisabled("(-%d)", -trace.addrInc);
                        } else {
                            ImGui::TextDisabled("-%d", -trace.addrInc);
                        }
                    }
                    ImGui::PopFont();
                }
            }
            if (ImGui::TableNextColumn()) {
                if (trace.toD0) {
                    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                    ImGui::Text("%07X", trace.addrD0);
                    ImGui::PopFont();
                    ImGui::SameLine();
                    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.small);
                    if (trace.addrInc > 0) {
                        ImGui::TextDisabled("+%d", trace.addrInc);
                    } else if (trace.addrInc < 0) {
                        ImGui::TextDisabled("-%d", -trace.addrInc);
                    }
                    ImGui::PopFont();
                } else {
                    switch (trace.addrDSP) {
                    case 0: ImGui::TextUnformatted("Data RAM 0"); break;
                    case 1: ImGui::TextUnformatted("Data RAM 1"); break;
                    case 2: ImGui::TextUnformatted("Data RAM 2"); break;
                    case 3: ImGui::TextUnformatted("Data RAM 3"); break;
                    case 4: ImGui::TextUnformatted("Program RAM"); break;
                    default: ImGui::Text("Invalid (%u)", trace.addrDSP); break;
                    }
                }
            }
            if (ImGui::TableNextColumn()) {
                ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                ImGui::Text("%X", trace.count == 0 ? 0x100 : trace.count);
                ImGui::PopFont();
            }
        }

        ImGui::EndTable();
    }

    ImGui::EndGroup();
}

} // namespace app::ui
