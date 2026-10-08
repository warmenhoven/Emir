#pragma once

#include <app/debug/scsp_tracer.hpp>

namespace app::ui {

class SCSPKeyOnExecuteTraceView {
public:
    SCSPKeyOnExecuteTraceView(SCSPTracer &tracer);

    void Display();

private:
    SCSPTracer &m_tracer;
};

} // namespace app::ui
