# PhantomX Two-VPS Testnet Final Readiness Report

**Date:** 2026-11-26  
**Baseline HEAD:** d094596549a53eb0695527df06729eca41e737e9  
**Branch:** encryption  
**Initial dirty worktree:** 106 entries preserved  
**Final git status:** dirty (includes pre-existing 106 + deployment/report artifacts created in this pass)

## A. Repository baseline
- Initial commit: `d094596549a53eb0695527df06729eca41e737e9`
- Initial dirty worktree: 106 entries (`/tmp/tv_baseline_status.txt`)
- Changes preserved: yes, no destructive git commands.
- Final commit: unchanged (no commit created).
- Final git status: dirty; new files under `deploy/testnet/` and `docs/reports/TV_*`.

## B. Architecture (proposed, NOT live-deployed)

| Item | VPS 1 PHANTOMX-A | VPS 2 PHANTOMX-B |
|---|---|---|
| Blockchain | phantomxd node-a instance(s), separate datadir/db/ports/keys | phantomxd node-b instance(s), separate datadir/db/ports/keys |
| POI Pool | pool API/aggregator **DRAFT ONLY** | pool API/coordination **DRAFT ONLY** |
| Public | 443 Pool only after security gate | 443 Pool only after security gate |
| Private | 127.0.0.1:18740 RPC, local DBs, internal metrics | same |

- Node instance count: **BLOCKED/UNDECIDED** by consensus audit. Canonical `config/testnet.json` has no BFT section; `config/testnet-bft-validation.json` is 4 validators but marked ISOLATED TEST-ONLY, NOT CANONICAL TESTNET.
- Validator count: 4 in BFT validation config; 0/unspecified in canonical testnet config. **NOT VERIFIED** for public two-VPS testnet.
- Validator distribution: if 4 validators on 2 VPS, 2 per VPS = 2 failure domains; **validator independence NOT PROVABLE**.
- Network connectivity: requires P2P 18742 between VPS; RPC 18740 private only.

## C. Implementation status

| Component | Status | Evidence |
|---|---|---|
| Repo audit | IMPLEMENTED | `docs/reports/TV_AUDIT_2026-11-26.md` |
| Baseline report | IMPLEMENTED | `docs/reports/TV_BASELINE_2026-11-26.md` |
| Two-VPS deployment artifacts | IMPLEMENTED (draft) | `deploy/testnet/*` |
| Systemd/nginx/firewall templates | IMPLEMENTED (draft) | `deploy/testnet/systemd`, `nginx`, `firewall` |
| Backup/restore scripts | PARTIAL | `deploy/testnet/backup`, `restore` - manual/untested on live VPS |
| Healthcheck scripts | PARTIAL | `deploy/testnet/scripts/healthcheck.sh` |
| POI Pool public API | BLOCKED | `apps/phantomx-pool/main.cpp` stub; no auth/replay/evidence/accounting |
| Multi-node two-VPS live test | NOT RUN | no VPS access/execution |
| Soak 14-28 days | NOT RUN | runbook only |
| SQLITE_BUSY_RECOVERY fix | NOT PROVEN | R18/R19 reproduced 5/261; cause not proven |

## D. Build and tests (executed this pass)

| Command | Exit | Result |
|---|---:|---|
| `cmake --build build-ubuntu --target phantomx_unit_tests -j6` | 0 | PASS |
| `./build-ubuntu/tests/phantomx_unit_tests` | 0 | 123 passed, 0 failed, 123 total |
| `ctest --test-dir build-ubuntu --output-on-failure` | 0 | 11/11 PASS |
| `cmake --build build-ubuntu --target phantomxd phantomx-pool -j6` | 0 | PASS |

Sanitizers: NOT RUN. Two-VPS integration: NOT RUN. Live deployment validation: NOT RUN.

## E. Security
- Public attack surface: intended 80/443, 18742 P2P. See `docs/reports/TV_SECURITY_REPORT.md`.
- RPC isolation: template enforces localhost+allowip+firewall deny 18740; **NOT VERIFIED** live.
- Pool: **BLOCKED** for public exposure (stub).
- Secrets: templates only; no secrets committed.

## F. Reliability
- Restart/recovery/database integrity/pool recovery: **NOT VERIFIED** live.
- SQLITE_BUSY_RECOVERY: **REPRODUCED / NOT PROVEN** (primary=5 extended=261).

## G. Soak test
- Start/end: NOT RUN
- Duration actually completed: 0 days
- Uptime/block/tx/evidence/error/resource: NOT RUN
- Automation/runbook: `docs/reports/TV_RUNBOOK.md`

## H. Final verdict

**NOT READY**

Rationale: no live two-VPS deployment executed; POI Pool is stub/unsafe for public exposure; canonical testnet BFT topology for two VPS is unresolved and testnet-bft-validation is explicitly not canonical/public; SQLITE_BUSY_RECOVERY cause unproven; 14-28 day soak test not run. Existing build/unit/ctest pass, and draft deployment artifacts/runbooks were created, but these are insufficient for readiness.
