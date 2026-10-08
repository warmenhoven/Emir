#include "debug_output_window.hpp"

#include <app/imgui_data.hpp>

#include <imgui.h>

namespace app::ui {

DebugOutputWindow::DebugOutputWindow(SharedContext &context)
    : WindowBase(context)
    , m_debugOutputView(context) {

    m_windowConfig.name = "Debug output";
}

void DebugOutputWindow::PrepareWindow() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    ImGui::SetNextWindowSizeConstraints(ImVec2(365 * imguiData->displayScale, 150 * imguiData->displayScale),
                                        ImVec2(FLT_MAX, FLT_MAX));
}

void DebugOutputWindow::DrawContents() {
    m_debugOutputView.Display();
}

} // namespace app::ui
