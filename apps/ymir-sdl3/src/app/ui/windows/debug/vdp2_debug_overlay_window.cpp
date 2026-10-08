#include "vdp2_debug_overlay_window.hpp"

namespace app::ui {

VDP2DebugOverlayWindow::VDP2DebugOverlayWindow(SharedContext &context)
    : VDPWindowBase(context)
    , m_debugOverlayView(m_vdp) {

    m_windowConfig.name = "VDP2 debug overlay";
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void VDP2DebugOverlayWindow::PrepareWindow() {
    /*const YmirImGuiData *imguiData = GetYmirImGuiData();
    ImGui::SetNextWindowSizeConstraints(ImVec2(860 * imguiData->displayScale, 250 * imguiData->displayScale),
                                        ImVec2(860 * imguiData->displayScale, FLT_MAX));*/
}

void VDP2DebugOverlayWindow::DrawContents() {
    m_debugOverlayView.Display();
}

} // namespace app::ui
