# PhantomX Architecture Diagram

> **NOTE (PoI+Verifier removed):** PhantomX consensus/finality has moved to **PoI + Verifier attestation**. Any remaining PoI+Verifier/verifier wording in this document is historical and must not be used to reintroduce PoI+Verifier.

This document contains PhantomX architecture diagrams based on the implementation in `include/phantomx/`, `src/`, and `docs/`.

- **Diagram source:** Mermaid diagram below (image assets not included in this directory).
- **Status:** foundation/prototype, not production-ready.
- **Legend:**
  - Solid line = implemented flow.
  - Dashed red line = integration gap / not yet connected to chain.
  - Dashed amber line = PoI signal to verifier eligibility.
  - Dashed teal line = storage settlement flow (separate from block subsidy).

---

## 1. Layered architecture

Mermaid version:

```mermaid
flowchart TB
    subgraph Clients["Clients"]
        CLI["Native CLI"]
        QT["Qt Desktop"]
        MOB["Mobile Wallet"]
    end

    subgraph Interfaces["Interfaces"]
        RPC["RPC Server<br/>auth · rate limit · audit"]
        P2P["P2P Node<br/>handshake · inv/getdata · sync"]
    end

    subgraph Core["Blockchain Core"]
        PRIM["Primitives<br/>hash · merkle · tx · block"]
        UTXO["UTXO Set & Validation"]
        MEM["Mempool & Tx Builder"]
        CHAIN["ChainDatabase<br/>validate · apply · disconnect"]
        GEN["Genesis & Networks"]
        REW["Rewards & Claims"]
        BFT["BFT/Verifier Consensus<br/>votes · QC · finality"]
        WALLET["Wallet & Keystore"]
    end

    subgraph Contrib["Contribution & Storage"]
        POI["PoI Subsystem<br/>challenge · evidence · score"]
        STOR["Storage Subsystem<br/>chunk · manifest · proof · lease"]
    end

    subgraph Persist["Persistence & State"]
        SQL[("SQLite Chain DB")]
        OBJ[("Encrypted Chunk Store")]
    end

    CLI --> RPC
    QT --> RPC
    MOB --> RPC
    CLI --> P2P
    QT --> P2P
    MOB --> P2P

    RPC --> UTXO
    RPC --> MEM
    RPC --> WALLET
    P2P --> CHAIN
    P2P --> MEM

    PRIM --> UTXO
    UTXO --> CHAIN
    MEM --> UTXO
    PRIM --> GEN
    GEN --> CHAIN
    WALLET --> MEM
    CHAIN --> SQL

    POI -.->|"score / eligibility"| BFT
    STOR -.->|"storage fees"| REW
    BFT -.->|"QC / finality (not yet connected)"| CHAIN
    REW -.->|"reward commitment (not yet on-chain)"| CHAIN
    STOR --> OBJ
```

---

## 2. Block and transaction data structure

```mermaid
classDiagram
    class BlockHeader {
        +uint32 version
        +Hash256 prev_hash
        +Hash256 merkle_root
        +uint64 timestamp
        +uint32 height
        +uint32 nonce
        +uint32 bits
    }

    class Block {
        +BlockHeader header
        +Transaction[] transactions
    }

    class Transaction {
        +uint32 version
        +uint8 kind
        +TxIn[] inputs
        +TxOut[] outputs
        +uint32 locktime
    }

    class TxIn {
        +OutPoint prevout
        +bytes script_sig
        +uint32 sequence
    }

    class TxOut {
        +uint64 amount_atoms
        +bytes script_pubkey
    }

    class OutPoint {
        +Hash256 txid
        +uint32 vout
    }

    Block *-- BlockHeader
    Block *-- "1..*" Transaction
    Transaction *-- "0..*" TxIn
    Transaction *-- "1..*" TxOut
    TxIn *-- OutPoint
```

### Block layout v1

```text
Block
├── header (104 bytes)
│   ├── version        u32
│   ├── prev_hash      hash256 (length-prefixed)
│   ├── merkle_root    hash256 (length-prefixed)
│   ├── timestamp      u64
│   ├── height         u32
│   ├── nonce          u32
│   └── bits           u32
└── transactions
    ├── tx_count       u32
    └── tx[0..n]       length-prefixed serialized transactions
```

Hash rules:

```text
txid        = SHA-256d(serialize_transaction(tx))
block_hash  = SHA-256d(serialize_block_header(header))
merkle_root = SHA-256d(left || right), odd node duplicated
```

---

## 3. Block lifecycle

