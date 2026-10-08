#include "sh2_watchpoints_window.hpp"

#include <app/imgui_data.hpp>

using namespace ymir;

namespace app::ui {

SH2WatchpointsWindow::SH2WatchpointsWindow(SharedContext &context, bool master, SH2WatchpointsManager &wtptManager)
    : SH2WindowBase(context, master)
    , m_watchpointsView(context, wtptManager) {

    m_windowConfig.name = fmt::format("{}SH2 watchpoints", master ? 'M' : 'S');
    // m_windowConfig.flags = ImGuiWindowFlags_MenuBar;
}

void SH2WatchpointsWindow::PrepareWindow() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    ImGui::SetNextWindowSizeConstraints(ImVec2(285 * imguiData->displayScale, 300 * imguiData->displayScale),
                                        ImVec2(285 * imguiData->displayScale, FLT_MAX));
}

void SH2WatchpointsWindow::DrawContents() {
    m_watchpointsView.Display();
}

} // namespace app::ui
