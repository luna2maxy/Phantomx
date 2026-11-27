# PHANTOMX — OS-Level Network Isolation Method (TB-020)

**Date:** 2026-11-26  
**Status:** Method prepared; not executed in this environment (requires root/CAP_NET_ADMIN).  
**Related:** `docs/reports/PHANTOMX_BFT_PARTITION_ISOLATION_AUDIT.md`.

## Why not executed here

`ip netns` requires root (sudo interactive auth unavailable in this environment). `unshare -Urn` allows user-namespace veth creation but orchestrating two isolated netns with veth pairing and cross-namespace process launch is not feasible without root or a privileged helper.

## Method (for a root-privileged environment)

1. Create two network namespaces: `nsA`, `nsB`.
2. Create a veth pair `vethA` (in nsA) / `vethB` (in nsB), assign 10.200.1.1/24 and 10.200.1.2/24.
3. Enable loopback in each ns.
4. Launch phantomxd in nsA (testnet) and nsB (testnet) with P2P bound to the veth IPs.
5. Verify same-network connectivity (ready peers = 1 each, chain sync).
6. Apply `iptables -A INPUT -s 10.200.1.2 -j DROP` in nsA (and reciprocal) to simulate partition.
7. Verify: no cross-group TCP (ss in each ns), no ready peers across groups, no false finality (block counts unchanged, BFT h/v unchanged).
8. Heal: remove iptables rule; verify reconnect and convergence.
9. Record ss output, peer counts, block counts, BFT status, finalized hashes per phase.

## Script

A runnable script template is provided at `/tmp/phx_testnet_exec7d_20261126/tb020/os_isolation_test.sh` (requires root). Do not run without root authorization.

## Current evidence status

- Process/socket-level isolation: PROVEN (exec6, 3 trials, 0 cross-group).
- Kernel-level firewall isolation: NOT PROVEN in this environment (method documented). Deployment-time control: `deploy/testnet/firewall/ufw.sh`.
- TB-020 remains PARTIAL. OS-level proof requires a root-privileged run (VPS frozen; no deployment).