#pragma once

#include <array>
#include <functional>

#include "phantomx/aggregator/aggregator.hpp"
#include "phantomx/blockchain/chain.hpp"
#include "phantomx/blockchain/genesis.hpp"
#include "phantomx/consensus/bft.hpp"
#include "phantomx/common/crypto.hpp"
#include "phantomx/network/node.hpp"
#include "phantomx/pool/pool.hpp"
#include "phantomx/rpc/rpc.hpp"
#include "phantomx/utxo/mempool.hpp"

namespace phantomx::rpc {

// Registers the real node/blockchain/wallet RPC handlers on `server`.
// Node handlers operate directly on the local ChainDatabase, mempool, pool ledger, and aggregation engine.
[[nodiscard]] Result<void> register_node_rpc_handlers(RpcServer& server,
                                                       blockchain::ChainDatabase& chain,
                                                       phantomx::Mempool& mempool,
                                                       phantomx::pool::PoolLedger& pool,
                                                       phantomx::aggregator::AggregationEngine& aggregator,
                                                       std::string aggregator_state_path = "",
                                                       std::string aggregator_db_path = "",
                                                       network::P2PNode* p2p = nullptr,
                                                       consensus::BftEngine* bft = nullptr,
                                                       const crypto::KeyPair* local_bft_key = nullptr,
                                                       std::function<void(const consensus::BlockProposal&)> on_local_proposal = nullptr,
                                                       std::function<std::array<std::uint8_t, 32>(std::uint64_t, std::uint64_t)> proposer_selector = nullptr,
                                                       phantomx::Network network = phantomx::Network::kRegtest);

}  // namespace phantomx::rpc
