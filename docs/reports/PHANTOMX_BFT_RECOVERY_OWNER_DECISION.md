# PHANTOMX — BFT Recovery Owner Decision Package (TB-019)

**Date:** 2026-11-26  
**Status:** DECISION REQUIRED — no code change, no remediation implemented.  
**Related:** `docs/reports/PHANTOMX_BFT_RECOVERY_FAILURE_ANALYSIS.md`, `docs/reports/PHANTOMX_BFT_PARTITION_ISOLATION_AUDIT.md`.

## Evidence preserved (EXEC2–EXEC6)

- EXEC2: 3 trials; trial 2 FAIL (final 2,2,2,2). N3 stale finalized metadata h1 vs others h2 (`0ca511...e6d6` vs `df6f64...eb83`). P2P mesh recovery failure suspected; logs overwritten.
- EXEC4: instrumented repro PASS (4,4,4,4; bft h5/v3); N3 temporarily stale h2/v1 but caught up; no conflicting/false finality.
- EXEC5: 5/5 instrumented PASS (counts 3,3,3,3 or 4,4,4,4; bft h4-5/v3; ALL_DONE EXIT=0). Combined with exec4: 6/6 instrumented PASS.
- EXEC6: duplicate-connection dedup (transport only) removed a plausible confounder; partition matrix 3/3 PASS.
- Read-only DB evidence: `/tmp/phx-b1r-t2/n3/{bft_wal.bin,chain.db}`; N3 WAL consistent with chain metadata (lag, not conflicting finality).

## What is proven

- No conflicting or false finality observed in any run (instrumented or not).
- N3 stale metadata in exec2 trial 2 is consistent with lag, not a safety violation.
- 6/6 instrumented recovery runs PASS.
- Duplicate peer connections (a liveness confounder) addressed in exec6 (transport-only).

## What is NOT proven

- Root cause of exec2 trial 2 is UNCONFIRMED (LIKELY P2P liveness/timing).
- Absence of intermittent failure is not proven by repeated runs.
- N3 stale finalized metadata root cause not confirmed.

## Option A — Accept as bounded operational risk (recommended for isolated testnet)

- Accept the historical N3 lag as a documented liveness/recovery risk.
- Keep TB-019 OPEN for mainnet; do not close.
- Conditions: monitor ready-peer count and cross-node finalized height/hash divergence; treat any conflicting finality as stop-ship; re-run recovery before mainnet and before any live 2-VPS soak.
- No consensus change; no persistent-state change.

## Option B — Keep TB-019 BLOCKED

- Keep TB-019 BLOCKER until the historical failure is reproduced and root cause confirmed.
- Any fix touching persistent state/consensus-adjacent recovery requires separate owner approval and a full before/after evidence package.

## Constraints

- No modification of finality, QC, view-change, startup reconciliation, or persistent BFT state in either option.
- VPS remains FROZEN; TESTNET remains NO-GO until owner accepts a disposition.

## EXEC7B — Option A APPROVED (monitoring disposition)

Owner approved Option A. TB-019 remains OPEN for mainnet; root cause UNCONFIRMED; historical failure OBSERVED.

### Monitoring plan (required)

- **Detect node lag / finalized-height divergence:** poll each validator's `bft_status` and `finalized_height`/`finalized_hash`; alert if any node's finalized height differs from the majority or lags by > 1 block.
- **Detect prolonged absence of ready peers:** alert if `ready_peer_count` < (N-1) for > 30s.
- **Detect failure to catch up after restart:** alert if a restarted node's finalized height does not converge within 60s.
- **Detect conflicting finalized hashes:** any differing `finalized_hash` across nodes → critical incident.
- **Warning thresholds:** finalized height lag ≥ 1 block for > 30s; ready peers < N-1 for > 30s.
- **Critical thresholds:** conflicting finalized hash; false finality; any safety violation.
- **Operator actions:** restart node, check P2P connectivity, verify peer counts, escalate if no convergence in 60s.
- **Escalation:** conflicting finalized hash or false finality immediately escalates to critical incident and invalidates the bounded-risk acceptance for the affected release.

### Safety rule

Any conflicting finalized hash or evidence of false finality immediately escalates to a critical incident and invalidates the bounded-risk acceptance for the affected release.

## Owner decision required

Select Option A or Option B. Until then TB-019 stays PARTIAL/BLOCKER and TESTNET NO-GO.