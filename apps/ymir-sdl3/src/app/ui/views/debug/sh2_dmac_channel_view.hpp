#pragma once

#include <ymir/hw/sh2/sh2_dmac.hpp>

namespace app::ui {

class SH2DMAControllerChannelView {
public:
    SH2DMAControllerChannelView(ymir::sh2::DMAChannel &channel, int index);

    void Display();

private:
    ymir::sh2::DMAChannel &m_channel;
    const int m_index;
};

} // namespace app::ui
