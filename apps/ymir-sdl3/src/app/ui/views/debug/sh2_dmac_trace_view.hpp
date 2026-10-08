#pragma once

#include <app/debug/sh2_tracer.hpp>

namespace app::ui {

class SH2DMAControllerChannelTraceView {
public:
    SH2DMAControllerChannelTraceView(int index, SH2Tracer &tracer);

    void Display();

private:
    const int m_index;
    SH2Tracer &m_tracer;

    void DisplayStatistics();
    void DisplayTrace();
};

} // namespace app::ui
