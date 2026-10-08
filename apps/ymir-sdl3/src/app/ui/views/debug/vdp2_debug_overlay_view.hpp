#pragma once

namespace ymir::vdp {
class VDP;
}

namespace app::ui {

class VDP2DebugOverlayView {
public:
    VDP2DebugOverlayView(ymir::vdp::VDP &vdp);

    void Display();

private:
    ymir::vdp::VDP &m_vdp;
};

} // namespace app::ui
