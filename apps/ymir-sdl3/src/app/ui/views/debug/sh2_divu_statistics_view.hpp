#pragma once

#include <app/debug/sh2_tracer.hpp>

namespace app::ui {

class SH2DivisionUnitStatisticsView {
public:
    SH2DivisionUnitStatisticsView(SH2Tracer &tracer);

    void Display();

private:
    SH2Tracer &m_tracer;
};

} // namespace app::ui
