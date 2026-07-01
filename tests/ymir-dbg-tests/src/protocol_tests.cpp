#include <catch2/catch_test_macros.hpp>

#include <protocol/json_rpc_adapter.hpp>
#include <protocol/line_framer.hpp>

#include <string>
#include <string_view>
#include <variant>
#include <vector>

TEST_CASE("ymir-dbg LineFramer buffers protocol lines", "[protocol]") {
    std::vector<std::string> lines;
    ymir::debug::LineFramer framer([&](std::string_view line) { lines.emplace_back(line); },
                                   [](ymir::debug::LineFramerError) { FAIL("Unexpected error"); });

    const std::string first = R"({"jsonrpc":"2.0","id":"a)";
    const std::string second = R"(bc","result":{}})"
                               "\n";

    framer.Push(first.data(), first.size());
    CHECK(lines.empty());

    framer.Push(second.data(), second.size());
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == R"({"jsonrpc":"2.0","id":"abc","result":{}})");
}

TEST_CASE("ymir-dbg LineFramer recovers after oversized line", "[protocol]") {
    bool sawError{};
    std::vector<std::string> lines;
    ymir::debug::LineFramer framer([&](std::string_view line) { lines.emplace_back(line); },
                                   [&](ymir::debug::LineFramerError err) {
                                       sawError = true;
                                       CHECK(err == ymir::debug::LineFramerError::LineTooLong);
                                   });

    std::string oversized(ymir::debug::LineFramer::kMaxLineLength + 1, 'x');
    oversized += "\n";
    oversized += R"({"jsonrpc":"2.0","method":"debug.version","id":1})";
    oversized += "\n";

    framer.Push(oversized.data(), oversized.size());
    CHECK(sawError);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == R"({"jsonrpc":"2.0","method":"debug.version","id":1})");
}

TEST_CASE("ymir-dbg JsonRpcAdapter preserves request IDs", "[protocol]") {
    nlohmann::json error;

    auto integerRequest =
        ymir::debug::JsonRpcAdapter::ParseRequest(R"({"jsonrpc":"2.0","method":"debug.version","id":7})", error);
    REQUIRE(integerRequest.has_value());
    CHECK(std::get<int64_t>(integerRequest->id) == 7);
    CHECK_FALSE(integerRequest->is_notification);

    auto stringRequest =
        ymir::debug::JsonRpcAdapter::ParseRequest(R"({"jsonrpc":"2.0","method":"debug.version","id":"req-7"})", error);
    REQUIRE(stringRequest.has_value());
    CHECK(std::get<std::string>(stringRequest->id) == "req-7");
}

TEST_CASE("ymir-dbg JsonRpcAdapter clears outError on success", "[protocol]") {
    nlohmann::json error;
    error = ymir::debug::JsonRpcAdapter::CreateErrorResponse(std::monostate{}, ymir::debug::JsonRpcError::ParseError,
                                                             "prior failure");

    auto request =
        ymir::debug::JsonRpcAdapter::ParseRequest(R"({"jsonrpc":"2.0","method":"debug.version","id":1})", error);
    REQUIRE(request.has_value());
    CHECK(error.is_null());
}

TEST_CASE("ymir-dbg JsonRpcAdapter handles malformed and unsupported input", "[protocol]") {
    nlohmann::json error;

    auto malformed = ymir::debug::JsonRpcAdapter::ParseRequest(R"({"jsonrpc":"2.0","method":)", error);
    CHECK_FALSE(malformed.has_value());
    CHECK(error["error"]["code"] == static_cast<int>(ymir::debug::JsonRpcError::ParseError));

    auto batch = ymir::debug::JsonRpcAdapter::ParseRequest(R"([{"jsonrpc":"2.0","method":"debug.version"}])", error);
    CHECK_FALSE(batch.has_value());
    CHECK(error["error"]["code"] == static_cast<int>(ymir::debug::JsonRpcError::InvalidRequest));

    auto nonStringVersion =
        ymir::debug::JsonRpcAdapter::ParseRequest(R"({"jsonrpc":2,"method":"debug.version","id":1})", error);
    CHECK_FALSE(nonStringVersion.has_value());
    CHECK(error["error"]["code"] == static_cast<int>(ymir::debug::JsonRpcError::InvalidRequest));

    // Valid id echoed in error response; parse error and invalid id type produce null id
    auto missingMethod = ymir::debug::JsonRpcAdapter::ParseRequest(R"({"jsonrpc":"2.0","id":7})", error);
    CHECK_FALSE(missingMethod.has_value());
    CHECK(error["error"]["code"] == static_cast<int>(ymir::debug::JsonRpcError::InvalidRequest));
    CHECK(error["id"] == 7);

    auto parseErrorId = ymir::debug::JsonRpcAdapter::ParseRequest(R"({"jsonrpc":"2.0","method":)", error);
    CHECK_FALSE(parseErrorId.has_value());
    CHECK(error["id"].is_null());

    auto invalidIdType =
        ymir::debug::JsonRpcAdapter::ParseRequest(R"({"jsonrpc":"2.0","method":"debug.version","id":[]})", error);
    CHECK_FALSE(invalidIdType.has_value());
    CHECK(error["id"].is_null());

    auto scalarParams = ymir::debug::JsonRpcAdapter::ParseRequest(
        R"({"jsonrpc":"2.0","method":"debug.version","id":3,"params":"bad"})", error);
    CHECK_FALSE(scalarParams.has_value());
    CHECK(error["error"]["code"] == static_cast<int>(ymir::debug::JsonRpcError::InvalidParams));
    CHECK(error["id"] == 3);

    auto nullParams = ymir::debug::JsonRpcAdapter::ParseRequest(
        R"({"jsonrpc":"2.0","method":"debug.version","id":4,"params":null})", error);
    CHECK_FALSE(nullParams.has_value());
    CHECK(error["error"]["code"] == static_cast<int>(ymir::debug::JsonRpcError::InvalidParams));
    CHECK(error["id"] == 4);

    auto arrayParams = ymir::debug::JsonRpcAdapter::ParseRequest(
        R"({"jsonrpc":"2.0","method":"debug.version","id":5,"params":[]})", error);
    REQUIRE(arrayParams.has_value());
    CHECK(arrayParams->params.is_array());
}
