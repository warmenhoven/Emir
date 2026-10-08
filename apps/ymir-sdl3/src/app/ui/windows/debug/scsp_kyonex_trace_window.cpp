#include "scsp_kyonex_trace_window.hpp"

#include <app/imgui_data.hpp>

namespace app::ui {

SCSPKeyOnExecuteTraceWindow::SCSPKeyOnExecuteTraceWindow(SharedContext &context)
    : SCSPWindowBase(context)
    , m_kyonexTraceView(context.tracers.SCSP) {

    m_windowConfig.name = "SCSP KYONEX trace";
    // m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void SCSPKeyOnExecuteTraceWindow::PrepareWindow() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    ImGui::SetNextWindowSizeConstraints(ImVec2(300 * imguiData->displayScale, 100 * imguiData->displayScale),
                                        ImVec2(450 * imguiData->displayScale, FLT_MAX));
}

void SCSPKeyOnExecuteTraceWindow::DrawContents() {
    m_kyonexTraceView.Display();
}

} // namespace app::ui
