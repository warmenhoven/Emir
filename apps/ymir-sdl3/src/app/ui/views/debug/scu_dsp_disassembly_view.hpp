#pragma once

#include <imgui.h>

namespace ymir::scu {
class SCU;
}

namespace app::ui {

class SCUDSPDisassemblyView {
public:
    SCUDSPDisassemblyView(ymir::scu::SCU &scu);

    void Display();

private:
    ymir::scu::SCU &m_scu;

    struct Colors {
#define C(r, g, b) (r / 255.0f), (g / 255.0f), (b / 255.0f), 1.0f
#define CA(r, g, b, a) (r / 255.0f), (g / 255.0f), (b / 255.0f), a
        struct Disassembly {
            ImVec4 address{C(217, 216, 237)}; // 06 ........
            ImVec4 bytes{C(237, 236, 216)};   // .. 01C86F32

            ImVec4 mnemonic{C(173, 216, 247)};        // AND MVI DMA ...
            ImVec4 nopMnemonic{CA(66, 81, 92, 0.7f)}; // NOP
            ImVec4 illegalMnemonic{C(247, 191, 173)}; // (illegal)

            ImVec4 condPass{C(143, 240, 132)}; // Z S ZS C T0 ... (<cond>) (pass)
            ImVec4 condFail{C(222, 140, 135)}; // Z S ZS C T0 ... (<cond>) (fail)

            ImVec4 immediate{C(221, 247, 173)}; // #0x1234
            ImVec4 regRead{C(173, 247, 206)};   // M0-3 MC0-3 X Y A P RX RA0 LOP TOP ... (read)
            ImVec4 regWrite{C(215, 173, 247)};  // M0-3 MC0-3 X Y A P RX RA0 LOP TOP ... (write)

            ImVec4 separator{C(186, 191, 194)}; // , (operators)
        } disasm;
    } m_colors;

    struct Style {
        struct Disassembly {
            bool altLineColors = false;
            bool drawNOPs = true;

            float instrSeparation = 25.0f;
        } disasm;
    } m_style;
};

} // namespace app::ui
