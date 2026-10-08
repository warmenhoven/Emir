#include "sh2_exception_vectors_window.hpp"

#include <app/imgui_data.hpp>

namespace app::ui {

SH2ExceptionVectorsWindow::SH2ExceptionVectorsWindow(SharedContext &context, bool master)
    : SH2WindowBase(context, master)
    , m_excptVecView(m_sh2) {

    m_windowConfig.name = fmt::format("{}SH2 exception vectors", master ? 'M' : 'S');
    m_windowConfig.flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_AlwaysAutoResize;
}

void SH2ExceptionVectorsWindow::PrepareWindow() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    const float width = m_excptVecView.GetWidth();
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(width * imguiData->displayScale, FLT_MAX));
}

void SH2ExceptionVectorsWindow::DrawContents() {
    m_excptVecView.Display();
}

} // namespace app::ui
