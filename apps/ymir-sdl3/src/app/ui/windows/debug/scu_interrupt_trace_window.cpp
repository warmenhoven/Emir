#include "scu_interrupt_trace_window.hpp"

#include <app/imgui_data.hpp>

#include <imgui.h>

namespace app::ui {

SCUInterruptTraceWindow::SCUInterruptTraceWindow(SharedContext &context)
    : WindowBase(context)
    , m_intrTraceView(context.tracers.SCU) {

    m_windowConfig.name = "SCU interrupt trace";
}

void SCUInterruptTraceWindow::PrepareWindow() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    ImGui::SetNextWindowSizeConstraints(ImVec2(300 * imguiData->displayScale, 200 * imguiData->displayScale),
                                        ImVec2(450 * imguiData->displayScale, FLT_MAX));
}

void SCUInterruptTraceWindow::DrawContents() {
    ImGui::SeparatorText("Interrupt trace");
    m_intrTraceView.Display();
}

} // namespace app::ui
