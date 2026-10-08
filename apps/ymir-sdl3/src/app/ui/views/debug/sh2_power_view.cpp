#include "sh2_power_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <app/imgui_data.hpp>

#include <app/ui/widgets/common_widgets.hpp>

using namespace ymir;

namespace app::ui {

SH2PowerView::SH2PowerView(SharedContext &context, ymir::sh2::SH2 &sh2)
    : m_context(context)
    , m_sh2(sh2) {}

void SH2PowerView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &probe = m_sh2.GetProbe();
    auto &sbycr = probe.SBYCR();

    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();

    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    ImGui::InputScalar("##sbycr", ImGuiDataType_U8, &sbycr.u8, nullptr, nullptr, "%02X",
                       ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("SBYCR");
    ImGui::EndGroup();
    ImGui::SetItemTooltip("Standby Control Register");

    bool value;

    value = sbycr.MSTP0;
    if (ImGui::Checkbox("Halt and reset SCI", &value)) {
        sbycr.MSTP0 = value;
    }
    widgets::ExplanationTooltip("Serial Communication Interface");

    value = sbycr.MSTP1;
    if (ImGui::Checkbox("Halt and reset FRT", &value)) {
        sbycr.MSTP1 = value;
    }
    widgets::ExplanationTooltip("Free-running timer");

    value = sbycr.MSTP2;
    if (ImGui::Checkbox("Halt and reset DIVU", &value)) {
        sbycr.MSTP2 = value;
    }
    widgets::ExplanationTooltip("Division unit");

    value = sbycr.MSTP3;
    if (ImGui::Checkbox("Halt and reset MULT", &value)) {
        sbycr.MSTP3 = value;
    }
    widgets::ExplanationTooltip("Multiplication unit");

    value = sbycr.MSTP4;
    if (ImGui::Checkbox("Halt and reset DMAC", &value)) {
        sbycr.MSTP4 = value;
    }
    widgets::ExplanationTooltip("DMA controller");

    value = sbycr.HIZ;
    if (ImGui::Checkbox("Port high impedance", &value)) {
        sbycr.HIZ = value;
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Mode:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Sleep", sbycr.SBY == 0)) {
        sbycr.SBY = 0;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Standby", sbycr.SBY == 1)) {
        sbycr.SBY = 1;
    }

    ImGui::Separator();

    const bool master = m_sh2.IsMaster();
    if (!master) {
        bool slaveSH2Enabled = m_context.saturn.IsSlaveSH2Enabled();
        if (ImGui::Checkbox("Enabled", &slaveSH2Enabled)) {
            m_context.saturn.SetSlaveSH2Enabled(slaveSH2Enabled);
        }
    }

    if (!m_context.saturn.IsDebugTracingEnabled()) {
        ImGui::BeginDisabled();
    }
    bool suspended = m_sh2.IsCPUSuspended();
    if (ImGui::Checkbox("Suspended", &suspended)) {
        m_sh2.SetCPUSuspended(suspended);
    }
    widgets::ExplanationTooltip("Disables the CPU while in debug mode.");
    if (!m_context.saturn.IsDebugTracingEnabled()) {
        ImGui::EndDisabled();
    }

    bool asleep = probe.GetSleepState();
    if (ImGui::Checkbox("Asleep", &asleep)) {
        probe.SetSleepState(asleep);
    }
    widgets::ExplanationTooltip("Whether the CPU is in standby or sleep mode due to executing the SLEEP instruction.");
}

} // namespace app::ui
