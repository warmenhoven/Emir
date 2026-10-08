#include "cdblock_cmd_trace_window.hpp"

#include <app/imgui_data.hpp>

namespace app::ui {

CDBlockCommandTraceWindow::CDBlockCommandTraceWindow(SharedContext &context)
    : CDBlockWindowBase(context)
    , m_cmdTraceView(context) {

    m_windowConfig.name = "CD Block command trace";
}

void CDBlockCommandTraceWindow::PrepareWindow() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    ImGui::SetNextWindowSizeConstraints(ImVec2(450 * imguiData->displayScale, 180 * imguiData->displayScale),
                                        ImVec2(FLT_MAX, FLT_MAX));
}

void CDBlockCommandTraceWindow::DrawContents() {
    m_cmdTraceView.Display();
}

} // namespace app::ui
