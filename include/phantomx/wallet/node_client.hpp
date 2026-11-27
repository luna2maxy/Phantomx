#pragma once

#include <string>

#include "phantomx/common/error.hpp"
#include "phantomx/rpc/rpc.hpp"

namespace phantomx::wallet {

// Client for the node's RPC endpoint. Uses libcurl for transport and parses
// responses with the same JSON shape produced by phantomx::rpc::rpc_response_to_json.
class NodeClient {
 public:
  explicit NodeClient(std::string base_url);

  [[nodiscard]] Result<phantomx::rpc::RpcResponse> call(const phantomx::rpc::RpcRequest& req) const;

 private:
  std::string base_url_;
};

}  // namespace phantomx::wallet
