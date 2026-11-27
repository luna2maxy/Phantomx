# PhantomX RPC Reference

## Overview

PhantomX provides a JSON-RPC-style method dispatch foundation with authentication, authorization, rate limiting, error codes, and audit logging. The CLI (`phantomx-cli` / `apps/phantomx-cli`) accesses allowed functions through RPC dispatch.

This is a foundation implementation: method handlers are registered in-process; production deployment should expose them over authenticated transport (HTTP/TLS or Unix socket) with persistent audit storage.

## Request / Response

### Request

```json
{
  "id": "1",
  "method": "getblockcount",
  "params": { "key": "value" },
  "auth_token": "<token>"
}
```

### Success Response

```json
{
  "id": "1",
  "result": { "method": "getblockcount", "status": "foundation" },
  "error": null
}
```

### Error Response

```json
{
  "id": "1",
  "result": null,
  "error": { "code": -32601, "message": "method not found" }
}
```

## Error Codes

| Code | Name | Meaning |
|---:|---|---|
| 0 | OK | Success |
| -32600 | Invalid Request | Malformed request |
| -32601 | Method Not Found | Unknown method |
| -32602 | Invalid Params | Missing/invalid params |
| -32603 | Internal Error | Handler failure |
| -32001 | Unauthorized | Missing/invalid auth token |
| -32002 | Forbidden | Authenticated but not authorized for method |
| -32003 | Rate Limited | Request limit exceeded |

## Authentication / Authorization

- Every RPC call requires `auth_token`.
- Users are configured with token, name, and allowed method list.
- Empty allowed method list means all registered methods are allowed for that user (admin/dev policy; production should default deny).
- CLI uses `--rpc-token` and `--rpc-user`.

## Rate Limits

- Configurable per-server: `max_requests_per_window`, `window_seconds`.
- Rate limiting is applied after auth/authz and before handler execution.
- Exceeded requests return `-32003 Rate Limited` and are audit-logged.

## Audit Log

Each RPC call records an audit entry:

- timestamp
- user
- method
- allowed
- success
- detail

Production should persist audit logs append-only and protect them from tampering.

## Method Groups

### Node RPC
- `getnodeinfo`
- `getnetworkinfo`
- `getblockchaininfo`
- `getpeerinfo`
- `getconnectioncount`

### Blockchain RPC
- `getblockcount`
- `getbestblockhash`
- `getblock`
- `getblockhash`
- `gettransaction`
- `sendrawtransaction`

### Wallet RPC
- `getnewaddress`
- `getbalance`
- `listtransactions`
- `sendtoaddress`
- `backupwallet`

### PoI RPC
- `getpoistats`
- `getcontributions`
- `getpoievidence`
- `getepochinfo`

### Verifier RPC
- `getverifierinfo`
- `getverifierjobs`
- `getverificationresult`

### Pool RPC
- `getpoolinfo`
- `getpoolworkers`
- `getpoolrewards`

## CLI

`phantomx-cli <method> [--key value ...] --rpc-token <token>`

- CLI dispatches commands through RPC.
- `--help` shows CLI options; running with no command lists registered RPC methods.
- In foundation mode, dispatch is in-process; production can point `--rpc` to a remote authenticated RPC endpoint.

## Files

- `include/phantomx/rpc/rpc.hpp`
- `src/rpc/rpc.cpp`
- `apps/phantomx-cli/main.cpp`
- `tests/unit/test_rpc.cpp`
- `docs/RPC_REFERENCE.md`
