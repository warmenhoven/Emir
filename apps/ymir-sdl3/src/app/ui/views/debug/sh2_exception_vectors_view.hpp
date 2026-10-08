#pragma once

#include <ymir/core/types.hpp>

namespace ymir::sh2 {
class SH2;
}

namespace app::ui {

class SH2ExceptionVectorsView {
public:
    SH2ExceptionVectorsView(ymir::sh2::SH2 &sh2);

    void Display();

    float GetWidth() const;

private:
    ymir::sh2::SH2 &m_sh2;

    bool m_useVBR = true;
    uint32 m_customAddress = 0x00000000;

    // Split into 2**m_columnShift columns
    uint32 m_columnShift = 2;
};

} // namespace app::ui
