#include "scu_dma_trace_window.hpp"

#include <app/imgui_data.hpp>

namespace app::ui {

SCUDMATraceWindow::SCUDMATraceWindow(SharedContext &context)
    : WindowBase(context)
    , m_dmaTraceView(context.tracers.SCU) {

    m_windowConfig.name = "SCU DMA trace";
}

void SCUDMATraceWindow::PrepareWindow() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    ImGui::SetNextWindowSizeConstraints(ImVec2(425 * imguiData->displayScale, 200 * imguiData->displayScale),
                                        ImVec2(525 * imguiData->displayScale, FLT_MAX));
}

void SCUDMATraceWindow::DrawContents() {
    m_dmaTraceView.Display();
}

} // namespace app::ui
