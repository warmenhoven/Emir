#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>

#include <ymir/debug/protocol/debug_command.hpp>
#include <ymir/debug/protocol/debug_event.hpp>
#include <ymir/debug/protocol/debug_result.hpp>
#include <ymir/debug/protocol/debug_types.hpp>
#include <ymir/debug/protocol/protocol_version.hpp>

#include <protocol/json_rpc_adapter.hpp>
#include <protocol/line_framer.hpp>

#include <cstddef>
#include <cstring>
#include <new>
#include <variant>

TEST_CASE("Protocol version constants", "[protocol]") {
    CHECK(ymir::debug::kProtocolName == "ymir-debug");
    CHECK(ymir::debug::kProtocolVersion == "0.1.0");
    CHECK(ymir::debug::kStdioJsonRpcLinesTransport == "stdio-jsonrpc-lines");
}

TEST_CASE("DebugTarget string round-trip", "[protocol]") {
    CHECK(ToString(ymir::debug::DebugTarget::Sh2Master) == "sh2.master");
    CHECK(ToString(ymir::debug::DebugTarget::Sh2Slave) == "sh2.slave");
}

TEST_CASE("ExecutionState string round-trip", "[protocol]") {
    auto s = ymir::debug::ExecutionState::Starting;
    CHECK(ToString(s) == "starting");
    s = ymir::debug::ExecutionState::Paused;
    CHECK(ToString(s) == "paused");
    s = ymir::debug::ExecutionState::Running;
    CHECK(ToString(s) == "running");
}

TEST_CASE("StopReason string round-trip", "[protocol]") {
    CHECK(ToString(ymir::debug::StopReason::Breakpoint) == "breakpoint");
    CHECK(ToString(ymir::debug::StopReason::Step) == "step");
    CHECK(ToString(ymir::debug::StopReason::Pause) == "pause");
}

TEST_CASE("ErrorCode string round-trip", "[protocol]") {
    CHECK(ToString(ymir::debug::ErrorCode::InvalidState) == "invalid_state");
    CHECK(ToString(ymir::debug::ErrorCode::TargetDisabled) == "target_disabled");
    CHECK(ToString(ymir::debug::ErrorCode::InvalidAddress) == "invalid_address");
}

TEST_CASE("CommandMethod string round-trip", "[protocol]") {
    CHECK(ToString(ymir::debug::CommandMethod::RegsRead) == "regs.read");
    CHECK(ToString(ymir::debug::CommandMethod::MemPeek) == "mem.peek");
    CHECK(ToString(ymir::debug::CommandMethod::DisasmAt) == "disasm.at");
    CHECK(ToString(ymir::debug::CommandMethod::ExecContinue) == "exec.continue");
    CHECK(ToString(ymir::debug::CommandMethod::ExecPause) == "exec.pause");
    CHECK(ToString(ymir::debug::CommandMethod::ExecStepI) == "exec.stepi");
    CHECK(ToString(ymir::debug::CommandMethod::BreakpointSet) == "breakpoint.set");
}

TEST_CASE("RegsReadParams initializes", "[protocol]") {
    ymir::debug::RegsReadParams p;
    CHECK(p.target == ymir::debug::DebugTarget::Sh2Master);
}

TEST_CASE("MemPeekParams initializes", "[protocol]") {
    ymir::debug::MemPeekParams p;
    CHECK(p.count == 0);
    CHECK(p.address == 0);
}

TEST_CASE("BreakpointSetParams supports optional label", "[protocol]") {
    ymir::debug::BreakpointSetParams p;
    CHECK_FALSE(p.label.has_value());
    p.label = "boot";
    CHECK(p.label.value() == "boot");
}

TEST_CASE("ExecStepIResult shape", "[protocol]") {
    ymir::debug::ExecStepIResult r;
    r.reason = ymir::debug::StopReason::Step;
    r.target = ymir::debug::DebugTarget::Sh2Master;
    r.pc_before = 0x06004000;
    r.pc_after = 0x06004002;
    r.cycles_advanced = 1;
    r.counterpart_advanced = true;
    CHECK(r.reason == ymir::debug::StopReason::Step);
    CHECK(r.pc_before == 0x06004000);
    CHECK(r.counterpart_advanced);
}

