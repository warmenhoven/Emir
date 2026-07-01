#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace ymir::debug {

enum class DebugTarget {
    Sh2Master,
    Sh2Slave,
};
constexpr std::string_view ToString(DebugTarget t) {
    switch (t) {
    case DebugTarget::Sh2Master: return "sh2.master";
    case DebugTarget::Sh2Slave: return "sh2.slave";
    }
    return "unknown";
}

enum class ExecutionState {
    Starting,
    Paused,
    Running,
    Stopped,
    Crashed,
};
constexpr std::string_view ToString(ExecutionState s) {
    switch (s) {
    case ExecutionState::Starting: return "starting";
    case ExecutionState::Paused: return "paused";
    case ExecutionState::Running: return "running";
    case ExecutionState::Stopped: return "stopped";
    case ExecutionState::Crashed: return "crashed";
    }
    return "unknown";
}

enum class StopReason {
    Entry,
    Breakpoint,
    Watchpoint,
    Step,
    Pause,
    Exception,
    Shutdown,
    Exited,
    Error,
};
constexpr std::string_view ToString(StopReason r) {
    switch (r) {
    case StopReason::Entry: return "entry";
    case StopReason::Breakpoint: return "breakpoint";
    case StopReason::Watchpoint: return "watchpoint";
    case StopReason::Step: return "step";
    case StopReason::Pause: return "pause";
    case StopReason::Exception: return "exception";
    case StopReason::Shutdown: return "shutdown";
    case StopReason::Exited: return "exited";
    case StopReason::Error: return "error";
    }
    return "unknown";
}

enum class ErrorCode {
    InvalidRequest,
    InvalidMethod,
    InvalidParams,
    InvalidState,
    DebugTracingDisabled,
    TargetDisabled,
    InvalidTarget,
    InvalidRegister,
    InvalidAddress,
    BreakpointExists,
    BreakpointNotFound,
    InvalidMemorySpace,
    MemoryOutOfRange,
    Unsupported,
    InternalError,
};
constexpr std::string_view ToString(ErrorCode e) {
    switch (e) {
    case ErrorCode::InvalidRequest: return "invalid_request";
    case ErrorCode::InvalidMethod: return "invalid_method";
    case ErrorCode::InvalidParams: return "invalid_params";
    case ErrorCode::InvalidState: return "invalid_state";
    case ErrorCode::DebugTracingDisabled: return "debug_tracing_disabled";
    case ErrorCode::TargetDisabled: return "target_disabled";
    case ErrorCode::InvalidTarget: return "invalid_target";
    case ErrorCode::InvalidRegister: return "invalid_register";
    case ErrorCode::InvalidAddress: return "invalid_address";
    case ErrorCode::BreakpointExists: return "breakpoint_exists";
    case ErrorCode::BreakpointNotFound: return "breakpoint_not_found";
    case ErrorCode::InvalidMemorySpace: return "invalid_memory_space";
    case ErrorCode::MemoryOutOfRange: return "memory_out_of_range";
    case ErrorCode::Unsupported: return "unsupported";
    case ErrorCode::InternalError: return "internal_error";
    }
    return "unknown";
}

struct ErrorInfo {
    ErrorCode code{ErrorCode::InternalError};
    std::string message;
};

using DebugRequestId = std::variant<std::monostate, int64_t, std::string>;

} // namespace ymir::debug
