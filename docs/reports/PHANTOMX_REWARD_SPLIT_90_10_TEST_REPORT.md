# PhantomX Reward Split 90/10 — Implementation & Test Evidence

**Date:** 2026-11-27
**Branch:** `encryption`
**Commit:** `196ac2f` — *Change master reward plan to 90% PoI / 10% verifier with no pool category*

## Request

> rubah plan master tanpa adanya pool rewards, verifier menjadi 10% saja dan poi contributor menjadi 90% sehabis mengimplementasi harus ada bukti test suite dan pengetesan

## What changed

| Area | Before | After |
|---|---|---|
| PoI contributor share | 70% | **90%** |
| Verifier share | 20% | **10%** |
| Pool operator subsidy | 10% | **removed** (`kPoolShareBps`, `EpochSnapshot::pool_operators`, `"pool"` reward entries, all pool RPC fields) |
| BPS arithmetic | `(total * bps) / 100000` (wraps above ~9.22e14 atoms) | overflow-safe `mul_div_u64()` / `bps_share()` |
| Config / docs | 70/20/10 | 90/10, no pool category |

Epoch accounting remains exact: verifier receives `floor(total * 10000 / 100000)` and PoI receives `total - verifier_atoms`.

## Build evidence

```text
$ cmake --build build -j$(nproc)
[100%] Built target phantomx_unit_tests
[100%] Built target phantomxd
```

## Test suite — normal build

```text
$ ctest --test-dir build --output-on-failure -j4
Test project /home/ubuntu/Documents/GitHub/Phantomx/build
      Start  4: phantomx.network
      Start  1: phantomx.unit
      Start  7: phantomx.smoke.phantomxd
      Start 10: phantomx.smoke.poi
 1/11 Test  #7: phantomx.smoke.phantomxd .........   Passed    0.01 sec
      Start  8: phantomx.smoke.cli
 2/11 Test #10: phantomx.smoke.poi ...............   Passed    0.01 sec
      Start  5: phantomx.poi.anti_sybil
 3/11 Test  #8: phantomx.smoke.cli ...............   Passed    0.00 sec
      Start 11: phantomx.smoke.pool
 4/11 Test  #5: phantomx.poi.anti_sybil ..........   Passed    0.00 sec
      Start  3: phantomx.integration.wallet
 5/11 Test #11: phantomx.smoke.pool ..............   Passed    0.01 sec
      Start  9: phantomx.smoke.wallet
 6/11 Test  #3: phantomx.integration.wallet ......   Passed    0.01 sec
      Start  2: phantomx.integration
 7/11 Test  #9: phantomx.smoke.wallet ............   Passed    0.00 sec
      Start  6: phantomx.verifier
 8/11 Test  #2: phantomx.integration .............   Passed    0.01 sec
 9/11 Test  #6: phantomx.verifier ................   Passed    0.00 sec
10/11 Test  #1: phantomx.unit ....................   Passed    0.15 sec
11/11 Test  #4: phantomx.network .................   Passed    5.23 sec

100% tests passed, 0 tests failed out of 11

Total Test time (real) =   5.23 sec
```

Unit binary total:

```text
$ ./build/tests/phantomx_unit_tests | tail -1
141 passed, 0 failed, 141 total
```

Reward-specific tests:

```text
RUN: rewards_halving_schedule
PASS: rewards_halving_schedule (0 ms)
RUN: rewards_repeated_halving_to_zero
PASS: rewards_repeated_halving_to_zero (0 ms)
RUN: rewards_max_supply_cap
PASS: rewards_max_supply_cap (0 ms)
RUN: rewards_distribution_90_10_no_pool
PASS: rewards_distribution_90_10_no_pool (0 ms)
RUN: rewards_large_epoch_no_u64_overflow
PASS: rewards_large_epoch_no_u64_overflow (0 ms)
RUN: rewards_rounding_no_atom_loss
PASS: rewards_rounding_no_atom_loss (0 ms)
RUN: rewards_claim_commitment_and_double_claim
PASS: rewards_claim_commitment_and_double_claim (0 ms)
RUN: rewards_zero_subsidy_epoch
PASS: rewards_zero_subsidy_epoch (0 ms)
--
RUN: property_reward_invariant_no_overflow_total
PASS: property_reward_invariant_no_overflow_total (0 ms)
RUN: property_reward_split_large_totals_no_wrap
PASS: property_reward_split_large_totals_no_wrap (0 ms)
```

Notes:
- `rewards_distribution_90_10_no_pool` asserts `poi=2,250,000,000`, `verifier=250,000,000`, `pool=0` for a 2,500,000,000-atom subsidy.
- `rewards_large_epoch_no_u64_overflow` uses `1e15` atoms (above the old wrap threshold) and asserts `poi=900,000,000,000,000`, `verifier=100,000,000,000,000`.
- `property_reward_split_large_totals_no_wrap` randomizes 100 totals in `[1e15, 2e15)` and checks `verifier == total/10`, exact sum, and no wrap.
- `tests/unit/test_property.cpp` was added to the unit-test target; previously it was never compiled or executed.

## Test suite — AddressSanitizer build

```text
$ ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan --output-on-failure -j4
10/11 Test  #1: phantomx.unit ....................   Passed    0.40 sec
11/11 Test  #4: phantomx.network .................   Passed    5.92 sec

100% tests passed, 0 tests failed out of 11

Total Test time (real) =   5.93 sec
```

## Config validation

All five chain configs parse and report the new split:

```text
config/mainnet.json OK {'poi_contributors_percent': 90, 'verifier_operators_percent': 10}
config/regtest.json OK {'poi_contributors_percent': 90, 'verifier_operators_percent': 10}
config/testnet-bft-canonical.json OK {'poi_contributors_percent': 90, 'verifier_operators_percent': 10}
config/testnet-bft-validation.json OK {'poi_contributors_percent': 90, 'verifier_operators_percent': 10}
config/testnet.json OK {'poi_contributors_percent': 90, 'verifier_operators_percent': 10}
```

## End-to-end RPC check (regtest daemon)

Daemon: `phantomxd --regtest --datadir /tmp/opencode/phxreg --rpc-host 127.0.0.1 --rpc-port 19741 --rpc-token testtoken`

```text
== getrewardinfo
{"id":"getrewardinfo","result":{"verifier_percent":"10","status":"foundation","contributor_percent":"90","distribution":"90/10"},"error":null}
== getblockreward
{"id":"getblockreward","result":{"verifier_atoms":"250000000","poi_atoms":"2250000000","subsidy_atoms":"2500000000","height":"0"},"error":null}
```

`getblocktemplate` also reports `contributor_percent=90`, `verifier_percent=10`, and no `pool_*` fields:

```json
{"algorithm":"poi","height":1,"coinbasevalue":2500000000,"poi_required":true,
 "coinbase_split":{"contributor_percent":90,"verifier_percent":10,
 "miner_subsidy_atoms":2500000000,"verifier_subsidy_atoms":0,
 "verifier_address":null,"note":"no verifier registered; full subsidy to miner"}}
```

The daemon was stopped after the check.

## Conclusion

The master plan is now **90% PoI contributor / 10% verifier, with no pool reward category**, and the previously identified 64-bit overflow in basis-point arithmetic is fixed. All normal and ASan tests pass, configs are valid, and live RPC responses confirm the new split.
