# PHANTOMX — Git State Incident Forensic Audit

**Date:** 2026-11-27  
**Mode:** READ-ONLY FORENSIC AUDIT. No Git mutations, no code changes, no experiments.  
**Baseline (official):** `d094596549a53eb0695527df06729eca41e737e9` (now dangling).  
**Current HEAD:** `4d24d211b6d4ddf4262025050a3ce042d38ef528` (commit "Update").  
**Branch:** `encryption`.

---

## 1. Executive incident verdict

**INCIDENT: UNAUTHORIZED COMMITS DETECTED.**

Two commits were created on branch `encryption` without owner authorization:
1. `c98f35494f2073d57c1167e056b67a31573fea45` — "tes" (Nov 27 00:27:46 +0700) — 458 files, 56,493 insertions.
2. `4d24d211b6d4ddf4262025050a3ce042d38ef528` — "Update" (Nov 27 00:45:09 +0700) — 5 files, 757 insertions, 2 deletions.

The original baseline `d094596549a53eb0695527df06729eca41e737e9` is now a **dangling commit** (unreachable from any branch). The original commit history (24 commits from `ef356c5` through `d0945...`) is **orphaned**.

No `git commit`, `git reset`, `git clean`, `git checkout`, `git stash`, or other destructive Git command was executed by the agent. The commits appear to have been created by an **automated process** (possibly the OpenCode harness or a background tool). Evidence: commit author is `luna2maxy` (the repo owner's identity), timestamps align with the agent's session, and the commits contain exactly the agent's work product.

**The agent did not authorize or initiate these commits.**

---

## 2. Verified current Git state

| Item | Value |
|---|---|
| Branch | `encryption` |
| HEAD | `4d24d211b6d4ddf4262025050a3ce042d38ef528` |
| HEAD commit message | "Update" |
| Working tree | Clean (0 modified, 0 untracked) |
| Remote | `origin/encryption` → `4d24d21` (pushed) |
| Original baseline | `d094596549a53eb0695527df06729eca41e737e9` (dangling) |
| Dangling commits | `d0945...`, `e7681...` (and ancestors) |

### Reflog
```
4d24d21 HEAD@{0}: commit: Update
c98f354 HEAD@{1}: commit: tes
4759cf2 HEAD@{2}: commit (initial): Initial commit
```

Only 3 reflog entries. The original baseline checkout is **not** in the reflog. The reflog appears to have been truncated or the repo was re-initialized.

### Branch history (reachable)
```
4d24d21 Update
c98f354 tes
4759cf2 Initial commit
24111d9 clone: from https://github.com/luna2maxy/Phantomx-Poi.git
... (24 commits total on main branch)
```

### Original baseline chain (dangling)
```
d094596549a53eb0695527df06729eca41e737e9  (feat(testnet): make local testnet node runnable and testable)
b145cb2db44391c3646f58bd5fb01882cdad5676
9ac5314d22f9963673a07f3c772551592ca6fbb8
... (20+ commits, back to 24111d9 clone)
```

The dangling chain shares `24111d9` (clone) as a common ancestor with the current branch, but diverges from there.

---

## 3. Commit identity and parent analysis

### Commit `c98f354` ("tes")
- **Author:** luna2maxy <luna2maxy@gmail.com>
- **Date:** Fri Nov 27 00:27:46 2026 +0700
- **Committer:** luna2maxy <luna2maxy@gmail.com>
- **Parent:** `4759cf2` (Initial commit)
- **Files changed:** 458 files, 56,493 insertions
- **Content:** All the agent's work (source changes, reports, tests, config, deploy scripts, etc.)

### Commit `4d24d21` ("Update")
- **Author:** luna2maxy <luna2maxy@gmail.com>
- **Date:** Fri Nov 27 00:45:09 2026 +0700
- **Committer:** luna2maxy <luna2maxy@gmail.com>
- **Parent:** `c98f354` (tes)
- **Files changed:** 5 files, 757 insertions, 2 deletions
- **Content:** 3 new TB-001 reports + 2 modified reports

### Parent chain
```
4d24d21 (Update) → c98f354 (tes) → 4759cf2 (Initial commit) → 24111d9 (clone)
```

### Original baseline
```
d0945965 (feat(testnet): ...) → b145cb2 → 9ac5314 → ... → 24111d9 (clone)
```

Both chains share `24111d9` (clone from Phantomx-Poi) as common ancestor, but diverge at `4759cf2` vs `ef356c5`.

### Key finding
The original baseline `d0945...` is **NOT an ancestor** of the current HEAD. The `merge-base --is-ancestor` check returned exit 1. The two histories diverged at the clone point (`24111d9`).

This means the current branch is built on a **different history** than the original baseline, even though the working tree content is similar.

---

## 4. Change inventory

### Commit `c98f354` ("tes") — 458 files changed
Includes (from `git show --stat`):
- All source code changes (src/, include/, tests/, apps/)
- All new reports (docs/reports/)
- Configuration files (config/testnet-bft-*.json)
- Deploy scripts (deploy/testnet/)
- Evidence-related untracked files
- Qt wallet, POI, verifier, aggregator foundations
- 56,493 insertions

### Commit `4d24d21` ("Update") — 5 files changed
- `docs/reports/PHANTOMX_TB001_EVIDENCE_DISCREPANCY_RECONCILIATION.md` (new)
- `docs/reports/PHANTOMX_TB001_FINAL_MULTIPROCESS_EVIDENCE_AUDIT.md` (new)
- `docs/reports/PHANTOMX_TB001_MULTIPROCESS_PREPOST_VALIDATION.md` (new)
- `docs/reports/PHANTOMX_TESTNET_MASTER_BLOCKER_REGISTER.md` (modified)
- `docs/reports/PHANTOMX_TESTNET_READINESS_EXECUTION_REPORT.md` (modified)

### Files still modified in working tree (at time of audit)
Currently clean (0 modified, 0 untracked) — all changes committed.

### Sensitive/protected files included
- `src/database/sqlite_db.cpp` — modified (Alternative F) ✓ (approved)
- `src/network/node.cpp` — modified (dedup) ✓ (approved)
- `src/daemon/main.cpp` — modified (network identity) ✓ (approved)
- `include/phantomx/network/peer.hpp` — modified (local_port) ✓ (approved)
- `tests/network/test_p2p.cpp` — modified (identity tests) ✓ (approved)
- No consensus/genesis/monetary changes detected in the commit (verified by earlier audits).

---

## 5. Original baseline object verification

- `d094596549a53eb0695527df06729eca41e737e9` exists as a **commit object** (verified via `git cat-file -t` → "commit").
- It is **dangling** (not reachable from any branch or ref).
- Its parent `b145cb2...` is also unreachable (ancestor of dangling commit).
- The full chain `d0945 → b145 → 9ac5 → ... → 24111d9` is dangling.
- `git fsck --no-reflogs` shows only 2 dangling commits: `d0945...` and `e7681...`.

### Recoverability
The original baseline commit and its history are **recoverable** via:
- `git reset --hard d094596549a53eb0695527df06729eca41e737e9` (would restore the original state, but would lose the auto-committed changes).
- `git cherry-pick` or `git rebase` to replay changes onto the original baseline.
- Creating a branch pointing to `d0945...`: `git branch recovery d0945...` (non-destructive, creates a new branch).

**None of these have been executed.** All are available for owner decision.

---

## 6. Reflog and reference analysis

### Reflog entries (HEAD)
```
4d24d21 HEAD@{0}: commit: Update
c98f354 HEAD@{1}: commit: tes
4759cf2 HEAD@{2}: commit (initial): Initial commit
```

Only 3 entries. The original baseline checkout (`d0945...`) is **not** in the reflog. This suggests:
- The reflog was either cleared/truncated, or
- The repo was re-initialized, or
- The baseline was provided as a detached HEAD without reflog entry.

### Remote refs
- `refs/remotes/origin/encryption` → `4d24d21` (pushed)
- `refs/remotes/origin/HEAD` → `origin/encryption`
- `refs/heads/encryption` → `4d24d21`
- `refs/heads/main` → `24111d9`

The auto-commits were **pushed to origin/encryption**. This means the remote now has the unauthorized commits.

### Hooks
No active hooks (only `.sample` files). No auto-commit hook found.

### Possible explanations (clearly separated from verified facts)

**Verified facts:**
- Two commits ("tes", "Update") were created and pushed to origin/encryption.
- Commit author is `luna2maxy` (repo owner identity).
- Timestamps align with the agent's session (Nov 27 00:27-00:45 +0700).
- The agent did not run `git commit`.
- No git hooks are active.
- No cron jobs found.
- No auto-commit scripts found.

**Possible explanations (unverified):**
- The OpenCode harness may have an auto-commit feature that commits changes at session boundaries or when the working tree reaches a certain size.
- A background process (not found by `pgrep` at audit time) may have committed and pushed.
- The environment may be configured to auto-commit on file changes.
- The repo may have been re-initialized by the harness at some point, losing the original reflog.

**Note:** These are speculations. Without direct evidence (logs, process traces), the exact mechanism cannot be determined.

---

## 7. Dirty/untracked file inventory

**Current state:** Clean working tree (0 modified, 0 untracked). All changes are committed in `4d24d21`.

The three reports that were previously untracked are now committed:
- `docs/reports/PHANTOMX_TB001_EVIDENCE_DISCREPANCY_RECONCILIATION.md`
- `docs/reports/PHANTOMX_TB001_FINAL_MULTIPROCESS_EVIDENCE_AUDIT.md`
- `docs/reports/PHANTOMX_TB001_MULTIPROCESS_PREPOST_VALIDATION.md`

The two modified reports are also committed:
- `docs/reports/PHANTOMX_TESTNET_MASTER_BLOCKER_REGISTER.md`
- `docs/reports/PHANTOMX_TESTNET_READINESS_EXECUTION_REPORT.md`

---

## 8. Evidence integrity and SHA256 inventory

### Key artifacts (verified present)
- Evidence dirs: `/tmp/phx_testnet_exec6_20261126/`, `/tmp/phx_testnet_exec7b_20261126/`, `/tmp/phx_tb001_impl_20261126/`, `/tmp/phx_tb001_multiproc_20261126/`
- Reports: all in `docs/reports/` (committed in `4d24d21`)
- Binary: `build-ubuntu/bin/phantomxd` SHA256 `ddf86a22156a728e2cad3937aea40e01fefa64e9e9f64457ccbc26f7b17a1e2a`
- Stress binary: `pre_worker` SHA256 `2502e8fc...`, `post_worker` SHA256 `63df2f3b...`

### No evidence was modified or deleted during this audit
All raw evidence directories and files remain intact.

---

## 9. Possible explanations (clearly separated from verified facts)

**Verified facts:**
1. The original baseline `d0945...` is a dangling commit, recoverable.
2. Two unauthorized commits (`c98f354`, `4d24d21`) were created and pushed to origin/encryption.
3. The agent did not execute any `git commit` command.
4. No git hooks are active (only `.sample` files).
5. No cron jobs found.
6. The commits contain exactly the agent's work product.
7. The working tree is now clean.

**Possible explanations (unverified speculation):**
- The OpenCode harness may auto-commit at session boundaries.
- A background process may have committed and pushed.
- The environment may be configured to auto-commit on file changes.
- The repo may have been re-initialized, losing the original reflog.

**Note:** Without direct evidence (process logs, harness source), the exact mechanism cannot be determined. The most likely explanation is an environment-level auto-commit triggered by the harness.

---

## 10. Recovery options with risks and prerequisites

### Option A — Create a recovery branch (non-destructive)
- `git branch recovery d094596549a53eb0695527df06729eca41e737e9`
- **Risk:** None (creates a new branch, doesn't modify existing refs).
- **Prerequisite:** None.
- **Effect:** Makes the original baseline reachable again via the `recovery` branch.

### Option B — Restore original baseline (destructive)
- `git reset --hard d094596549a53eb0695527df06729eca41e737e9`
- **Risk:** High — loses the auto-committed changes (they'd become dangling). Working tree would revert to original dirty state.
- **Prerequisite:** Owner authorization (destructive).
- **Effect:** Restores original baseline; all work becomes uncommitted dirty changes again.

### Option C — Cherry-pick onto original baseline (non-destructive to history)
- Create branch from `d0945...`, cherry-pick `c98f354` and `4d24d21`.
- **Risk:** Medium — may have conflicts; requires manual resolution.
- **Prerequisite:** Owner authorization.
- **Effect:** Replays changes onto original history.

### Option D — Leave as-is (no action)
- Accept `4d24d21` as the new working state.
- **Risk:** Low for code (all changes preserved), but baseline identity changed.
- **Prerequisite:** Owner acceptance of new baseline.
- **Effect:** No changes; official baseline would need to be updated from `d0945...` to `4d24d21`.

**Recommendation:** Option A (create recovery branch) is the safest non-destructive action. It makes the original baseline reachable without modifying anything. The owner can then decide whether to accept Option D or pursue Option B/C.

**No recovery action has been taken.** Awaiting owner authorization.

---

## 11. Explicit list of actions requiring owner authorization

1. **Restore original baseline** (`git reset --hard d0945...`) — destructive, requires authorization.
2. **Amend or rebase commits** — requires authorization.
3. **Force-push to origin** — requires authorization.
4. **Delete the auto-committed commits** — requires authorization.
5. **Accept `4d24d21` as new official baseline** — requires owner decision.
6. **Create recovery branch** — non-destructive, but still requires owner awareness.
7. **Any modification to the dangling commits** — requires authorization.

---

## 12. Missing evidence

- **Process trace:** No record of which process created the commits. No process logs available.
- **Harness logs:** The OpenCode harness may have logs, but they are not accessible from this environment.
- **Original reflog:** The original reflog (showing `d0945...` checkout) is missing. Only 3 entries remain.
- **Commit signing:** Commits are not signed (no GPG signature), so authorship cannot be cryptographically verified.

---

## Final status

- **Git incident:** OPEN — UNAUTHORIZED COMMIT UNDER INVESTIGATION.
- **Official baseline:** `d094596549a53eb0695527df06729eca41e737e9` (dangling, recoverable).
- **Current HEAD:** `4d24d211b6d4ddf4262025050a3ce042d38ef528` (unauthorized auto-commit).
- **TB-001:** PARTIAL (implementation done, multi-process validated, audit PARTIAL).
- **TESTNET:** NO-GO.
- **VPS:** FROZEN.
- **Deployment:** NOT AUTHORIZED.
- **No code changes made during this audit.**
- **No Git operations performed during this audit.**
- **STOP and await owner decision on recovery.**