TEST_CASE("DebugCommand carries typed params", "[protocol]") {
    ymir::debug::DebugCommand command;
    command.method = ymir::debug::CommandMethod::MemPeek;
    command.params = ymir::debug::MemPeekParams{ymir::debug::DebugTarget::Sh2Master, 0x06004000, 16};

    REQUIRE(std::holds_alternative<ymir::debug::MemPeekParams>(command.params));
    const auto &params = std::get<ymir::debug::MemPeekParams>(command.params);
    CHECK(params.address == 0x06004000);
    CHECK(params.count == 16);
}

TEST_CASE("DebugResult carries payload or error", "[protocol]") {
    ymir::debug::DebugResult result{
        ymir::debug::DebugResultPayload{
            ymir::debug::BreakpointSetResult{"bp-1", ymir::debug::DebugTarget::Sh2Master, 0x06004000},
        },
    };

    REQUIRE(std::holds_alternative<ymir::debug::DebugResultPayload>(result));
    const auto &payload = std::get<ymir::debug::DebugResultPayload>(result);
    REQUIRE(std::holds_alternative<ymir::debug::BreakpointSetResult>(payload));

    result = ymir::debug::ErrorInfo{ymir::debug::ErrorCode::InvalidState, "not paused"};
    REQUIRE(std::holds_alternative<ymir::debug::ErrorInfo>(result));
    CHECK(std::get<ymir::debug::ErrorInfo>(result).code == ymir::debug::ErrorCode::InvalidState);
}

TEST_CASE("DebugStoppedEvent initializes", "[protocol]") {
    ymir::debug::DebugStoppedEvent e;
    e.instance_id = "local-001";
    e.reason = ymir::debug::StopReason::Breakpoint;
    e.target = ymir::debug::DebugTarget::Sh2Master;
    e.pc = 0x06004000;
    e.sequence = 1842;
    CHECK(e.instance_id == "local-001");
    CHECK_FALSE(e.breakpoint_id.has_value());
}

TEST_CASE("DebugEvent carries typed event payload", "[protocol]") {
    ymir::debug::DebugEvent event{
        ymir::debug::DebugStoppedEvent{"local-001", ymir::debug::StopReason::Breakpoint,
                                       ymir::debug::DebugTarget::Sh2Master, 0x06004000, 1842, std::nullopt},
    };

    REQUIRE(std::holds_alternative<ymir::debug::DebugStoppedEvent>(event.payload));
    const auto &stopped = std::get<ymir::debug::DebugStoppedEvent>(event.payload);
    CHECK(stopped.pc == 0x06004000);
}

TEST_CASE("DebugVersionResult with nested ApplicationInfo", "[protocol]") {
    ymir::debug::DebugVersionResult version;
    version.protocol = "ymir-debug";
    version.protocol_version = "0.1.0";
    version.transport = "stdio-jsonrpc-lines";
    version.application.name = "ymir-headless";
    version.application.version = "0.3.2-dev";
    version.application.git_sha = "unknown";
    version.capabilities = {"sh2.master", "sh2.slave"};

    CHECK(version.application.name == "ymir-headless");
    CHECK(version.application.version == "0.3.2-dev");
    CHECK(version.capabilities.size() == 2);
}

TEST_CASE("InstanceStatusResult includes instance_id", "[protocol]") {
    ymir::debug::InstanceStatusResult status;
    status.instance_id = "local-001";
    status.state = ymir::debug::ExecutionState::Paused;
    status.slave_enabled = false;

    CHECK(status.instance_id == "local-001");
    CHECK(status.state == ymir::debug::ExecutionState::Paused);
    CHECK_FALSE(status.slave_enabled);
}

TEST_CASE("InstanceReadyEvent full shape", "[protocol]") {
    ymir::debug::InstanceReadyEvent ready;
    ready.protocol = "ymir-debug";
    ready.protocol_version = "0.1.0";
    ready.transport = "stdio-jsonrpc-lines";
    ready.instance_id = "local-001";
    ready.state = ymir::debug::ExecutionState::Paused;
    ready.capabilities = {"sh2.master", "sh2.slave", "regs.read"};
    ready.targets = {
        ymir::debug::TargetInfo{ymir::debug::DebugTarget::Sh2Master, true},
        ymir::debug::TargetInfo{ymir::debug::DebugTarget::Sh2Slave, false},
    };

    CHECK(ready.instance_id == "local-001");
    REQUIRE(ready.targets.size() == 2);
    CHECK(ready.targets[0].enabled);
    CHECK_FALSE(ready.targets[1].enabled);
}