```mermaid
sequenceDiagram
    autonumber
    participant P2P as P2P Node
    participant Chain as ChainDatabase
    participant DB as SQLite
    participant BFT as BftEngine

    P2P->>Chain: write_block(block)
    Chain->>DB: INSERT blocks (idempotent by hash)

    P2P->>Chain: apply_block(block)
    Chain->>Chain: validate_block(block)

    alt valid
        Chain->>DB: BEGIN
        Chain->>DB: mark block active
        Chain->>DB: DELETE spent utxos
        Chain->>DB: INSERT new utxos
        Chain->>DB: INSERT tx_index
        Chain->>DB: INSERT undo data
        Chain->>DB: UPDATE metadata tip_hash / tip_height
        Chain->>DB: COMMIT
    else invalid
        Chain-->>P2P: error
    end

    Note over BFT,Chain: Finality certificate is not yet forwarded to ChainDatabase.
    BFT-->>Chain: QC / finality (not yet connected)
```

### Validation and apply flow

```mermaid
flowchart TD
    A["Receive Block"] --> B{"transactions empty?"}
    B -- yes --> X["Reject"]
    B -- no --> C{"tx[0] coinbase?"}
    C -- no --> X
    C -- yes --> D["validate_transaction_basic()"]
    D --> E["Compute txid for all transactions"]
    E --> F["Compute merkle root"]
    F --> G{"merkle root == header?"}
    G -- no --> X
    G -- yes --> H["Validate non-coinbase tx against UTXO set"]
    H --> I{"parent == tip && height == tip+1?"}
    I -- no --> X
    I -- yes --> J["build_undo()"]
    J --> K["SQLite transaction"]
    K --> L["Update blocks / utxos / tx_index / undo / tip"]
    L --> M["Commit"]
    M --> N["Block applied"]

    X --> O["Block rejected"]
```

---

## 4. PoI + Verifier attestation consensus

```mermaid
sequenceDiagram
    autonumber
    participant P as Proposer
    participant V1 as Verifier A
    participant V2 as Verifier B
    participant V3 as Verifier C
    participant E as BftEngine
    participant C as ChainDatabase

    P->>V1: BlockProposal(epoch, view, height, hash)
    P->>V2: BlockProposal
    P->>V3: BlockProposal

    V1->>E: Vote(prevote, hash)
    V2->>E: Vote(prevote, hash)
    V3->>E: Vote(prevote, hash)

    E->>E: build_qc(prevotes)
    Note over E: Quorum: signed_power * 3 > total_power * 2

    V1->>E: Vote(precommit, hash)
    V2->>E: Vote(precommit, hash)
    V3->>E: Vote(precommit, hash)

    E->>E: build_qc(precommits)
    E->>E: FinalityCertificate(precommit QC)

    E-->>C: QC / finality (not yet wired)
    Note over E,C: Gap: apply_block does not yet verify QC/finality.
```

### Key rules

```text
eligible verifier = identity_verified && !slashed && poi_score >= min_poi_score
voting_power       = clamp(poi_score, caps)
quorum             = floor(2 * total_power / 3) + 1
proposer           = weighted_select(verifiers, SHA-256d(epoch || view || pubkeys))
finality           = precommit QC
```

### PoI scoring

```mermaid
flowchart LR
    BW["Bandwidth 30%"] --> SCORE["ScoreMilli<br/>0..100000"]
    UP["Uptime 25%"] --> SCORE
    LAT["Latency 20%"] --> SCORE
    REL["Relay 25%"] --> SCORE
    SCORE --> ELIG["Verifier eligibility"]
    SCORE --> REWARD["Reward contribution"]
    SCORE -.->|"not Sybil resistance"| NOTE["Requires verifier attestation + identity + policy"]
```

---

## 5. P2P network

```mermaid
sequenceDiagram
    autonumber
    participant A as Node A
    participant B as Node B

    A->>B: HELLO(magic, version, nonce, genesis_hash)
    B->>A: HELLO_ACK(magic, version, nonce, genesis_hash)

    alt magic / genesis / version match
        A->>B: GET_ADDR
        B->>A: ADDR(peer list)
        A->>B: GET_HEADERS(tip)
        B->>A: HEADERS(hash list)
        A->>B: GET_DATA(block hashes)
        B->>A: BLOCK(serialized block)
        A->>A: write_block + apply_block if next tip
    else mismatch
        B-->>A: reject / ban
    end

    A->>B: INV(tx)
    B->>A: GET_DATA(tx)
    A->>B: TX(serialized tx)
    B->>B: validate + mempool.add()
```

### P2P frame

```text
network_magic(u32) | type(u8) | reserved(u8) | payload_len(u32) | checksum(u32) | payload
checksum = first 4 bytes of SHA-256d(payload)
max payload = 2 MiB
```

---

## 6. Chainstate database

