#pragma once

#include <app/debug/scu_tracer.hpp>

namespace app::ui {

class SCUInterruptTraceView {
public:
    SCUInterruptTraceView(SCUTracer &tracer);

    void Display();

private:
    SCUTracer &m_tracer;
};

} // namespace app::ui
