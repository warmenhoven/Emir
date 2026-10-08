#include "vdp2_cram_window.hpp"

#include <app/imgui_data.hpp>

namespace app::ui {

VDP2CRAMWindow::VDP2CRAMWindow(SharedContext &context)
    : VDPWindowBase(context)
    , m_cramView(context, m_vdp) {

    m_windowConfig.name = "VDP2 Color RAM palette";
    // m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void VDP2CRAMWindow::PrepareWindow() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    ImGui::SetNextWindowSizeConstraints(ImVec2(860 * imguiData->displayScale, 250 * imguiData->displayScale),
                                        ImVec2(860 * imguiData->displayScale, FLT_MAX));
}

void VDP2CRAMWindow::DrawContents() {
    m_cramView.Display();
}

} // namespace app::ui
