#include "sh2_dmac_window.hpp"

#include <ymir/hw/sh2/sh2.hpp>

using namespace ymir;

namespace app::ui {

SH2DMAControllerWindow::SH2DMAControllerWindow(SharedContext &context, bool master)
    : SH2WindowBase(context, master)
    , m_dmacRegsView(m_sh2)
    , m_dmacChannel0View(m_sh2.GetProbe().DMAC0(), 0)
    , m_dmacChannel1View(m_sh2.GetProbe().DMAC1(), 1) {

    m_windowConfig.name = fmt::format("{}SH2 DMA controller", master ? 'M' : 'S');
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void SH2DMAControllerWindow::DrawContents() {
    ImGui::SeparatorText("Registers");
    m_dmacRegsView.Display();

    ImGui::SeparatorText("Channel 0");
    m_dmacChannel0View.Display();

    ImGui::SeparatorText("Channel 1");
    m_dmacChannel1View.Display();
}

} // namespace app::ui
