#pragma once

#include <app/debug/scu_tracer.hpp>

namespace app::ui {

class SCUDMATraceView {
public:
    SCUDMATraceView(SCUTracer &tracer);

    void Display();

private:
    SCUTracer &m_tracer;
};

} // namespace app::ui
