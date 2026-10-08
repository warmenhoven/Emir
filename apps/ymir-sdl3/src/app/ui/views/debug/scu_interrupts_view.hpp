#pragma once

namespace ymir::scu {
class SCU;
}

namespace app::ui {

class SCUInterruptsView {
public:
    SCUInterruptsView(ymir::scu::SCU &scu);

    void Display();

private:
    ymir::scu::SCU &m_scu;

    void DisplayInternalInterrupts();
    void DisplayExternalInterrupts();
};

} // namespace app::ui