```mermaid
erDiagram
    METADATA {
        text key PK
        text value
    }

    BLOCKS {
        text hash PK
        integer height
        text prev_hash
        integer timestamp
        blob header_blob
        blob block_blob
        integer active
    }

    UTXOS {
        text txid PK
        integer vout PK
        integer amount
        integer coinbase
        integer height
        blob script_pubkey
    }

    TX_INDEX {
        text txid PK
        text block_hash
        integer tx_index
    }

    UNDO {
        text block_hash PK
        blob undo_blob
    }

    SNAPSHOTS {
        text id PK
        text tip_hash
        integer tip_height
        integer created_at
        text path
    }

    BLOCKS ||--o{ TX_INDEX : "contains"
    BLOCKS ||--|| UNDO : "has undo"
    BLOCKS ||--o{ UTXOS : "creates / spends"
    BLOCKS ||--o{ SNAPSHOTS : "snapshot tip"
```

Notes:
- `metadata` stores `tip_hash`, `tip_height`, `network`, and `finalized_height`.
- `blocks.active` marks blocks on the active chain.
- `undo` is used for `disconnect_block`.
- UTXO state is **not** committed to the block header.

---

## 7. Component integration status

```mermaid
flowchart LR
    subgraph Done["Implemented"]
        PRIM["Primitives"]
        UTXO["UTXO validation"]
        CHAIN["ChainDatabase"]
        DB[("SQLite")]
        P2P["P2P foundation"]
        BFT["BFT engine"]
        REW["Rewards"]
        POI["PoI scoring"]
        STOR["Storage foundation"]
    end

    subgraph Gap["Not yet connected to state transition"]
        G1["QC / finality certificate"]
        G2["Reward commitment"]
        G3["PoI commitment"]
        G4["Storage settlement"]
        G5["Fork choice / reorg"]
        G6["PoW / difficulty"]
    end

    PRIM --> UTXO --> CHAIN --> DB
    P2P --> CHAIN
    BFT -.-> G1
    REW -.-> G2
    POI -.-> G3
    STOR -.-> G4
    G1 -.-> CHAIN
    G2 -.-> CHAIN
    G3 -.-> CHAIN
    G4 -.-> REW

    classDef gap fill:#fef2f2,stroke:#dc2626,stroke-width:2px,stroke-dasharray:5 5,color:#991b1b;
    class G1,G2,G3,G4,G5,G6 gap;
```

---

## 8. Main flow summary

```text
Client
  └── RPC / P2P
        ├── Transaction
        │     └── UTXO validation → Mempool
        └── Block
              └── ChainDatabase.validate_block()
                    ├── merkle root check
                    ├── UTXO contextual check
                    └── apply_block()
                          ├── spend inputs
                          ├── create outputs
                          ├── write tx index
                          ├── write undo
                          └── advance tip

Consensus
  └── PoI score → verifier eligibility → BFT votes → QC → finality certificate
        └── (not yet forwarded to ChainDatabase)

Storage
  └── chunk + encrypt → manifest → replication → lease → proof → settlement
        └── (not yet fully on-chain)
```

---

## 9. Main source files

| Area | File |
|---|---|
| Block & header | `include/phantomx/primitives/block.hpp`, `src/primitives/block.cpp` |
| Transaction | `include/phantomx/primitives/transaction.hpp`, `src/primitives/transaction.cpp` |
| Hash & Merkle | `src/primitives/hash.cpp`, `src/primitives/merkle.cpp` |
| UTXO & validation | `include/phantomx/utxo/utxo.hpp`, `src/utxo/validation.cpp` |
| Mempool & tx builder | `include/phantomx/utxo/mempool.hpp`, `src/utxo/tx_builder.cpp` |
| Chain database | `include/phantomx/blockchain/chain.hpp`, `src/blockchain/chain.cpp` |
| Genesis | `src/blockchain/genesis.cpp`, `config/*.json` |
| Consensus/BFT | `include/phantomx/consensus/bft.hpp`, `src/consensus/bft.cpp` |
| PoI | `include/phantomx/poi/`, `src/poi/` |
| Storage | `include/phantomx/storage/`, `src/storage/` |
| Rewards | `include/phantomx/rewards/rewards.hpp`, `src/rewards/rewards.cpp` |
| P2P | `include/phantomx/network/`, `src/network/` |
| Database | `include/phantomx/database/sqlite_db.hpp`, `src/database/sqlite_db.cpp` |

---

## 10. Visual conclusion

PhantomX architecture consists of five layers:

1. **Clients** — CLI, desktop, mobile.
2. **Interfaces** — RPC, P2P.
3. **Blockchain Core** — primitives, UTXO, chain database, BFT/verifier consensus, rewards, wallet.
4. **Contribution & Storage** — PoI and storage network.
5. **Persistence** — SQLite chain DB, encrypted chunk store.

The biggest current gap is that **BFT finality is not yet bound to `apply_block`**, so the diagram shows a dashed red line from BFT/consensus to ChainDatabase.
