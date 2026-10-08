#include "sh2_dmac_registers_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <app/imgui_data.hpp>

using namespace ymir;

namespace app::ui {

SH2DMAControllerRegistersView::SH2DMAControllerRegistersView(ymir::sh2::SH2 &sh2)
    : m_sh2(sh2) {}

void SH2DMAControllerRegistersView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &probe = m_sh2.GetProbe();
    auto &dmaor = probe.DMAOR();
    auto &intc = probe.INTC();

    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();

    uint32 DMAOR = dmaor.Read();

    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 8);
    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    ImGui::InputScalar("##dmaor", ImGuiDataType_U32, &DMAOR, nullptr, nullptr, "%08X",
                       ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("DMAOR");
    ImGui::EndGroup();
    ImGui::SetItemTooltip("DMA Operation Register");

    ImGui::SameLine();
    ImGui::Checkbox("PR", &dmaor.PR);
    ImGui::SetItemTooltip("Priority Mode");

    ImGui::SameLine();
    ImGui::Checkbox("AE", &dmaor.AE);
    ImGui::SetItemTooltip("Address Error Flag");

    ImGui::SameLine();
    ImGui::Checkbox("NMIF", &dmaor.NMIF);
    ImGui::SetItemTooltip("NMI Flag");

    ImGui::SameLine();
    ImGui::Checkbox("DME", &dmaor.DME);
    ImGui::SetItemTooltip("DMA Master Enable");

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Interrupts:");
    ImGui::SameLine();
    {
        ImGui::BeginGroup();
        uint8 vector = intc.GetVector(sh2::InterruptSource::DMAC0_XferEnd);
        ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
        ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
        if (ImGui::InputScalar("##vcrdma0", ImGuiDataType_U8, &vector, nullptr, nullptr, "%02X",
                               ImGuiInputTextFlags_CharsHexadecimal)) {
            intc.SetVector(sh2::InterruptSource::DMAC0_XferEnd, vector);
        }
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextUnformatted("VCRDMA0");
        ImGui::EndGroup();
        ImGui::SetItemTooltip("DMA channel 0 transfer end vector");
    }
    ImGui::SameLine();
    {
        ImGui::BeginGroup();
        uint8 vector = intc.GetVector(sh2::InterruptSource::DMAC1_XferEnd);
        ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
        ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
        if (ImGui::InputScalar("##vcrdma1", ImGuiDataType_U8, &vector, nullptr, nullptr, "%02X",
                               ImGuiInputTextFlags_CharsHexadecimal)) {
            intc.SetVector(sh2::InterruptSource::DMAC1_XferEnd, vector);
        }
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextUnformatted("VCRDMA1");
        ImGui::EndGroup();
        ImGui::SetItemTooltip("DMA channel 1 transfer end vector");
    }
    ImGui::SameLine();
    {
        uint8 level = intc.GetLevel(sh2::InterruptSource::DMAC0_XferEnd);
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 1);
        ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
        if (ImGui::InputScalar("##ipra_dmacipn", ImGuiDataType_U8, &level, nullptr, nullptr, "%X",
                               ImGuiInputTextFlags_CharsHexadecimal)) {
            intc.SetLevel(sh2::InterruptSource::DMAC0_XferEnd, std::min<uint8>(level, 0xF));
        }
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextUnformatted("IPRA.DMACIP3-0");
        ImGui::EndGroup();
        ImGui::SetItemTooltip("DMA controller interrupt level");
    }
}

} // namespace app::ui
