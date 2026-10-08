#pragma once

namespace ymir::sh2 {
class SH2;
}

namespace app::ui {

class SH2CacheRegisterView {
public:
    SH2CacheRegisterView(ymir::sh2::SH2 &sh2);

    void Display();

private:
    ymir::sh2::SH2 &m_sh2;
};

} // namespace app::ui
