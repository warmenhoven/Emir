#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "debug_types.hpp"

namespace ymir::debug {

struct DebugStoppedEvent {
    std::string instance_id;
    StopReason reason{StopReason::Entry};
    DebugTarget target{DebugTarget::Sh2Master};
    uint32_t pc{};
    uint32_t sequence{};
    std::optional<std::string> breakpoint_id;
};

struct TargetInfo {
    DebugTarget target{DebugTarget::Sh2Master};
    bool enabled{true};
};

struct InstanceReadyEvent {
    std::string protocol;
    std::string protocol_version;
    std::string transport;
    std::string instance_id;
    ExecutionState state{ExecutionState::Starting};
    std::vector<std::string> capabilities;
    std::vector<TargetInfo> targets;
};

using DebugEventPayload = std::variant<DebugStoppedEvent, InstanceReadyEvent>;

struct DebugEvent {
    DebugEventPayload payload;
};

} // namespace ymir::debug
