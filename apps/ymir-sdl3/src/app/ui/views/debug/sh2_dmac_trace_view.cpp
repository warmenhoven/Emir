#include "sh2_dmac_trace_view.hpp"

#include <ymir/util/size_ops.hpp>

#include <app/imgui_data.hpp>

#include <cinttypes>

using namespace ymir;

namespace app::ui {

SH2DMAControllerChannelTraceView::SH2DMAControllerChannelTraceView(int index, SH2Tracer &tracer)
    : m_index(index)
    , m_tracer(tracer) {}

void SH2DMAControllerChannelTraceView::Display() {
    DisplayStatistics();
    DisplayTrace();
}

void SH2DMAControllerChannelTraceView::DisplayStatistics() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();

    ImGui::SeparatorText(fmt::format("Channel {} statistics", m_index).c_str());

    const auto &stats = m_tracer.dmaStats[m_index];

    ImGui::PushStyleVarX(ImGuiStyleVar_CellPadding, 8.0f);
    if (ImGui::BeginTable(fmt::format("dmac{}_stats", m_index).c_str(), 3, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextRow();

        if (ImGui::TableNextColumn()) {
            ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.medium);
            ImGui::Text("%" PRIu64, stats.numTransfers);
            ImGui::PopFont();
            ImGui::TextUnformatted("transfers");
        }

        if (ImGui::TableNextColumn()) {
            ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.medium);
            if (stats.bytesTransferred >= 1_TiB) {
                ImGui::Text("%0.2lf TiB", util::BytesToTiB(stats.bytesTransferred));
            } else if (stats.bytesTransferred >= 1_GiB) {
                ImGui::Text("%0.2lf GiB", util::BytesToGiB(stats.bytesTransferred));
            } else if (stats.bytesTransferred >= 1_MiB) {
                ImGui::Text("%0.2lf MiB", util::BytesToMiB(stats.bytesTransferred));
            } else if (stats.bytesTransferred >= 1_KiB) {
                ImGui::Text("%0.2lf KiB", util::BytesToKiB(stats.bytesTransferred));
            } else {
                ImGui::Text("%" PRIu64 " bytes", stats.bytesTransferred);
            }
            ImGui::PopFont();
            ImGui::TextUnformatted("transferred");
        }

        if (ImGui::TableNextColumn()) {
            ImGui::PushFont(imguiData->fonts.sansSerif.bold, imguiData->fontSizes.medium);
            ImGui::Text("%" PRIu64, stats.interrupts);
            ImGui::PopFont();
            ImGui::TextUnformatted("interrupts");
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

void SH2DMAControllerChannelTraceView::DisplayTrace() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    const float paddingWidth = ImGui::GetStyle().FramePadding.x;
    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();

    ImGui::SeparatorText(fmt::format("Channel {} trace", m_index).c_str());

    ImGui::BeginGroup();

    if (ImGui::Button(fmt::format("Clear##dmac{}", m_index).c_str())) {
        m_tracer.dmaTransfers[m_index].Clear();
        m_tracer.dmaStats[m_index].Clear();
        m_tracer.ResetDMACounter(m_index);
    }

    if (ImGui::BeginTable(fmt::format("dmac{}_trace", m_index).c_str(), 6,
                          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_PreferSortDescending);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                paddingWidth * 2 + hexCharWidth * 11);
        ImGui::TableSetupColumn("Destination", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                paddingWidth * 2 + hexCharWidth * 11);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                paddingWidth * 2 + hexCharWidth * 6);
        ImGui::TableSetupColumn("Unit size", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                paddingWidth * 2 + hexCharWidth * 6);
        ImGui::TableSetupColumn("IRQ", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableHeadersRow();

        const size_t count = m_tracer.dmaTransfers[m_index].Count();
        for (size_t i = 0; i < count; i++) {
            auto *sort = ImGui::TableGetSortSpecs();
            bool reverse = false;
            if (sort != nullptr && sort->SpecsCount == 1) {
                reverse = sort->Specs[0].SortDirection == ImGuiSortDirection_Descending;
            }

            auto trace =
                reverse ? m_tracer.dmaTransfers[m_index].ReadReverse(i) : m_tracer.dmaTransfers[m_index].Read(i);

            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                ImGui::Text("%u", trace.counter);
                ImGui::PopFont();
            }
            if (ImGui::TableNextColumn()) {
                ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                ImGui::Text("%08X", trace.srcAddress);
                ImGui::PopFont();
                ImGui::SameLine();
                ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.small);
                if (trace.srcInc > 0) {
                    ImGui::TextDisabled("+%d", trace.srcInc);
                } else if (trace.srcInc < 0) {
                    ImGui::TextDisabled("-%d", -trace.srcInc);
                }
                ImGui::PopFont();
            }
            if (ImGui::TableNextColumn()) {
                ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                ImGui::Text("%08X", trace.dstAddress);
                ImGui::PopFont();
                ImGui::SameLine();
                ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.small);
                if (trace.dstInc > 0) {
                    ImGui::TextDisabled("+%d", trace.dstInc);
                } else if (trace.dstInc < 0) {
                    ImGui::TextDisabled("-%d", -trace.dstInc);
                }
                ImGui::PopFont();
            }
            if (ImGui::TableNextColumn()) {
                ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                ImGui::Text("%X", trace.count);
                ImGui::PopFont();
            }
            if (ImGui::TableNextColumn()) {
                ImGui::Text("%X byte%s", trace.unitSize, (trace.unitSize != 1 ? "s" : ""));
            }
            if (ImGui::TableNextColumn()) {
                if (trace.irqRaised) {
                    ImGui::TextUnformatted("raised");
                }
            }
        }

        ImGui::EndTable();
    }

    ImGui::EndGroup();
}

} // namespace app::ui
