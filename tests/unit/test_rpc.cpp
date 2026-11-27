#include "../test_framework.hpp"
#include "phantomx/rpc/rpc.hpp"

PHX_TEST(rpc_authz_rate_limit_audit) {
  phantomx::rpc::RpcServer server;
  phantomx::rpc::register_default_methods(server);
  server.add_user({"admin", "t1", {}});
  server.add_user({"readonly", "t2", {"getblockcount"}});
  server.set_rate_limit(2, 60);

  phantomx::rpc::RpcRequest req;
  req.id = "1"; req.method = "getblockcount"; req.auth_token = "bad";
  auto r1 = server.call(req, 1);
  PHX_EXPECT(!r1.ok);
  PHX_EXPECT(r1.code == phantomx::rpc::RpcErrorCode::kUnauthorized);

  req.auth_token = "t2";
  auto r2 = server.call(req, 1);
  PHX_EXPECT(r2.ok);

  req.method = "sendrawtransaction";
  auto r3 = server.call(req, 2);
  PHX_EXPECT(!r3.ok);
  PHX_EXPECT(r3.code == phantomx::rpc::RpcErrorCode::kForbidden);

  req.method = "getblockcount";
  auto r4 = server.call(req, 2);
  PHX_EXPECT(r4.ok); // second allowed call within limit

  auto r5 = server.call(req, 3);
  PHX_EXPECT(!r5.ok); // third allowed call exceeds limit 2
  PHX_EXPECT(r5.code == phantomx::rpc::RpcErrorCode::kRateLimited);

  PHX_EXPECT(server.audit_log().size() == 5);
}

PHX_TEST(rpc_default_methods_registered) {
  phantomx::rpc::RpcServer server;
  phantomx::rpc::register_default_methods(server);
  auto methods = server.methods();
  PHX_EXPECT(!methods.empty());
  bool has_getnodeinfo = false;
  bool has_getpoolrewards = false;
  for (const auto& m : methods) {
    if (m == "getnodeinfo") has_getnodeinfo = true;
    if (m == "getpoolrewards") has_getpoolrewards = true;
  }
  PHX_EXPECT(has_getnodeinfo);
  PHX_EXPECT(has_getpoolrewards);
}

