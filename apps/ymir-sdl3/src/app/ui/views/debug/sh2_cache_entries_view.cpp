#include "sh2_cache_entries_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <app/imgui_data.hpp>

using namespace ymir;

namespace app::ui {

SH2CacheEntriesView::SH2CacheEntriesView(sh2::SH2 &sh2)
    : m_sh2(sh2) {}

void SH2CacheEntriesView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &probe = m_sh2.GetProbe();
    auto &cache = probe.GetCache();

    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();

    if (ImGui::BeginTable("lru", 6, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("LRU bits\n   -> Code/Data way");
        ImGui::TableSetupColumn("Way 0\nValid  Tag address");
        ImGui::TableSetupColumn("Way 1\nValid  Tag address");
        ImGui::TableSetupColumn("Way 2\nValid  Tag address");
        ImGui::TableSetupColumn("Way 3\nValid  Tag address");
        ImGui::TableHeadersRow();

        for (uint32 i = 0; i < 64; i++) {
            ImGui::TableNextRow();

            uint8 lru = cache.GetLRU(i);

            if (ImGui::TableNextColumn()) {
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%u", i);
            }
            if (ImGui::TableNextColumn()) {
                const uint8 codeWay = cache.GetWayFromLRU<true>(lru);
                const uint8 dataWay = cache.GetWayFromLRU<false>(lru);
                const char codeWayCh = sh2::IsValidCacheWay(codeWay) ? '0' + codeWay : '-';
                const char dataWayCh = sh2::IsValidCacheWay(dataWay) ? '0' + dataWay : '-';

                ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
                ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                if (ImGui::InputScalar(fmt::format("##lru_{}", i).c_str(), ImGuiDataType_U8, &lru, nullptr, nullptr,
                                       "%02X", ImGuiInputTextFlags_CharsHexadecimal)) {
                    lru = std::min<uint8>(lru, 0b111111);
                    cache.SetLRU(i, lru);
                }
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s -> %c/%c", fmt::format("{:06b}", lru).c_str(), codeWayCh, dataWayCh);
                ImGui::PopFont();
            }

            for (uint32 way = 0; way < sh2::kCacheWays; way++) {
                if (ImGui::TableNextColumn()) {
                    auto &entry = cache.GetEntryByIndex(i);
                    bool valid = entry.tag[way].valid;
                    if (ImGui::Checkbox(fmt::format("##entry_{}_way_{}_valid", i, way).c_str(), &valid)) {
                        entry.tag[way].valid = valid;
                    }

                    ImGui::SameLine();

                    uint32 tagAddress = entry.tag[way].tagAddress << 10u;
                    ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 8);
                    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
                    if (ImGui::InputScalar(fmt::format("##entry_{}_way_{}_tag_addr", i, way).c_str(), ImGuiDataType_U32,
                                           &tagAddress, nullptr, nullptr, "%08X",
                                           ImGuiInputTextFlags_CharsHexadecimal)) {
                        entry.tag[way].tagAddress = tagAddress >> 10u;
                    }
                    ImGui::PopFont();
                }
            }
        }
        ImGui::EndTable();
    }
}

} // namespace app::ui
