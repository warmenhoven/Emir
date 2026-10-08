#include "scu_registers_view.hpp"

#include <ymir/hw/scu/scu.hpp>

#include <imgui.h>

namespace app::ui {

SCURegistersView::SCURegistersView(ymir::scu::SCU &scu)
    : m_scu(scu) {}

void SCURegistersView::Display() {
    auto &probe = m_scu.GetProbe();

    ImGui::BeginGroup();

    bool wramSizeSelect = probe.GetWRAMSizeSelect();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("WRAM size:");
    ImGui::SameLine();
    if (ImGui::RadioButton("512 KiB (2x2 Mbit)", !wramSizeSelect)) {
        probe.SetWRAMSizeSelect(false);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("1 MiB (2x4 Mbit)", wramSizeSelect)) {
        probe.SetWRAMSizeSelect(true);
    }

    ImGui::EndGroup();
}

} // namespace app::ui
