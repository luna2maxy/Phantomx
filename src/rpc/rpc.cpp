#include "phantomx/rpc/rpc.hpp"

#include <algorithm>
#include <sstream>

namespace phantomx::rpc {
namespace {

std::string json_escape(const std::string& in) {
  std::string out;
  out.reserve(in.size() + 8);
  for (char c : in) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c; break;
    }
  }
  return out;
}

RpcResponse ok(std::string id, std::unordered_map<std::string, std::string> result) {
  RpcResponse r; r.id = std::move(id); r.ok = true; r.code = RpcErrorCode::kOk; r.result = std::move(result); return r;
}
RpcResponse err(std::string id, RpcErrorCode code, std::string msg) {
  RpcResponse r; r.id = std::move(id); r.ok = false; r.code = code; r.error_message = std::move(msg); return r;
}

}  // namespace

void RpcServer::register_method(std::string method, Handler handler) {
  handlers_[std::move(method)] = std::move(handler);
}

void RpcServer::add_user(RpcUser user) {
  users_by_token_[user.token] = std::move(user);
}

void RpcServer::set_rate_limit(std::uint32_t max_requests_per_window, std::uint64_t window_seconds) {
  max_requests_per_window_ = max_requests_per_window;
  window_seconds_ = window_seconds == 0 ? 1 : window_seconds;
}

bool RpcServer::authorized(const RpcUser& user, const std::string& method) const {
  if (user.allowed_methods.empty()) return true;
  return std::find(user.allowed_methods.begin(), user.allowed_methods.end(), method) != user.allowed_methods.end();
}

bool RpcServer::rate_limited(const std::string& user, std::uint64_t now_seconds) {
  auto& times = request_times_[user];
  const std::uint64_t cutoff = now_seconds > window_seconds_ ? (now_seconds - window_seconds_) : 0;
  times.erase(std::remove_if(times.begin(), times.end(), [&](std::uint64_t t) { return t < cutoff; }), times.end());
  if (times.size() >= max_requests_per_window_) return true;
  times.push_back(now_seconds);
  return false;
}

RpcResponse RpcServer::call(const RpcRequest& req, std::uint64_t now_seconds) {
  RpcAuditEntry audit;
  audit.timestamp = now_seconds;
  audit.method = req.method;

  auto uit = users_by_token_.find(req.auth_token);
  if (uit == users_by_token_.end()) {
    audit.user = "anonymous";
    audit.allowed = false;
    audit.success = false;
    audit.detail = "unauthorized";
    audit_.push_back(audit);
    return err(req.id, RpcErrorCode::kUnauthorized, "unauthorized");
  }
  const auto& user = uit->second;
  audit.user = user.name;

  if (!authorized(user, req.method)) {
    audit.allowed = false;
    audit.success = false;
    audit.detail = "forbidden";
    audit_.push_back(audit);
    return err(req.id, RpcErrorCode::kForbidden, "forbidden");
  }
  audit.allowed = true;

  if (rate_limited(user.name, now_seconds)) {
    audit.success = false;
    audit.detail = "rate_limited";
    audit_.push_back(audit);
    return err(req.id, RpcErrorCode::kRateLimited, "rate limited");
  }

  auto it = handlers_.find(req.method);
  if (it == handlers_.end()) {
    audit.success = false;
    audit.detail = "method_not_found";
    audit_.push_back(audit);
    return err(req.id, RpcErrorCode::kMethodNotFound, "method not found");
  }

  RpcResponse resp;
  try {
    resp = it->second(req);
  } catch (const std::exception& e) {
    audit.success = false;
    audit.detail = e.what();
    audit_.push_back(audit);
    return err(req.id, RpcErrorCode::kInternalError, "internal error");
  }
  audit.success = resp.ok;
  audit.detail = resp.ok ? "ok" : resp.error_message;
  audit_.push_back(audit);
  return resp;
}

std::vector<std::string> RpcServer::methods() const {
  std::vector<std::string> out;
  out.reserve(handlers_.size());
  for (const auto& [k, _] : handlers_) out.push_back(k);
  std::sort(out.begin(), out.end());
  return out;
}

std::string rpc_response_to_json(const RpcResponse& resp) {
  std::ostringstream os;
  os << "{\"id\":\"" << json_escape(resp.id) << "\",";
  if (resp.ok) {
    os << "\"result\":{";
    bool first = true;
    for (const auto& [k, v] : resp.result) {
      if (!first) os << ",";
      first = false;
      os << "\"" << json_escape(k) << "\":\"" << json_escape(v) << "\"";
    }
    os << "},\"error\":null}";
  } else {
    os << "\"result\":null,\"error\":{\"code\":" << static_cast<int>(resp.code)
       << ",\"message\":\"" << json_escape(resp.error_message) << "\"}}";
  }
  return os.str();
}

void register_default_methods(RpcServer& server) {
  auto reg = [&](const char* name, const char* status) {
    server.register_method(name, [name, status](const RpcRequest& req) {
      return ok(req.id, {{"method", name}, {"status", status}});
    });
  };
  // Node
  reg("getnodeinfo", "foundation"); reg("getnetworkinfo", "foundation"); reg("getblockchaininfo", "foundation");
  reg("getpeerinfo", "foundation"); reg("getconnectioncount", "foundation");
  // Blockchain
  reg("getblockcount", "foundation"); reg("getbestblockhash", "foundation"); reg("getblock", "foundation");
  reg("getblockhash", "foundation"); reg("gettransaction", "foundation"); reg("sendrawtransaction", "foundation");
  // Wallet
  reg("getnewaddress", "foundation"); reg("getbalance", "foundation"); reg("listtransactions", "foundation");
  reg("sendtoaddress", "foundation"); reg("backupwallet", "foundation");
  // PoI
  reg("getpoistats", "foundation"); reg("getcontributions", "foundation"); reg("getpoievidence", "foundation"); reg("getepochinfo", "foundation");
  // Verifier
  reg("getverifierinfo", "foundation"); reg("getverifierjobs", "foundation"); reg("getverificationresult", "foundation");
  // Pool
  reg("getpoolinfo", "foundation"); reg("getpoolworkers", "foundation"); reg("getpoolrewards", "foundation");
}

}  // namespace phantomx::rpc