TEST_CASE("DebugCommand carries request_id", "[protocol]") {
    ymir::debug::DebugCommand cmd;
    cmd.request_id = int64_t{42};
    cmd.method = ymir::debug::CommandMethod::DebugVersion;

    CHECK(std::get<int64_t>(cmd.request_id) == 42);

    cmd.request_id = std::string{"req-001"};
    CHECK(std::get<std::string>(cmd.request_id) == "req-001");

    cmd.request_id = std::monostate{};
    CHECK(std::holds_alternative<std::monostate>(cmd.request_id));
}

TEST_CASE("DebugRequestId variant round-trip", "[protocol]") {
    ymir::debug::DebugRequestId id;

    id = int64_t{0};
    CHECK(std::get<int64_t>(id) == 0);

    id = int64_t{-1};
    CHECK(std::get<int64_t>(id) == -1);

    id = std::string{};
    CHECK(std::get<std::string>(id).empty());

    id = std::string{"abc"};
    CHECK(std::get<std::string>(id) == "abc");
}

TEST_CASE("TargetInfo initializes", "[protocol]") {
    ymir::debug::TargetInfo info;
    CHECK(info.target == ymir::debug::DebugTarget::Sh2Master);
    CHECK(info.enabled);
}

TEST_CASE("ErrorInfo construction", "[protocol]") {
    ymir::debug::ErrorInfo err{ymir::debug::ErrorCode::InvalidState, "not paused"};
    CHECK(err.code == ymir::debug::ErrorCode::InvalidState);
    CHECK(err.message == "not paused");
}

TEST_CASE("LineFramer splits lines", "[protocol]") {
    std::vector<std::string> lines;
    ymir::debug::LineFramer framer([&](std::string_view line) { lines.emplace_back(line); },
                                   [](ymir::debug::LineFramerError) { FAIL("Unexpected error"); });

    SECTION("Single line") {
        std::string data = "hello\n";
        framer.Push(data.data(), data.length());
        REQUIRE(lines.size() == 1);
        CHECK(lines[0] == "hello");
    }

    SECTION("Multiple lines") {
        std::string data = "line1\nline2\n";
        framer.Push(data.data(), data.length());
        REQUIRE(lines.size() == 2);
        CHECK(lines[0] == "line1");
        CHECK(lines[1] == "line2");
    }

    SECTION("Partial lines") {
        std::string data1 = "part";
        std::string data2 = "ial\n";
        framer.Push(data1.data(), data1.length());
        CHECK(lines.empty());
        framer.Push(data2.data(), data2.length());
        REQUIRE(lines.size() == 1);
        CHECK(lines[0] == "partial");
    }

    SECTION("CRLF line terminator") {
        std::string data = "hello\r\nworld\r\n";
        framer.Push(data.data(), data.length());
        REQUIRE(lines.size() == 2);
        CHECK(lines[0] == "hello");
        CHECK(lines[1] == "world");
    }

    SECTION("Bare CR line terminator") {
        std::string data = "hello\rworld\r";
        framer.Push(data.data(), data.length());
        REQUIRE(lines.size() == 2);
        CHECK(lines[0] == "hello");
        CHECK(lines[1] == "world");
    }
}

TEST_CASE("LineFramer enforces max length", "[protocol]") {
    bool sawError{};
    std::vector<std::string> lines;
    ymir::debug::LineFramer framer([&](std::string_view line) { lines.emplace_back(line); },
                                   [&](ymir::debug::LineFramerError) { sawError = true; });

    std::string maxLine(ymir::debug::LineFramer::kMaxLineLength, 'a');
    maxLine += '\n';
    framer.Push(maxLine.data(), maxLine.length());
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].size() == ymir::debug::LineFramer::kMaxLineLength);

    lines.clear();
    std::string longLine(ymir::debug::LineFramer::kMaxLineLength + 1, 'a');
    longLine += "\nvalid\n";

    framer.Push(longLine.data(), longLine.length());
    CHECK(sawError);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "valid");
}

