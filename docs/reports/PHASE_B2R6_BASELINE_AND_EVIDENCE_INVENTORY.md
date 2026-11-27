# PHASE B2-R6 — Baseline and B2-R5 Evidence Inventory (Phase 1, Read-Only)

**Status:** COMPLETE (Phase 1 only)  
**Date:** 2026-11-26  
**Mode:** STRICT READ-ONLY AUDIT / INVENTORY — NO EXPERIMENTS, NO PRODUCTION CHANGES  
**Approval:** Owner explicitly approved B2-R6 Phase 1 and Phase 2 only.

## 1. Baseline verification

| Item | Expected (historical) | Observed (current) | Status |
|---|---|---|---|
| HEAD | `d094596549a53eb0695527df06729eca41e737e9` | `d094596549a53eb0695527df06729eca41e737e9` | MATCH |
| Branch | `encryption` | `encryption` | MATCH |
| Dirty worktree entries | 114 | 114 (`git status --short \| wc -l`) | MATCH |

Commands run (read-only):

```bash
git rev-parse HEAD
git branch --show-current
git status --short | wc -l
```

No destructive git commands were run. No files were modified, deleted, stashed, restored, or cleaned.

## 2. B2-R5 evidence preservation status

All required B2-R5 artifacts were located and left intact (no modification, deletion, overwrite, relocation).

| Artifact | Path | Size (bytes) | SHA-256 | Status |
|---|---|---:|---|---|
| Run log | `/tmp/b2r5_run.log` | 3716 | `e665dad8500a8f67ba9eef02b3430839daa1a013c54ffa084558f9624b94b617` | PRESENT |
| Summary CSV | `/tmp/phantomx_b2r5_out/summary.csv` | 450335 | `ce90ff2bd3eb168e3d7b665793e93d6ed675640f6907f579a77c60398d572a7f` | PRESENT |
| Independent overlap audit | `/tmp/phantomx_b2r5_out/independent_overlap_audit.csv` | 388070 | `5ba0e78e45bd8f1f623b13f64792f8439162ee9c33504b508f798ee3cefd85b1` | PRESENT |
| Independent integrity audit | `/tmp/phantomx_b2r5_out/independent_integrity_audit.csv` | 6118314 | `03adfd7ffae9a6a9f808182d8eb192d24b596c951b9f27eb9aeb91fc0ae512f0` | PRESENT |
| Harness source | `/home/ubuntu/b2r5_harness.c` | 14612 | `d474cf60b75bc13fa9fde4dcf0d2ea64da3259b9f3b22fffa3b90b80f3c7b72e` | PRESENT |
| Harness binary | `/home/ubuntu/b2r5_harness` | 30680 | `763cd9b16ff1a6badb472308c4368e5fa94b50b6d52f5ee6f7546a91c493cbb0` | PRESENT |
| B2-R5 report | `docs/reports/PHASE_B2R5_PRODUCTION_PATH_SQLITE_RECOVERY_REPORT.md` | 7107 | `052d67b92276a8f48e7e387cf90847e476e24ef69845e5f57e9045c5a08b8ab9` | PRESENT |
| B2-R5 independent audit report | `docs/reports/PHASE_B2R5_FINAL_INDEPENDENT_EVIDENCE_AUDIT.md` | 10523 | `42866da07b290651342e70b374f5912402af4e69cc71d89894c549ddaf7bb7b6` | PRESENT |
| Baseline status | `/tmp/b2r5_baseline_status.txt` | 3362 | `e8e4cc08a3b4e09edddbcf214cf93dd4874393dd40b961dde5ca4275c443e1c0` | PRESENT |
| Start time | `/tmp/b2r5_start_time.txt` | 21 | `e29d2c6573060f3dfac2d876d03b642ad11ab2fd45f2ceaa121caea710e1eb39` | PRESENT |
| B2-R5 output dir | `/tmp/phantomx_b2r5_out/` | 61203 files | full manifest: `/tmp/b2r6_b2r5_evidence_sha256.txt` | PRESENT |

Full out-dir SHA-256 manifest generated read-only: `/tmp/b2r6_b2r5_evidence_sha256.txt` (61203 entries). No B2-R5 files were modified during inventory.

## 3. B2-R5 independent audit conclusion (preserved)

Preserved as-is from `docs/reports/PHASE_B2R5_FINAL_INDEPENDENT_EVIDENCE_AUDIT.md`:

- **Overall verdict:** PARTIAL — NOT ACCEPTED AS FULLY VERIFIED 8000-ITERATION EXPERIMENT.
- **Verifiable unique iterations:** 7200 (not 8000). 800 duplicate summary rows / overwritten pilot per-worker evidence for iters 0-99.
- **Verifiable operations:** 214200.
- **SQLITE_BUSY_RECOVERY (261):** 0 observed in verifiable raw evidence.
- **Database integrity failures:** 0 in verifiable unique iterations.
- **Independent overlap:** 7197/7200.
- **Production-path equivalence:** NOT EXECUTED.
- **Root cause:** UNVERIFIED.
- **After_commit row mismatch:** harness expected-value bug confirmed; not a demonstrated DB integrity failure.

## 4. B2-R6 evidence boundary

- B2-R5 evidence is read-only and preserved.
- B2-R6 artifacts must be written under separate paths (e.g. `docs/reports/PHASE_B2R6_*.md`, `/tmp/phantomx_b2r6_*`, `/tmp/b2r6_*`). No B2-R6 output may overwrite `/tmp/phantomx_b2r5_out/`.
- No B2-R5/B2-R3/B2-R4 reruns performed.

## 5. Phase 1 compliance

- Production source modified: **NO**
- Production config modified: **NO**
- B2-R5 evidence modified/deleted/overwritten/relocated: **NO**
- Experiments/reproduction run: **NO**
- Destructive git commands: **NO**
- Worktree preservation: **PASS** (HEAD `d094596549a53eb0695527df06729eca41e737e9`, branch `encryption`, dirty 114)

*Phase 1 only. No Phase 3 execution authorized.*