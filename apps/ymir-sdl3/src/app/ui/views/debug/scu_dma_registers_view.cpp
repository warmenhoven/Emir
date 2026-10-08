#include "scu_dma_registers_view.hpp"

#include <ymir/hw/scu/scu.hpp>

#include <app/imgui_data.hpp>

using namespace ymir;

namespace app::ui {

SCUDMARegistersView::SCUDMARegistersView(ymir::scu::SCU &scu)
    : m_scu(scu) {}

void SCUDMARegistersView::Display(uint8 channel) {
    if (channel >= 3) {
        return;
    }

    const YmirImGuiData *imguiData = GetYmirImGuiData();

    const float frameHeight = ImGui::GetFrameHeight();
    const float paddingWidth = ImGui::GetStyle().FramePadding.x;
    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();

    auto &probe = m_scu.GetProbe();

    ImGui::BeginGroup();

    bool enabled = probe.IsDMAEnabled(channel);
    if (ImGui::Checkbox(fmt::format("Enabled##{}", channel).c_str(), &enabled)) {
        probe.SetDMAEnabled(channel, enabled);
    }
    ImGui::SameLine();
    if (!enabled) {
        ImGui::BeginDisabled();
    }
    bool indirect = probe.IsDMAIndirect(channel);
    if (ImGui::Checkbox(fmt::format("Indirect transfer##{}", channel).c_str(), &indirect)) {
        probe.SetDMAIndirect(channel, indirect);
    }

    if (ImGui::BeginTable(fmt::format("addrs_{}", channel).c_str(), 4, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, paddingWidth * 2 + hexCharWidth * 7);
        ImGui::TableSetupColumn("Update", ImGuiTableColumnFlags_WidthFixed, frameHeight);
        ImGui::TableSetupColumn("Increment", ImGuiTableColumnFlags_WidthFixed, paddingWidth * 2 + hexCharWidth * 3);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            uint32 srcAddr = probe.GetDMASourceAddress(channel);
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            ImGui::SetNextItemWidth(paddingWidth * 2 + hexCharWidth * 7);
            if (ImGui::InputScalar(fmt::format("##srcAddr_{}", channel).c_str(), ImGuiDataType_U32, &srcAddr, nullptr,
                                   nullptr, "%07X", ImGuiInputTextFlags_CharsHexadecimal)) {
                probe.SetDMASourceAddress(channel, srcAddr);
            }
            ImGui::PopFont();
        }
        if (ImGui::TableNextColumn()) {
            bool srcAddrInc = probe.IsDMAUpdateSourceAddress(channel);
            if (ImGui::Checkbox(fmt::format("##srcAddrInc_{}", channel).c_str(), &srcAddrInc)) {
                probe.SetDMAUpdateSourceAddress(channel, srcAddrInc);
            }
        }
        if (ImGui::TableNextColumn()) {
            uint32 srcAddrIncAmount = probe.GetDMASourceAddressIncrement(channel);
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            ImGui::SetNextItemWidth(paddingWidth * 2 + hexCharWidth * 3);
            if (ImGui::InputScalar(fmt::format("##srcAddrIncAmount_{}", channel).c_str(), ImGuiDataType_U32,
                                   &srcAddrIncAmount, nullptr, nullptr, "%u", ImGuiInputTextFlags_CharsHexadecimal)) {
                probe.SetDMASourceAddressIncrement(channel, srcAddrIncAmount);
            }
            ImGui::PopFont();
        }
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Source");
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            uint32 dstAddr = probe.GetDMADestinationAddress(channel);
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            ImGui::SetNextItemWidth(paddingWidth * 2 + hexCharWidth * 7);
            if (ImGui::InputScalar(fmt::format("##dstAddr_{}", channel).c_str(), ImGuiDataType_U32, &dstAddr, nullptr,
                                   nullptr, "%07X", ImGuiInputTextFlags_CharsHexadecimal)) {
                probe.SetDMADestinationAddress(channel, dstAddr);
            }
            ImGui::PopFont();
        }
        if (ImGui::TableNextColumn()) {
            bool dstAddrInc = probe.IsDMAUpdateDestinationAddress(channel);
            if (ImGui::Checkbox(fmt::format("##dstAddrInc_{}", channel).c_str(), &dstAddrInc)) {
                probe.SetDMAUpdateDestinationAddress(channel, dstAddrInc);
            }
        }
        if (ImGui::TableNextColumn()) {
            uint32 dstAddrIncAmount = probe.GetDMADestinationAddressIncrement(channel);
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            ImGui::SetNextItemWidth(paddingWidth * 2 + hexCharWidth * 3);
            if (ImGui::InputScalar(fmt::format("##dstAddrIncAmount_{}", channel).c_str(), ImGuiDataType_U32,
                                   &dstAddrIncAmount, nullptr, nullptr, "%u", ImGuiInputTextFlags_CharsHexadecimal)) {
                probe.SetDMADestinationAddressIncrement(channel, dstAddrIncAmount);
            }
            ImGui::PopFont();
        }
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Destination");
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            uint32 xferLen = probe.GetDMATransferCount(channel);
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            ImGui::SetNextItemWidth(paddingWidth * 2 + hexCharWidth * 7); // only 4 for channels 1 and 2
            if (ImGui::InputScalar(fmt::format("##xferLen_{}", channel).c_str(), ImGuiDataType_U32, &xferLen, nullptr,
                                   nullptr, "%u", ImGuiInputTextFlags_CharsHexadecimal)) {
                probe.SetDMATransferCount(channel, xferLen);
            }
            ImGui::PopFont();
        }
        if (ImGui::TableNextColumn()) {
            // empty
        }
        if (ImGui::TableNextColumn()) {
            // empty
        }
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Length");
        }

        ImGui::EndTable();
    }

    scu::DMATrigger trigger = probe.GetDMATrigger(channel);
    static constexpr const char *kTriggerNames[] = {
        "VDP2 VBlank IN", "VDP2 VBlank OUT",    "VDP2 HBlank IN",       "SCU Timer 0",
        "SCU Timer 1",    "SCSP Sound Request", "VDP1 Sprite Draw End", "Immediate",
    };
    if (ImGui::BeginCombo(fmt::format("Trigger##{}", channel).c_str(), kTriggerNames[static_cast<uint32>(trigger)],
                          ImGuiComboFlags_WidthFitPreview)) {
        for (uint32 i = 0; i < 8; i++) {
            if (ImGui::Selectable(kTriggerNames[i], i == static_cast<uint32>(trigger))) {
                probe.SetDMATrigger(channel, static_cast<scu::DMATrigger>(i));
            }
        }
        ImGui::EndCombo();
    }
    if (!enabled) {
        ImGui::EndDisabled();
    }

    ImGui::EndGroup();
}

} // namespace app::ui