TEST_CASE("JsonRpcAdapter parses requests", "[protocol]") {
    nlohmann::json error;

    SECTION("Valid request") {
        std::string line = R"({"jsonrpc": "2.0", "method": "debug.version", "id": 1})";
        auto req = ymir::debug::JsonRpcAdapter::ParseRequest(line, error);
        REQUIRE(req.has_value());
        CHECK(req->method == "debug.version");
        CHECK(std::get<int64_t>(req->id) == 1);
        CHECK_FALSE(req->is_notification);
    }

    SECTION("Valid notification") {
        std::string line = R"({"jsonrpc": "2.0", "method": "instance.ready"})";
        auto req = ymir::debug::JsonRpcAdapter::ParseRequest(line, error);
        REQUIRE(req.has_value());
        CHECK(req->method == "instance.ready");
        CHECK(req->is_notification);
    }

    SECTION("Batch request reject") {
        std::string line = R"([{"jsonrpc": "2.0", "method": "test"}])";
        auto req = ymir::debug::JsonRpcAdapter::ParseRequest(line, error);
        CHECK_FALSE(req.has_value());
        CHECK(error["error"]["message"] == "Batch requests are not supported");
    }

    SECTION("Malformed JSON") {
        std::string line = R"({"jsonrpc": "2.0", "method": )";
        auto req = ymir::debug::JsonRpcAdapter::ParseRequest(line, error);
        CHECK_FALSE(req.has_value());
        CHECK(error["error"]["code"] == static_cast<int>(ymir::debug::JsonRpcError::ParseError));
    }

    SECTION("Non-string jsonrpc field") {
        std::string line = R"({"jsonrpc": 2, "method": "debug.version", "id": 1})";
        auto req = ymir::debug::JsonRpcAdapter::ParseRequest(line, error);
        CHECK_FALSE(req.has_value());
        CHECK(error["error"]["code"] == static_cast<int>(ymir::debug::JsonRpcError::InvalidRequest));
    }

    SECTION("Valid id echoed in missing-method error") {
        std::string line = R"({"jsonrpc": "2.0", "id": 99})";
        auto req = ymir::debug::JsonRpcAdapter::ParseRequest(line, error);
        CHECK_FALSE(req.has_value());
        CHECK(error["error"]["code"] == static_cast<int>(ymir::debug::JsonRpcError::InvalidRequest));
        CHECK(error["id"] == 99);
    }

    SECTION("Valid id echoed in bad-version error") {
        std::string line = R"({"jsonrpc": "1.0", "method": "debug.version", "id": 5})";
        auto req = ymir::debug::JsonRpcAdapter::ParseRequest(line, error);
        CHECK_FALSE(req.has_value());
        CHECK(error["error"]["code"] == static_cast<int>(ymir::debug::JsonRpcError::InvalidRequest));
        CHECK(error["id"] == 5);
    }

    SECTION("Parse error id stays null") {
        std::string line = R"({"jsonrpc": "2.0", "method":)";
        auto req = ymir::debug::JsonRpcAdapter::ParseRequest(line, error);
        CHECK_FALSE(req.has_value());
        CHECK(error["error"]["code"] == static_cast<int>(ymir::debug::JsonRpcError::ParseError));
        CHECK(error["id"].is_null());
    }

    SECTION("Invalid id type stays null in error") {
        std::string line = R"({"jsonrpc": "2.0", "method": "debug.version", "id": []})";
        auto req = ymir::debug::JsonRpcAdapter::ParseRequest(line, error);
        CHECK_FALSE(req.has_value());
        CHECK(error["error"]["code"] == static_cast<int>(ymir::debug::JsonRpcError::InvalidRequest));
        CHECK(error["id"].is_null());
    }

    SECTION("Scalar params rejected") {
        std::string line = R"({"jsonrpc": "2.0", "method": "debug.version", "id": 1, "params": "bad"})";
        auto req = ymir::debug::JsonRpcAdapter::ParseRequest(line, error);
        CHECK_FALSE(req.has_value());
        CHECK(error["error"]["code"] == static_cast<int>(ymir::debug::JsonRpcError::InvalidParams));
        CHECK(error["id"] == 1);
    }

    SECTION("Null params rejected") {
        std::string line = R"({"jsonrpc": "2.0", "method": "debug.version", "id": 1, "params": null})";
        auto req = ymir::debug::JsonRpcAdapter::ParseRequest(line, error);
        CHECK_FALSE(req.has_value());
        CHECK(error["error"]["code"] == static_cast<int>(ymir::debug::JsonRpcError::InvalidParams));
        CHECK(error["id"] == 1);
    }

    SECTION("Array params accepted") {
        std::string line = R"({"jsonrpc": "2.0", "method": "debug.version", "id": 1, "params": []})";
        auto req = ymir::debug::JsonRpcAdapter::ParseRequest(line, error);
        REQUIRE(req.has_value());
        CHECK(req->params.is_array());
    }
}

