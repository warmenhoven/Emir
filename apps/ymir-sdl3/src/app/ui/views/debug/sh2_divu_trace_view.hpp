#pragma once

#include <app/debug/sh2_tracer.hpp>

namespace app::ui {

class SH2DivisionUnitTraceView {
public:
    SH2DivisionUnitTraceView(SH2Tracer &tracer);

    void Display();

private:
    SH2Tracer &m_tracer;

    bool m_showHex = false;
};

} // namespace app::ui
