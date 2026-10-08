#include "sh2_dmac_channel_view.hpp"

#include <app/imgui_data.hpp>

using namespace ymir;

namespace app::ui {

SH2DMAControllerChannelView::SH2DMAControllerChannelView(ymir::sh2::DMAChannel &channel, int index)
    : m_channel(channel)
    , m_index(index) {}

void SH2DMAControllerChannelView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();

    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();

    auto calcSpacing = [&](const char *label) {
        const float len0 = ImGui::CalcTextSize(fmt::format("{}0", label).c_str()).x;
        const float len1 = ImGui::CalcTextSize(fmt::format("{}1", label).c_str()).x;
        const float currLen = m_index == 0 ? len0 : len1;
        const float maxLen = std::max(len0, len1);
        return maxLen - currLen + ImGui::GetStyle().ItemSpacing.x;
    };

    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 8);
    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    ImGui::InputScalar(fmt::format("##sar{}", m_index).c_str(), ImGuiDataType_U32, &m_channel.srcAddress, nullptr,
                       nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("SAR%d", m_index);
    ImGui::EndGroup();
    ImGui::SetItemTooltip("DMA Source Address Register");

    ImGui::SameLine(0, calcSpacing("SAR"));

    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 8);
    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    ImGui::InputScalar(fmt::format("##dar{}", m_index).c_str(), ImGuiDataType_U32, &m_channel.dstAddress, nullptr,
                       nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("DAR%d", m_index);
    ImGui::EndGroup();
    ImGui::SetItemTooltip("DMA Destination Address Register");

    ImGui::SameLine(0, calcSpacing("DAR"));

    uint32 xferCount = m_channel.xferCount;
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 6);
    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    if (ImGui::InputScalar(fmt::format("##tcr{}", m_index).c_str(), ImGuiDataType_U32, &xferCount, nullptr, nullptr,
                           "%06X", ImGuiInputTextFlags_CharsHexadecimal)) {
        m_channel.xferCount = xferCount;
    }
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("TCR%d", m_index);
    ImGui::EndGroup();
    ImGui::SetItemTooltip("DMA Transfer Count Register");

    ImGui::SameLine(0, calcSpacing("TCR"));

    uint32 CHCR = m_channel.ReadCHCR();
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 8);
    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    if (ImGui::InputScalar(fmt::format("##chcr{}", m_index).c_str(), ImGuiDataType_U32, &CHCR, nullptr, nullptr, "%08X",
                           ImGuiInputTextFlags_CharsHexadecimal)) {
        m_channel.WriteCHCR<true>(CHCR);
    }
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("CHCR%d", m_index);
    ImGui::EndGroup();
    ImGui::SetItemTooltip("DMA Channel Control Register");

    ImGui::SameLine(0, calcSpacing("CHCR"));

    uint8 DRCR = m_channel.ReadDRCR();
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    if (ImGui::InputScalar(fmt::format("##drcr{}", m_index).c_str(), ImGuiDataType_U8, &DRCR, nullptr, nullptr, "%02X",
                           ImGuiInputTextFlags_CharsHexadecimal)) {
        m_channel.WriteDRCR(DRCR);
    }
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("DRCR%d", m_index);
    ImGui::EndGroup();
    ImGui::SetItemTooltip("DMA Request/Response Selection Control Register");

    ImGui::Separator();

    ImGui::Checkbox(fmt::format("Enabled##{}", m_index).c_str(), &m_channel.xferEnabled);
    ImGui::SameLine();
    ImGui::Checkbox(fmt::format("Interrupt enable##{}", m_index).c_str(), &m_channel.irqEnable);
    ImGui::SameLine();
    ImGui::Checkbox(fmt::format("Auto-request mode##{}", m_index).c_str(), &m_channel.autoRequest);
    ImGui::SameLine();
    ImGui::Checkbox(fmt::format("Transfer ended##{}", m_index).c_str(), &m_channel.xferEnded);

    if (ImGui::BeginTable("chcr_values", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Source address mode");
        }
        if (ImGui::TableNextColumn()) {
            if (ImGui::RadioButton("Fixed##src", m_channel.srcMode == sh2::DMATransferIncrementMode::Fixed)) {
                m_channel.srcMode = sh2::DMATransferIncrementMode::Fixed;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Increment##src", m_channel.srcMode == sh2::DMATransferIncrementMode::Increment)) {
                m_channel.srcMode = sh2::DMATransferIncrementMode::Increment;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Decrement##src", m_channel.srcMode == sh2::DMATransferIncrementMode::Decrement)) {
                m_channel.srcMode = sh2::DMATransferIncrementMode::Decrement;
            }
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Destination address mode");
        }
        if (ImGui::TableNextColumn()) {
            if (ImGui::RadioButton("Fixed##dst", m_channel.dstMode == sh2::DMATransferIncrementMode::Fixed)) {
                m_channel.dstMode = sh2::DMATransferIncrementMode::Fixed;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Increment##dst", m_channel.dstMode == sh2::DMATransferIncrementMode::Increment)) {
                m_channel.dstMode = sh2::DMATransferIncrementMode::Increment;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Decrement##dst", m_channel.dstMode == sh2::DMATransferIncrementMode::Decrement)) {
                m_channel.dstMode = sh2::DMATransferIncrementMode::Decrement;
            }
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Transfer unit size");
        }
        if (ImGui::TableNextColumn()) {
            if (ImGui::RadioButton("8-bit##xfer_size", m_channel.xferSize == sh2::DMATransferSize::Byte)) {
                m_channel.xferSize = sh2::DMATransferSize::Byte;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("16-bit##xfer_size", m_channel.xferSize == sh2::DMATransferSize::Word)) {
                m_channel.xferSize = sh2::DMATransferSize::Word;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("32-bit##xfer_size", m_channel.xferSize == sh2::DMATransferSize::Longword)) {
                m_channel.xferSize = sh2::DMATransferSize::Longword;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("4x32-bit##xfer_size", m_channel.xferSize == sh2::DMATransferSize::QuadLongword)) {
                m_channel.xferSize = sh2::DMATransferSize::QuadLongword;
            }
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Transfer bus mode");
        }
        if (ImGui::TableNextColumn()) {
            if (ImGui::RadioButton("Cycle-steal##xfer_bus_mode",
                                   m_channel.xferBusMode == sh2::DMATransferBusMode::CycleSteal)) {
                m_channel.xferBusMode = sh2::DMATransferBusMode::CycleSteal;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Burst##xfer_bus_mode", m_channel.xferBusMode == sh2::DMATransferBusMode::Burst)) {
                m_channel.xferBusMode = sh2::DMATransferBusMode::Burst;
            }
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Transfer address mode");
        }
        if (ImGui::TableNextColumn()) {
            if (ImGui::RadioButton("Dual address##xfer_addr_mode",
                                   m_channel.xferAddressMode == sh2::DMATransferAddressMode::Dual)) {
                m_channel.xferAddressMode = sh2::DMATransferAddressMode::Dual;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Single address##xfer_addr_mode",
                                   m_channel.xferAddressMode == sh2::DMATransferAddressMode::Single)) {
                m_channel.xferAddressMode = sh2::DMATransferAddressMode::Single;
            }
        }

        const bool dualAddrMode = m_channel.xferAddressMode == sh2::DMATransferAddressMode::Dual;
        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(dualAddrMode ? "Acknowledge mode" : "Transfer mode");
        }
        if (ImGui::TableNextColumn()) {
            if (ImGui::RadioButton(dualAddrMode ? "During read cycle##ack_xfer_mode"
                                                : "Memory -> Device##ack_xfer_mode",
                                   !m_channel.ackXferMode)) {
                m_channel.ackXferMode = false;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton(dualAddrMode ? "During write cycle##ack_xfer_mode"
                                                : "Device -> Memory##ack_xfer_mode",
                                   m_channel.ackXferMode)) {
                m_channel.ackXferMode = true;
            }
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("DACK level");
        }
        if (ImGui::TableNextColumn()) {
            if (ImGui::RadioButton("Active-high##dack", !m_channel.ackLevel)) {
                m_channel.ackLevel = false;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Active-low##dack", m_channel.ackLevel)) {
                m_channel.ackLevel = true;
            }
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Request/response selection");
        }
        if (ImGui::TableNextColumn()) {
            if (ImGui::RadioButton("DREQ##res", m_channel.resSelect == sh2::DMAResourceSelect::DREQ)) {
                m_channel.resSelect = sh2::DMAResourceSelect::DREQ;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("RXI##res", m_channel.resSelect == sh2::DMAResourceSelect::RXI)) {
                m_channel.resSelect = sh2::DMAResourceSelect::RXI;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("TXI##res", m_channel.resSelect == sh2::DMAResourceSelect::TXI)) {
                m_channel.resSelect = sh2::DMAResourceSelect::TXI;
            }
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("DREQ select");
        }
        if (ImGui::TableNextColumn()) {
            if (ImGui::RadioButton("Detect by level##dreq", m_channel.dreqSelect == sh2::SignalDetectionMode::Level)) {
                m_channel.dreqSelect = sh2::SignalDetectionMode::Level;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Detect by edge##dreq", m_channel.dreqSelect == sh2::SignalDetectionMode::Edge)) {
                m_channel.dreqSelect = sh2::SignalDetectionMode::Edge;
            }
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("DREQ level");
        }
        if (ImGui::TableNextColumn()) {
            const bool levelDetect = m_channel.dreqSelect == sh2::SignalDetectionMode::Level;
            if (ImGui::RadioButton(levelDetect ? "Low level##dreq" : "Falling edge##dreq", !m_channel.dreqLevel)) {
                m_channel.dreqLevel = false;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton(levelDetect ? "High level##dreq" : "Rising edge##dreq", m_channel.dreqLevel)) {
                m_channel.dreqLevel = true;
            }
        }

        ImGui::EndTable();
    }
}

} // namespace app::ui
