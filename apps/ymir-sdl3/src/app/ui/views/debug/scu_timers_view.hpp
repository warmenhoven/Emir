#pragma once

namespace ymir::scu {
class SCU;
}

namespace app::ui {

class SCUTimersView {
public:
    SCUTimersView(ymir::scu::SCU &scu);

    void Display();

private:
    ymir::scu::SCU &m_scu;
};

} // namespace app::ui
