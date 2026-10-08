#include "scu_dsp_registers_view.hpp"

#include <ymir/hw/scu/scu.hpp>

#include <app/imgui_data.hpp>

namespace app::ui {

SCUDSPRegistersView::SCUDSPRegistersView(ymir::scu::SCU &scu)
    : m_scu(scu) {}

void SCUDSPRegistersView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    const float flagsSpacing = 4.0f;
    const float cellPaddingHeight = ImGui::GetStyle().CellPadding.y;
    const float frameHeight = ImGui::GetFrameHeight();
    const float framePadding = ImGui::GetStyle().FramePadding.x;
    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();

    auto &dsp = m_scu.GetDSP();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(flagsSpacing * imguiData->displayScale, cellPaddingHeight));

    ImGui::BeginGroup();
    ImGui::Spacing();
    ImGui::Checkbox("##reg_S", &dsp.sign);
    ImGui::NewLine();
    ImGui::SameLine(0, (frameHeight - ImGui::CalcTextSize("S").x) / 2);
    ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, ImGui::GetStyle().FramePadding.y + cellPaddingHeight);
    ImGui::AlignTextToFramePadding();
    ImGui::PopStyleVar();
    ImGui::TextUnformatted("S");
    ImGui::EndGroup();

    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::Spacing();
    ImGui::Checkbox("##reg_Z", &dsp.zero);
    ImGui::NewLine();
    ImGui::SameLine(0, (frameHeight - ImGui::CalcTextSize("Z").x) / 2);
    ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, ImGui::GetStyle().FramePadding.y + cellPaddingHeight);
    ImGui::AlignTextToFramePadding();
    ImGui::PopStyleVar();
    ImGui::TextUnformatted("Z");
    ImGui::EndGroup();

    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::Spacing();
    ImGui::Checkbox("##reg_C", &dsp.carry);
    ImGui::NewLine();
    ImGui::SameLine(0, (frameHeight - ImGui::CalcTextSize("C").x) / 2);
    ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, ImGui::GetStyle().FramePadding.y + cellPaddingHeight);
    ImGui::AlignTextToFramePadding();
    ImGui::PopStyleVar();
    ImGui::TextUnformatted("C");
    ImGui::EndGroup();

    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::Spacing();
    ImGui::Checkbox("##reg_V", &dsp.overflow);
    ImGui::NewLine();
    ImGui::SameLine(0, (frameHeight - ImGui::CalcTextSize("V").x) / 2);
    ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, ImGui::GetStyle().FramePadding.y + cellPaddingHeight);
    ImGui::AlignTextToFramePadding();
    ImGui::PopStyleVar();
    ImGui::TextUnformatted("V");
    ImGui::EndGroup();

    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::Spacing();
    ImGui::Checkbox("##reg_T0", &dsp.dmaRun);
    ImGui::NewLine();
    ImGui::SameLine(0, (frameHeight - ImGui::CalcTextSize("T0").x) / 2);
    ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, ImGui::GetStyle().FramePadding.y + cellPaddingHeight);
    ImGui::AlignTextToFramePadding();
    ImGui::PopStyleVar();
    ImGui::TextUnformatted("T0");
    ImGui::EndGroup();

    ImGui::PopStyleVar();

    ImGui::SameLine(0.0f, 16.0f * imguiData->displayScale);

    if (ImGui::BeginTable("scu_dsp_regs", 8, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("PC");
        }
        if (ImGui::TableNextColumn()) {
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            ImGui::SetNextItemWidth(framePadding * 2 + hexCharWidth * 2);
            ImGui::InputScalar("##reg_pc", ImGuiDataType_U8, &dsp.PC, nullptr, nullptr, "%02X",
                               ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::PopFont();

            ImGui::SameLine();

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("LOP");
            ImGui::SameLine();
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            ImGui::SetNextItemWidth(framePadding * 2 + hexCharWidth * 3);
            uint16 lop = dsp.loopCount;
            if (ImGui::InputScalar("##reg_lop", ImGuiDataType_U16, &lop, nullptr, nullptr, "%03X",
                                   ImGuiInputTextFlags_CharsHexadecimal)) {
                dsp.loopCount = lop & 0xFFF;
            }
            ImGui::PopFont();

            ImGui::SameLine();

            ImGui::TextUnformatted("TOP");
            ImGui::SameLine();
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            ImGui::SetNextItemWidth(framePadding * 2 + hexCharWidth * 2);
            ImGui::InputScalar("##reg_top", ImGuiDataType_U8, &dsp.loopTop, nullptr, nullptr, "%02X",
                               ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::PopFont();
        }
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("RA0");
        }
        if (ImGui::TableNextColumn()) {
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            ImGui::SetNextItemWidth(framePadding * 2 + hexCharWidth * 7);
            if (ImGui::InputScalar("##reg_ra0", ImGuiDataType_S32, &dsp.dmaReadAddr, nullptr, nullptr, "%07X",
                                   ImGuiInputTextFlags_CharsHexadecimal)) {
                dsp.dmaReadAddr &= 0x7FF'FFFC;
            }
            ImGui::PopFont();
        }
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("AC");
        }
        if (ImGui::TableNextColumn()) {
            uint64 ac = dsp.AC.u64;
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            ImGui::SetNextItemWidth(framePadding * 2 + hexCharWidth * 12);
            if (ImGui::InputScalar("##reg_ac", ImGuiDataType_U64, &ac, nullptr, nullptr, "%012X",
                                   ImGuiInputTextFlags_CharsHexadecimal)) {
                dsp.AC.u64 = ac;
            }
            ImGui::PopFont();
        }
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("RX");
        }
        if (ImGui::TableNextColumn()) {
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            ImGui::SetNextItemWidth(framePadding * 2 + hexCharWidth * 8);
            ImGui::InputScalar("##reg_rx", ImGuiDataType_S32, &dsp.RX, nullptr, nullptr, "%08X",
                               ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::PopFont();
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("CT");
        }
        if (ImGui::TableNextColumn()) {
            ImGui::PushStyleVarX(ImGuiStyleVar_ItemSpacing, flagsSpacing);
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            for (uint32 i = 0; i < 4; i++) {
                uint8 ct = dsp.CT.array[i];
                if (i > 0) {
                    ImGui::SameLine();
                }
                ImGui::SetNextItemWidth(framePadding * 2 + hexCharWidth * 2);
                if (ImGui::InputScalar(fmt::format("##reg_ct{}", i).c_str(), ImGuiDataType_U8, &ct, nullptr, nullptr,
                                       "%02X", ImGuiInputTextFlags_CharsHexadecimal)) {
                    dsp.CT.array[i] = ct & 0x3F;
                }
            }
            ImGui::PopFont();
            ImGui::PopStyleVar();
        }
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("WA0");
        }
        if (ImGui::TableNextColumn()) {
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            ImGui::SetNextItemWidth(framePadding * 2 + hexCharWidth * 7);
            if (ImGui::InputScalar("##reg_wa0", ImGuiDataType_S32, &dsp.dmaWriteAddr, nullptr, nullptr, "%07X",
                                   ImGuiInputTextFlags_CharsHexadecimal)) {
                dsp.dmaWriteAddr &= 0x7FF'FFFC;
            }
            ImGui::PopFont();
        }
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("P");
        }
        if (ImGui::TableNextColumn()) {
            uint64 p = dsp.P.u64;
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            ImGui::SetNextItemWidth(framePadding * 2 + hexCharWidth * 12);
            if (ImGui::InputScalar("##reg_p", ImGuiDataType_U64, &p, nullptr, nullptr, "%012X",
                                   ImGuiInputTextFlags_CharsHexadecimal)) {
                dsp.P.u64 = p;
            }
            ImGui::PopFont();
        }
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("RY");
        }
        if (ImGui::TableNextColumn()) {
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            ImGui::SetNextItemWidth(framePadding * 2 + hexCharWidth * 8);
            ImGui::InputScalar("##reg_ry", ImGuiDataType_S32, &dsp.RY, nullptr, nullptr, "%08X",
                               ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::PopFont();
        }

        ImGui::EndTable();
    }
}

} // namespace app::ui
