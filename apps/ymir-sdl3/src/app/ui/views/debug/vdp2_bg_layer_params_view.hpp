#pragma once

namespace ymir::vdp {
class VDP;
}

namespace app::ui {

class VDP2BGLayerParamsView {
public:
    VDP2BGLayerParamsView(ymir::vdp::VDP &vdp);

    void Display();

private:
    ymir::vdp::VDP &m_vdp;
};

} // namespace app::ui
