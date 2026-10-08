#include "sh2_divu_registers_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <app/events/emu_debug_event_factory.hpp>

#include <app/imgui_data.hpp>

using namespace ymir;

namespace app::ui {

SH2DivisionUnitRegistersView::SH2DivisionUnitRegistersView(SharedContext &context, ymir::sh2::SH2 &sh2)
    : m_context(context)
    , m_sh2(sh2) {}

void SH2DivisionUnitRegistersView::Display() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &probe = m_sh2.GetProbe();
    auto &divu = probe.DIVU();
    auto &intc = probe.INTC();

    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();

    if (ImGui::BeginTable("divu_regs", 4, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextRow();

        auto drawReg = [&](uint32 &value, const char *name, const char *tooltip) {
            ImGui::BeginGroup();
            ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 8);
            ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
            const bool changed = ImGui::InputScalar(fmt::format("##{}", name).c_str(), ImGuiDataType_U32, &value,
                                                    nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(name);
            ImGui::EndGroup();
            ImGui::SetItemTooltip("%s", tooltip);
            return changed;
        };

        if (ImGui::TableNextColumn()) {
            drawReg(divu.DVDNTH, "DVDNTH", "64-bit dividend high");
            drawReg(divu.DVDNTUH, "DVDNTUH", "64-bit dividend high (shadow copy)");
        }

        if (ImGui::TableNextColumn()) {
            drawReg(divu.DVDNTL, "DVDNTL", "64-bit dividend low");
            drawReg(divu.DVDNTUL, "DVDNTUL", "64-bit dividend low (shadow copy)");
        }

        if (ImGui::TableNextColumn()) {
            drawReg(divu.DVDNT, "DVDNT", "32-bit dividend");
            drawReg(divu.DVSR, "DVSR", "Divisor");
        }

        if (ImGui::TableNextColumn()) {
            uint32 dvcr = divu.DVCR.Read();
            if (drawReg(dvcr, "DVCR", "Division control register")) {
                divu.DVCR.Write(dvcr);
            }
            ImGui::Checkbox("OVF", &divu.DVCR.OVF);
            ImGui::SetItemTooltip("Overflow flag");
            ImGui::SameLine();
            ImGui::Checkbox("OVFIE", &divu.DVCR.OVFIE);
            ImGui::SetItemTooltip("Overflow interrupt enable");
        }

        ImGui::EndTable();
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Interrupt:");

    ImGui::SameLine();

    ImGui::BeginGroup();
    uint8 vector = intc.GetVector(sh2::InterruptSource::DIVU_OVFI);
    ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    if (ImGui::InputScalar("##vcrdiv", ImGuiDataType_U8, &vector, nullptr, nullptr, "%02X",
                           ImGuiInputTextFlags_CharsHexadecimal)) {
        intc.SetVector(sh2::InterruptSource::DIVU_OVFI, vector);
    }
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextUnformatted("VCRDIV");
    ImGui::EndGroup();
    ImGui::SetItemTooltip("Division unit interrupt vector");

    ImGui::SameLine();

    uint8 level = intc.GetLevel(sh2::InterruptSource::DIVU_OVFI);
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 1);
    ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
    if (ImGui::InputScalar("##ipra_divuipn", ImGuiDataType_U8, &level, nullptr, nullptr, "%X",
                           ImGuiInputTextFlags_CharsHexadecimal)) {
        intc.SetLevel(sh2::InterruptSource::DIVU_OVFI, std::min<uint8>(level, 0xF));
    }
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextUnformatted("IPRA.DIVUIP3-0");
    ImGui::EndGroup();
    ImGui::SetItemTooltip("Division unit interrupt level");

    const bool master = m_sh2.IsMaster();

    ImGui::SameLine(0, 15.0f);
    ImGui::TextUnformatted("Calculate:");
    ImGui::SameLine();
    if (ImGui::Button("32x32")) {
        m_context.EnqueueEvent(events::emu::debug::ExecuteSH2Division(master, false));
    }
    ImGui::SameLine();
    if (ImGui::Button("64x32")) {
        m_context.EnqueueEvent(events::emu::debug::ExecuteSH2Division(master, true));
    }
}

} // namespace app::ui
