# PHANTOMX — EXEC7C Report

**Date:** 2026-11-26  
**Evidence dir:** `/tmp/phx_testnet_exec7c_20261126/`  
**Baseline:** HEAD `d094596549a53eb0695527df06729eca41e737e9`, dirty 115+. No code changes in this phase (docs/tests/diagnostics only).

## 1. TB-019 monitoring & escalation
- Created `docs/reports/PHANTOMX_BFT_MONITORING_ESCALATION.md` with detection criteria, thresholds, escalation, and operator actions.
- Diagnostic helper: `/tmp/phx_testnet_exec7c_20261126/tb019/monitor.py` (read-only).
- Option A approved; TB-019 remains OPEN for mainnet; root cause UNCONFIRMED.

## 2. TB-020 OS-level isolation
- Attempted kernel-level (netns/iptables) isolation. `unshare -Urn` permits user-namespace veth creation, but `ip netns` requires root (sudo interactive auth unavailable). Full multi-netns firewall experiment not runnable in this environment.
- Process-level evidence from exec6 (3 trials, 0 cross-group sockets, ss -tnp) remains the strongest isolation proof. TB-023 (network identity wiring) adds application-layer cross-network rejection.
- Kernel-level firewall enforcement is a deployment-time control (deploy/testnet/firewall/ufw.sh) to be verified during VPS deployment (frozen).
- Status: PARTIAL (process-level proven; OS-level not proven in this environment).

## 3. TB-021 remaining tests
- AuthZ matrix: already covered by unit test `rpc_authz_rate_limit_audit` (allowed_methods, rate limit, audit log) — PASS.
- Adversarial P2P frames: probe sent bad magic, bad checksum, oversized length, unknown type, truncated, valid frame; node RPC stayed responsive, no crash markers. Evidence: `/tmp/phx_testnet_exec7c_20261126/adv/`.
- Timeout/resource exhaustion: code paths present (handshake 5s, idle 30s, rate 500/s, max_connections 64); controlled tests NOT RUN.
- 900k-param policy: still open (owner decision).
- Status: PARTIAL.

## 4. TB-022 POI Pool exclusion verification
- Audit of deploy/docs confirms pool is commented out in deploy.sh and smoke script notes "pool disabled until secured"; but healthcheck.sh and README still reference pool.
- Verification report: `docs/reports/PHANTOMX_POOL_EXCLUSION_VERIFICATION.md`.
- Recommendation: explicitly mark pool OUT OF SCOPE in runbooks; gate/remove pool healthcheck for initial testnet profile. PA-001..PA-011 remain OPEN.
- Status: exclusion verified for initial testnet (documentation hardening recommended).

## Overall verdict
**TESTNET NO-GO — VPS FROZEN.** TB-023 closed; other gates remain partial/open. No deployment. Stop after this report.