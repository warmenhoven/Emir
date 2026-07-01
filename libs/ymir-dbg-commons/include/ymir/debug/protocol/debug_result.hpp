#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "debug_types.hpp"

namespace ymir::debug {

struct ApplicationInfo {
    std::string name;
    std::string version;
    std::string git_sha;
};

struct DebugVersionResult {
    std::string protocol;
    std::string protocol_version;
    std::string transport;
    ApplicationInfo application;
    std::vector<std::string> capabilities;
};

struct InstanceStatusResult {
    std::string protocol;
    std::string instance_id;
    ExecutionState state{ExecutionState::Starting};
    bool slave_enabled{false};
    std::vector<std::string> capabilities;
};

struct RegsReadResult {
    DebugTarget target{DebugTarget::Sh2Master};
    uint32_t r[16]{};
    uint32_t pc{};
    uint32_t pr{};
    uint32_t sr{};
    bool sr_t{};
    bool sr_s{};
    uint8_t sr_ilevel{};
    bool sr_q{};
    bool sr_m{};
    uint32_t gbr{};
    uint32_t vbr{};
    uint32_t mach{};
    uint32_t macl{};
    bool is_delay_slot{};
};

struct MemPeekResult {
    DebugTarget target{DebugTarget::Sh2Master};
    uint32_t address{};
    std::vector<uint8_t> data;
};

struct DisasmLine {
    uint32_t address{};
    uint16_t opcode{};
    std::string text;
    std::optional<uint32_t> branch_target;
};

struct DisasmAtResult {
    DebugTarget target{DebugTarget::Sh2Master};
    std::vector<DisasmLine> instructions;
};

struct BreakpointInfo {
    std::string breakpoint_id;
    DebugTarget target{DebugTarget::Sh2Master};
    uint32_t address{};
    std::optional<std::string> label;
    bool enabled{};
};

struct BreakpointSetResult {
    std::string breakpoint_id;
    DebugTarget target{DebugTarget::Sh2Master};
    uint32_t address{};
};

struct BreakpointListResult {
    std::vector<BreakpointInfo> breakpoints;
    uint32_t total_count{};
};

struct ExecStepIResult {
    StopReason reason{StopReason::Step};
    DebugTarget target{DebugTarget::Sh2Master};
    uint32_t pc_before{};
    uint32_t pc_after{};
    uint32_t cycles_advanced{};
    bool counterpart_advanced{};
    std::optional<std::string> breakpoint_id;
};

using DebugResultPayload =
    std::variant<std::monostate, DebugVersionResult, InstanceStatusResult, RegsReadResult, MemPeekResult,
                 DisasmAtResult, BreakpointSetResult, BreakpointListResult, ExecStepIResult>;

// Variant enforces success XOR error at the type level; a result cannot carry both.
using DebugResult = std::variant<DebugResultPayload, ErrorInfo>;

} // namespace ymir::debug
