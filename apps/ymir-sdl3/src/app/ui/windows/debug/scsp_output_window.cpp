#include "scsp_output_window.hpp"

#include <app/imgui_data.hpp>

namespace app::ui {

SCSPOutputWindow::SCSPOutputWindow(SharedContext &context)
    : SCSPWindowBase(context)
    , m_outputView(context) {

    m_windowConfig.name = "SCSP output";
    // m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void SCSPOutputWindow::PrepareWindow() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    ImGui::SetNextWindowSizeConstraints(ImVec2(200 * imguiData->displayScale, 50 * imguiData->displayScale),
                                        ImVec2(FLT_MAX, 200 * imguiData->displayScale));
}

void SCSPOutputWindow::DrawContents() {
    m_outputView.Display(ImGui::GetContentRegionAvail());
}

} // namespace app::ui
