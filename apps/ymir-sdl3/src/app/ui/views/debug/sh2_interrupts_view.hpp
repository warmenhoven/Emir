#pragma once

#include <ymir/core/types.hpp>

namespace ymir::sh2 {
class SH2;
}

namespace app::ui {

class SH2InterruptsView {
public:
    SH2InterruptsView(ymir::sh2::SH2 &sh2);

    void Display();

private:
    ymir::sh2::SH2 &m_sh2;

    uint8 m_extIntrVector = 0x0;
    uint8 m_extIntrLevel = 0x0;
};

} // namespace app::ui
