# PhantomX Two-VPS Testnet Architecture (DRAFT)

## Constraint
Exactly 2 VPS. Each VPS runs separate services: blockchain node instance(s), POI Pool (pool public exposure BLOCKED until app secured).

## VPS 1 - PHANTOMX-A
- `phantomxd@node-a` (or additional instances if consensus config requires) - datadir `/var/lib/phantomx/node-a`, DB local, P2P 18742, RPC 127.0.0.1:18740.
- `phantomx-pool` - **DISABLED until implemented/secured**.
- node_exporter/healthchecks.

## VPS 2 - PHANTOMX-B
- `phantomxd@node-b` - datadir `/var/lib/phantomx/node-b`.

- POI Pool instance **DISABLED until implemented/secured**.
- Monitoring.

## Port matrix (intended)

| Port | Service | Exposure |
|---:|---|---|
| 22/tcp | SSH | admin IPs only, key auth |
| 80/tcp | HTTP redirect | public |

| 443/tcp | Pool HTTPS | public only after security gate |
| 18742/tcp | P2P | peer VPS/public as protocol requires, prefer peer-restricted |
| 18740/tcp | RPC admin | localhost/private only, firewall deny public |

| 8751/tcp | Pool API | localhost only behind nginx; disabled by default |

## Consensus limitation
Canonical `config/testnet.json` has no BFT section. `config/testnet-bft-validation.json` has 4 validators but is ISOLATED TEST-ONLY and not canonical/public. Two physical VPS = two failure domains. If 4 BFT validators are distributed 2+2, validator independence and BFT fault tolerance cannot be claimed. Do not reduce quorum or change finality rules.
