#pragma once

#include <nlohmann/json.hpp>
#include <ymir/debug/protocol/debug_types.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace ymir::debug {

inline constexpr std::string_view kStdioJsonRpcLinesTransport = "stdio-jsonrpc-lines";

using JsonRpcId = DebugRequestId;

struct JsonRpcRequest {
    JsonRpcId id;
    std::string method;
    nlohmann::json params;
    bool is_notification{false};
};

struct JsonRpcResponseSuccess {
    nlohmann::json result;
};
struct JsonRpcResponseError {
    nlohmann::json error;
};
struct JsonRpcResponse {
    JsonRpcId id;
    std::variant<JsonRpcResponseSuccess, JsonRpcResponseError> payload;
};

struct JsonRpcNotification {
    std::string method;
    nlohmann::json params;
};

enum class JsonRpcError : int {
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,
};

class JsonRpcAdapter {
public:
    static nlohmann::json CreateErrorResponse(const JsonRpcId &id, JsonRpcError code, std::string_view message,
                                              const nlohmann::json &data = nullptr) {
        nlohmann::json response = {{"jsonrpc", "2.0"},
                                   {"id", IdToJson(id)},
                                   {"error", {{"code", static_cast<int>(code)}, {"message", message}}}};
        if (!data.is_null()) {
            response["error"]["data"] = data;
        }
        return response;
    }

    static nlohmann::json CreateSuccessResponse(const JsonRpcId &id, const nlohmann::json &result) {
        return {{"jsonrpc", "2.0"}, {"id", IdToJson(id)}, {"result", result}};
    }

    static nlohmann::json CreateNotification(std::string_view method, const nlohmann::json &params) {
        return {{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
    }

    static nlohmann::json CreateMethodNotFoundResponse(const JsonRpcId &id, std::string_view method) {
        return CreateErrorResponse(id, JsonRpcError::MethodNotFound, "Method not found",
                                   nlohmann::json{{"method", method}});
    }

    static std::optional<JsonRpcRequest> ParseRequest(std::string_view line, nlohmann::json &outError) {
        outError = nullptr;
        try {
            auto j = nlohmann::json::parse(line);

            if (j.is_array()) {
                outError = CreateErrorResponse(std::monostate{}, JsonRpcError::InvalidRequest,
                                               "Batch requests are not supported");
                return std::nullopt;
            }

            if (!j.is_object()) {
                outError = CreateErrorResponse(std::monostate{}, JsonRpcError::InvalidRequest, "Invalid request shape");
                return std::nullopt;
            }

            // Extract id early so validation errors can echo it back per JSON-RPC 2.0
            JsonRpcId earlyId = std::monostate{};
            if (j.contains("id")) {
                if (j["id"].is_number_integer()) {
                    if (j["id"].is_number_unsigned()) {
                        // nlohmann stores all non-negative integers as uint64_t on some versions,
                        // so is_number_integer() alone does not guarantee get<int64_t>() is safe.
                        uint64_t uval = j["id"].get<uint64_t>();
                        if (uval <= static_cast<uint64_t>(INT64_MAX)) {
                            earlyId = static_cast<int64_t>(uval);
                        }
                        // else: out of int64_t range, earlyId stays monostate (null in response)
                    } else {
                        earlyId = j["id"].get<int64_t>();
                    }
                } else if (j["id"].is_string()) {
                    earlyId = j["id"].get<std::string>();
                }
                // null or unrecognised type: earlyId stays monostate (null in response)
            }

            if (!j.contains("jsonrpc") || !j["jsonrpc"].is_string() || j["jsonrpc"].get<std::string>() != "2.0") {
                outError =
                    CreateErrorResponse(earlyId, JsonRpcError::InvalidRequest, "Invalid or missing jsonrpc version");
                return std::nullopt;
            }

            if (!j.contains("method") || !j["method"].is_string()) {
                outError = CreateErrorResponse(earlyId, JsonRpcError::InvalidRequest, "Missing or invalid method");
                return std::nullopt;
            }

            JsonRpcRequest req;
            req.method = j["method"].get<std::string>();

            if (j.contains("params")) {
                req.params = j["params"];
                if (!req.params.is_object() && !req.params.is_array()) {
                    outError =
                        CreateErrorResponse(earlyId, JsonRpcError::InvalidParams, "params must be object or array");
                    return std::nullopt;
                }
            } else {
                req.params = nlohmann::json::object();
            }

            if (j.contains("id")) {
                if (j["id"].is_number_integer()) {
                    if (j["id"].is_number_unsigned()) {
                        uint64_t uval = j["id"].get<uint64_t>();
                        if (uval > static_cast<uint64_t>(INT64_MAX)) {
                            outError = CreateErrorResponse(earlyId, JsonRpcError::InvalidRequest, "Invalid id type");
                            return std::nullopt;
                        }
                        req.id = static_cast<int64_t>(uval);
                    } else {
                        req.id = j["id"].get<int64_t>();
                    }
                } else if (j["id"].is_string()) {
                    req.id = j["id"].get<std::string>();
                } else if (j["id"].is_null()) {
                    req.id = std::monostate{};
                } else {
                    outError = CreateErrorResponse(earlyId, JsonRpcError::InvalidRequest, "Invalid id type");
                    return std::nullopt;
                }
                req.is_notification = false;
            } else {
                req.is_notification = true;
            }

            return req;
        } catch (const nlohmann::json::exception &) {
            outError = CreateErrorResponse(std::monostate{}, JsonRpcError::ParseError, "Parse error");
            return std::nullopt;
        }
    }

private:
    static nlohmann::json IdToJson(const JsonRpcId &id) {
        return std::visit(
            [](auto &&arg) -> nlohmann::json {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return nullptr;
                } else {
                    return arg;
                }
            },
            id);
    }
};

} // namespace ymir::debug
