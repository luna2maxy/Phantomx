# PhantomX

> Do not add or implement BFT or validator consensus; use PoI + Verifier attestation.

Independent Layer 1 blockchain focused on verifiable internet contribution through Proof of Internet (PoI), independent verification, aggregator certificates, and PoI + Verifier attestation finality.

## Summary

PhantomX (PHX) is a C++20 blockchain implementation with a transparent UTXO ledger, 60-second target blocks, deterministic integer-based tokenomics, and a modular PoI contribution network. Contributors provide measurable internet-service evidence; independent verifiers validate that evidence; aggregators produce certificates; verifier attestations finalize blocks through PoI + Verifier attestation consensus.

## Core Features

- Independent Layer 1 blockchain
- Proof of Internet (PoI) contribution measurement
- Verifier and Aggregator protocol
- PoI + Verifier attestation finality
- Transparent UTXO transaction model
- Native C++20 node and CLI foundation
- Qt desktop wallet foundation
- JSON-RPC and P2P networking
- SQLite-backed chain and wallet data
- Bech32m address format
- PHX token: 8 decimals, zero premine, 105,000,000 PHX maximum theoretical supply
- No storage mining, storage marketplace, bridge, or smart-contract VM in the initial scope

## Architecture

PhantomX separates contribution measurement, verification, aggregation, consensus, settlement, and wallet operations. PoI affects contribution scoring and participation eligibility; it does not independently provide Sybil resistance or determine verifier attestation/quorum policy.

Reward distribution:

- 70% Contributor
- 20% Verifier
- 10% Pool Operator

Initial block reward: 25 PHX. Halving interval: 2,100,000 blocks.

## Build

Requirements: C++20 compiler, CMake 3.20+, libsodium, SQLite3, libcurl, and Jansson.

```bash
cmake -S . -B build -DPHANTOMX_BUILD_TESTS=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

See `docs/BUILDING.md` for platform-specific instructions.

## Project Status

Active development. The repository contains the core blockchain, UTXO, wallet, PoI, verifier, aggregator, consensus, pool, RPC, and networking foundations. Mainnet security claims require completed audits and adversarial testing.

## Documentation

- `docs/ARCHITECTURE.md` — system architecture
- `docs/CONSENSUS.md` — consensus design
- `docs/PROOF_OF_INTERNET.md` — PoI model
- `docs/VERIFIER_PROTOCOL.md` — verifier protocol
- `docs/AGGREGATOR_PROTOCOL.md` — aggregator protocol
- `docs/TOKENOMICS.md` — supply and rewards
- `docs/THREAT_MODEL.md` — threat model
- `docs/ROADMAP.md` — development roadmap

## License

See `SECURITY.md` and repository files for current security and licensing information.
