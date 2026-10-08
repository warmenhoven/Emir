#include "cdblock_ygr_cmd_trace_window.hpp"

#include <app/imgui_data.hpp>

namespace app::ui {

YGRCommandTraceWindow::YGRCommandTraceWindow(SharedContext &context)
    : CDBlockWindowBase(context)
    , m_cmdTraceView(context) {

    m_windowConfig.name = "YGR command trace";
}

void YGRCommandTraceWindow::PrepareWindow() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    ImGui::SetNextWindowSizeConstraints(ImVec2(450 * imguiData->displayScale, 180 * imguiData->displayScale),
                                        ImVec2(FLT_MAX, FLT_MAX));
}

void YGRCommandTraceWindow::DrawContents() {
    m_cmdTraceView.Display();
}

} // namespace app::ui
