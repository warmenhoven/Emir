#pragma once

#include <ymir/core/types.hpp>

namespace ymir::scu {
class SCU;
}

namespace app::ui {

class SCUDMAStateView {
public:
    SCUDMAStateView(ymir::scu::SCU &scu);

    void Display(uint8 channel);

private:
    ymir::scu::SCU &m_scu;
};

} // namespace app::ui
