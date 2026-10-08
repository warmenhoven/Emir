#include "sh2_breakpoints_window.hpp"

#include <app/imgui_data.hpp>

using namespace ymir;

namespace app::ui {

SH2BreakpointsWindow::SH2BreakpointsWindow(SharedContext &context, bool master, SH2DebuggerModel &model)
    : SH2WindowBase(context, master)
    , m_breakpointsView(context, model) {

    m_windowConfig.name = fmt::format("{}SH2 breakpoints", master ? 'M' : 'S');
    // m_windowConfig.flags = ImGuiWindowFlags_MenuBar;
}

void SH2BreakpointsWindow::PrepareWindow() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    ImGui::SetNextWindowSizeConstraints(ImVec2(250 * imguiData->displayScale, 250 * imguiData->displayScale),
                                        ImVec2(250 * imguiData->displayScale, FLT_MAX));
}

void SH2BreakpointsWindow::DrawContents() {
    m_breakpointsView.Display();
}

} // namespace app::ui
