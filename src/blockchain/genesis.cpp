#include "phantomx/blockchain/genesis.hpp"

#include <string>

#include "phantomx/primitives/merkle.hpp"

namespace phantomx {
namespace {
Transaction make_genesis_coinbase(Network network) {
  Transaction tx;
  tx.version = kTxVersionV1;
  tx.kind = kTxKindCoinbase;
  TxIn in;
  in.prevout.txid = Hash256Bytes{};
  in.prevout.vout = 0xFFFFFFFFu;
  const std::string msg = std::string("PhantomX Genesis ") + std::string(network_name(network));
  in.script_sig.assign(msg.begin(), msg.end());
  in.sequence = 0xFFFFFFFFu;
  tx.inputs.push_back(in);
  TxOut out;
  out.amount_atoms = 0;  // premine = 0
  const std::string script = "OP_RETURN PHANTOMX_GENESIS";
  out.script_pubkey.assign(script.begin(), script.end());
  tx.outputs.push_back(out);
  tx.locktime = 0;
  return tx;
}
}  // namespace

std::string_view network_name(Network network) {
  switch (network) {
    case Network::kMainnet: return "mainnet";
    case Network::kTestnet: return "testnet";
    case Network::kRegtest: return "regtest";
  }
  return "unknown";
}

std::uint32_t network_magic(Network network) {
  switch (network) {
    case Network::kMainnet: return 0x50485801u; // "PHX\x01"
    case Network::kTestnet: return 0x54505801u; // "TPX\x01"
    case Network::kRegtest: return 0x52505801u; // "RPX\x01"
  }
  return 0;
}

std::string_view address_hrp(Network network) {
  switch (network) {
    case Network::kMainnet: return "phantomx";
    case Network::kTestnet: return "tphx";
    case Network::kRegtest: return "rphx";
  }
  return "phantomx";
}

Block genesis_block(Network network) {
  Block b;
  b.header.version = kBlockVersionV1;
  b.header.prev_hash = Hash256Bytes{};
  b.header.timestamp = (network == Network::kMainnet) ? 1767225600ULL : (network == Network::kTestnet ? 1767225601ULL : 1767225602ULL);
  b.header.height = 0;
  b.header.nonce = 0;
  b.header.bits = 0x1d00ffffu;
  b.transactions.push_back(make_genesis_coinbase(network));
  std::vector<Hash256Bytes> txids;
  txids.reserve(b.transactions.size());
  for (const auto& tx : b.transactions) txids.push_back(txid(tx));
  b.header.merkle_root = merkle_root(std::move(txids));
  return b;
}

}  // namespace phantomx