TEST_CASE("JsonRpcAdapter clears outError on success", "[protocol]") {
    nlohmann::json error;
    error = ymir::debug::JsonRpcAdapter::CreateErrorResponse(std::monostate{}, ymir::debug::JsonRpcError::ParseError,
                                                             "prior failure");

    std::string line = R"({"jsonrpc": "2.0", "method": "debug.version", "id": 1})";
    auto req = ymir::debug::JsonRpcAdapter::ParseRequest(line, error);
    REQUIRE(req.has_value());
    CHECK(error.is_null());
}

TEST_CASE("JsonRpcAdapter rejects out-of-range unsigned id as InvalidRequest", "[protocol]") {
    nlohmann::json error;
    // 2^64-1 exceeds int64_t range; nlohmann stores as uint64_t, is_number_unsigned() catches it
    std::string line = R"({"jsonrpc": "2.0", "method": "debug.version", "id": 18446744073709551615})";
    auto req = ymir::debug::JsonRpcAdapter::ParseRequest(line, error);
    CHECK_FALSE(req.has_value());
    CHECK(error["error"]["code"] == static_cast<int>(ymir::debug::JsonRpcError::InvalidRequest));
}

// Nit #3: InstanceStatusResult::state and slave_enabled lack in-class initializers.
// Without them, default-constructing the struct leaves those fields indeterminate (UB).
// Placement new over a 0xFF-filled buffer makes the absence of initializers observable.
TEST_CASE("InstanceStatusResult default-initializes state and slave_enabled", "[protocol]") {
    alignas(ymir::debug::InstanceStatusResult) std::byte buf[sizeof(ymir::debug::InstanceStatusResult)];
    std::memset(buf, 0xFF, sizeof(buf));
    auto *status = ::new (buf) ymir::debug::InstanceStatusResult;

    // Expected: Starting (0) and false. Without in-class initializers: 0xFF garbage.
    CHECK(status->state == ymir::debug::ExecutionState::Starting);
    CHECK_FALSE(status->slave_enabled);

    status->~InstanceStatusResult();
}

// Nit #4: InstanceReadyEvent::state lacks an in-class initializer; same exposure method.
TEST_CASE("InstanceReadyEvent default-initializes state", "[protocol]") {
    alignas(ymir::debug::InstanceReadyEvent) std::byte buf[sizeof(ymir::debug::InstanceReadyEvent)];
    std::memset(buf, 0xFF, sizeof(buf));
    auto *ready = ::new (buf) ymir::debug::InstanceReadyEvent;

    // Expected: Starting (0). Without in-class initializer: 0xFF garbage.
    CHECK(ready->state == ymir::debug::ExecutionState::Starting);

    ready->~InstanceReadyEvent();
}

TEST_CASE("JsonRpcAdapter creates method-not-found errors", "[protocol]") {
    const auto error = ymir::debug::JsonRpcAdapter::CreateMethodNotFoundResponse(ymir::debug::JsonRpcId{int64_t{42}},
                                                                                 "unknown.method");

    CHECK(error["id"] == 42);
    CHECK(error["error"]["code"] == static_cast<int>(ymir::debug::JsonRpcError::MethodNotFound));
    CHECK(error["error"]["data"]["method"] == "unknown.method");
}

TEST_CASE("JsonRpcAdapter ID preservation", "[protocol]") {
    nlohmann::json error;

    SECTION("Integer ID") {
        std::string line = R"({"jsonrpc": "2.0", "method": "debug.version", "id": 42})";
        auto req = ymir::debug::JsonRpcAdapter::ParseRequest(line, error);
        REQUIRE(req.has_value());
        CHECK(std::get<int64_t>(req->id) == 42);
    }

    SECTION("String ID") {
        std::string line = R"({"jsonrpc": "2.0", "method": "debug.version", "id": "req-001"})";
        auto req = ymir::debug::JsonRpcAdapter::ParseRequest(line, error);
        REQUIRE(req.has_value());
        CHECK(std::get<std::string>(req->id) == "req-001");
    }
}
