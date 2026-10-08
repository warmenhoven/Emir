#include "sh2_cache_window.hpp"

#include <app/imgui_data.hpp>

namespace app::ui {

SH2CacheWindow::SH2CacheWindow(SharedContext &context, bool master)
    : SH2WindowBase(context, master)
    , m_cacheRegView(m_sh2)
    , m_cacheEntriesView(m_sh2) {

    m_windowConfig.name = fmt::format("{}SH2 cache", master ? 'M' : 'S');
}

void SH2CacheWindow::PrepareWindow() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    ImGui::SetNextWindowSizeConstraints(ImVec2(652 * imguiData->displayScale, 246 * imguiData->displayScale),
                                        ImVec2(652 * imguiData->displayScale, FLT_MAX));
}

void SH2CacheWindow::DrawContents() {
    ImGui::SeparatorText("Cache Control Register");
    m_cacheRegView.Display();

    ImGui::SeparatorText("Entries");
    m_cacheEntriesView.Display();
}

} // namespace app::ui
