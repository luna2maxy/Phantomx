#pragma once

#include <cstdint>
#include <string>

#include "phantomx/common/error.hpp"
#include "phantomx/rpc/rpc.hpp"

namespace phantomx::rpc {

// Blocks forever serving the given RpcServer over HTTP/1.1 on bind_host:port.
// Each POST body must be the JSON representation accepted by the wallet NodeClient
// (method/id/token/params). Responses use rpc_response_to_json.
[[nodiscard]] Result<void> serve_rpc(RpcServer& server, const std::string& bind_host, std::uint16_t port);

}  // namespace phantomx::rpc
