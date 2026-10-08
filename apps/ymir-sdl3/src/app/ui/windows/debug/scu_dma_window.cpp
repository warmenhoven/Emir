#include "scu_dma_window.hpp"

namespace app::ui {

SCUDMAWindow::SCUDMAWindow(SharedContext &context)
    : WindowBase(context)
    , m_dmaRegsView(context.saturn.GetSCU())
    , m_dmaStateView(context.saturn.GetSCU()) {

    m_windowConfig.name = "SCU DMA";
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void SCUDMAWindow::DrawContents() {
    ImGui::BeginGroup();

    if (ImGui::BeginTable("scu_dma", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableNextRow();
        for (uint32 i = 0; i < 3; i++) {
            if (ImGui::TableNextColumn()) {
                ImGui::SeparatorText(fmt::format("Channel {}", i).c_str());
                m_dmaRegsView.Display(i);
                ImGui::Separator();
                m_dmaStateView.Display(i);
            }
        }

        ImGui::EndTable();
    }

    ImGui::EndGroup();
}

} // namespace app::ui
