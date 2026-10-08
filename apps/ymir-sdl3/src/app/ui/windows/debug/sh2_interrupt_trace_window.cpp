#include "sh2_interrupt_trace_window.hpp"

#include <app/imgui_data.hpp>

using namespace ymir;

namespace app::ui {

SH2InterruptTraceWindow::SH2InterruptTraceWindow(SharedContext &context, bool master)
    : SH2WindowBase(context, master)
    , m_intrTraceView(m_tracer) {

    m_windowConfig.name = fmt::format("{}SH2 interrupt trace", master ? 'M' : 'S');
}

void SH2InterruptTraceWindow::PrepareWindow() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    ImGui::SetNextWindowSizeConstraints(ImVec2(250 * imguiData->displayScale, 200 * imguiData->displayScale),
                                        ImVec2(600 * imguiData->displayScale, FLT_MAX));
}

void SH2InterruptTraceWindow::DrawContents() {
    m_intrTraceView.Display();
}

} // namespace app::ui
