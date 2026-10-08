#include "sh2_timers_window.hpp"

using namespace ymir;

namespace app::ui {

SH2TimersWindow::SH2TimersWindow(SharedContext &context, bool master)
    : SH2WindowBase(context, master)
    , m_frtView(m_sh2)
    , m_wdtView(m_sh2) {

    m_windowConfig.name = fmt::format("{}SH2 timers", master ? 'M' : 'S');
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void SH2TimersWindow::DrawContents() {
    ImGui::SeparatorText("Free-running timer");
    m_frtView.Display();

    ImGui::SeparatorText("Watchdog timer");
    m_wdtView.Display();
}

} // namespace app::ui
