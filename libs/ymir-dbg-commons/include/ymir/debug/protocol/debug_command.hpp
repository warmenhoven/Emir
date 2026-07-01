#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "debug_types.hpp"

namespace ymir::debug {

enum class CommandMethod {
    DebugVersion,
    InstanceStatus,
    InstanceShutdown,
    ExecContinue,
    ExecPause,
    ExecStepI,
    ExecReset,
    RegsRead,
    MemPeek,
    DisasmAt,
    BreakpointSet,
    BreakpointList,
    BreakpointClear,
    BreakpointEnable,
    BreakpointDisable,
};
constexpr std::string_view ToString(CommandMethod m) {
    switch (m) {
    case CommandMethod::DebugVersion: return "debug.version";
    case CommandMethod::InstanceStatus: return "instance.status";
    case CommandMethod::InstanceShutdown: return "instance.shutdown";
    case CommandMethod::ExecContinue: return "exec.continue";
    case CommandMethod::ExecPause: return "exec.pause";
    case CommandMethod::ExecStepI: return "exec.stepi";
    case CommandMethod::ExecReset: return "exec.reset";
    case CommandMethod::RegsRead: return "regs.read";
    case CommandMethod::MemPeek: return "mem.peek";
    case CommandMethod::DisasmAt: return "disasm.at";
    case CommandMethod::BreakpointSet: return "breakpoint.set";
    case CommandMethod::BreakpointList: return "breakpoint.list";
    case CommandMethod::BreakpointClear: return "breakpoint.clear";
    case CommandMethod::BreakpointEnable: return "breakpoint.enable";
    case CommandMethod::BreakpointDisable: return "breakpoint.disable";
    }
    return "unknown";
}

// -- Command parameter structs ------------------------------------------------

struct RegsReadParams {
    DebugTarget target{DebugTarget::Sh2Master};
};

struct MemPeekParams {
    DebugTarget target{DebugTarget::Sh2Master};
    uint32_t address{0};
    uint32_t count{0};
};

struct DisasmAtParams {
    DebugTarget target{DebugTarget::Sh2Master};
    uint32_t address{0};
    uint32_t count{0};
};

struct BreakpointSetParams {
    DebugTarget target{DebugTarget::Sh2Master};
    uint32_t address{0};
    std::optional<std::string> label;
};

struct BreakpointListParams {
    std::optional<uint32_t> offset;
    std::optional<uint32_t> limit;
};

struct BreakpointIdParams {
    std::string breakpoint_id;
};

struct ExecStepIParams {
    DebugTarget target{DebugTarget::Sh2Master};
};

using DebugCommandParams = std::variant<std::monostate, RegsReadParams, MemPeekParams, DisasmAtParams,
                                        BreakpointSetParams, BreakpointListParams, BreakpointIdParams, ExecStepIParams>;

struct DebugCommand {
    DebugRequestId request_id;
    CommandMethod method{CommandMethod::DebugVersion};
    DebugCommandParams params;
};

} // namespace ymir::debug
