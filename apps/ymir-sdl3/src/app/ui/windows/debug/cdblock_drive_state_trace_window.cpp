#include "cdblock_drive_state_trace_window.hpp"

#include <app/imgui_data.hpp>

namespace app::ui {

CDDriveStateTraceWindow::CDDriveStateTraceWindow(SharedContext &context)
    : CDBlockWindowBase(context)
    , m_stateTraceView(context) {

    m_windowConfig.name = "CD drive state trace";
}

void CDDriveStateTraceWindow::PrepareWindow() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    ImGui::SetNextWindowSizeConstraints(ImVec2(720 * imguiData->displayScale, 250 * imguiData->displayScale),
                                        ImVec2(FLT_MAX, FLT_MAX));
}

void CDDriveStateTraceWindow::DrawContents() {
    m_stateTraceView.Display();
}

} // namespace app::ui
