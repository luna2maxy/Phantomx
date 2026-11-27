#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "phantomx/common/error.hpp"

namespace phantomx::rpc {

enum class RpcErrorCode : int {
  kOk = 0,
  kInvalidRequest = -32600,
  kMethodNotFound = -32601,
  kNotFound = -32004,
  kInvalidParams = -32602,
  kInternalError = -32603,
  kUnauthorized = -32001,
  kForbidden = -32002,
  kRateLimited = -32003,
};

struct RpcRequest {
  std::string id = "1";
  std::string method;
  std::unordered_map<std::string, std::string> params;
  std::string auth_token;
};

struct RpcResponse {
  std::string id;
  bool ok = true;
  RpcErrorCode code = RpcErrorCode::kOk;
  std::string error_message;
  std::unordered_map<std::string, std::string> result;
};

struct RpcAuditEntry {
  std::uint64_t timestamp = 0;
  std::string user;
  std::string method;
  bool allowed = false;
  bool success = false;
  std::string detail;
};

struct RpcUser {
  std::string name;
  std::string token;
  std::vector<std::string> allowed_methods; // empty = all allowed
};

class RpcServer {
 public:
  using Handler = std::function<RpcResponse(const RpcRequest&)>;

  void register_method(std::string method, Handler handler);
  void add_user(RpcUser user);
  void set_rate_limit(std::uint32_t max_requests_per_window, std::uint64_t window_seconds);

  [[nodiscard]] RpcResponse call(const RpcRequest& req, std::uint64_t now_seconds);
  [[nodiscard]] const std::vector<RpcAuditEntry>& audit_log() const noexcept { return audit_; }
  [[nodiscard]] std::vector<std::string> methods() const;

 private:
  [[nodiscard]] bool authorized(const RpcUser& user, const std::string& method) const;
  [[nodiscard]] bool rate_limited(const std::string& user, std::uint64_t now_seconds);

  std::unordered_map<std::string, Handler> handlers_;
  std::unordered_map<std::string, RpcUser> users_by_token_;
  std::vector<RpcAuditEntry> audit_;
  std::uint32_t max_requests_per_window_ = 100;
  std::uint64_t window_seconds_ = 60;
  std::unordered_map<std::string, std::vector<std::uint64_t>> request_times_;
};

void register_default_methods(RpcServer& server);
[[nodiscard]] std::string rpc_response_to_json(const RpcResponse& resp);

}  // namespace phantomx::rpc
