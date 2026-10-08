#include "sh2_cache_register_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <app/imgui_data.hpp>

using namespace ymir;

namespace app::ui {

SH2CacheRegisterView::SH2CacheRegisterView(ymir::sh2::SH2 &sh2)
    : m_sh2(sh2) {}

void SH2CacheRegisterView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &probe = m_sh2.GetProbe();
    auto &cache = probe.GetCache();

    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();

    uint8 CCR = cache.ReadCCR();

    ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    if (ImGui::InputScalar("##ccr", ImGuiDataType_U8, &CCR, nullptr, nullptr, "%02X",
                           ImGuiInputTextFlags_CharsHexadecimal)) {
        cache.WriteCCR<true>(CCR);
    }
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("CCR");

    ImGui::SameLine();
    ImGui::Checkbox("CE", &cache.CCR.CE);
    ImGui::SetItemTooltip("Cache Enable");

    ImGui::SameLine();
    ImGui::Checkbox("ID", &cache.CCR.ID);
    ImGui::SetItemTooltip("Instruction Replacement Disable");

    ImGui::SameLine();
    ImGui::Checkbox("OD", &cache.CCR.OD);
    ImGui::SetItemTooltip("Data Replacement Disable");

    ImGui::SameLine();
    ImGui::Checkbox("TW", &cache.CCR.TW);
    ImGui::SetItemTooltip("Two-Way Mode");

    ImGui::SameLine();

    uint8 Wn = cache.CCR.Wn;
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 1);
    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    if (ImGui::InputScalar("##way", ImGuiDataType_U8, &Wn, nullptr, nullptr, "%X",
                           ImGuiInputTextFlags_CharsHexadecimal)) {
        cache.CCR.Wn = std::min<uint8>(Wn, 3);
    }
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("W1-0");
    ImGui::EndGroup();
    ImGui::SetItemTooltip("Way Select");

    ImGui::SameLine();
    if (ImGui::Button("Purge")) {
        cache.Purge();
    }
}

} // namespace app::ui
