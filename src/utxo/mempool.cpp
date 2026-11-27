#include "phantomx/utxo/mempool.hpp"

#include "phantomx/primitives/hash.hpp"

namespace phantomx {
namespace {
std::string outpoint_key(const OutPoint& p) {
  return to_hex(std::span<const std::uint8_t>(p.txid.data(), p.txid.size())) + ":" + std::to_string(p.vout);
}
}  // namespace

std::string Mempool::key(const Hash256Bytes& id) {
  return to_hex(std::span<const std::uint8_t>(id.data(), id.size()));
}

Result<void> Mempool::add(const Transaction& tx, const UtxoSet& utxos, bool verify_signatures,
                           std::uint32_t spend_height) {
  auto v = validate_transaction(tx, utxos, verify_signatures, spend_height);
  if (!v.ok) return v.error;
  const auto id = txid(tx);
  const auto k = key(id);
  if (txs_.find(k) != txs_.end()) return MakeError(ErrorCode::kInvalidArgument, "tx already in mempool");
  for (const auto& in : tx.inputs) {
    if (spent_outpoints_.count(outpoint_key(in.prevout)) > 0) {
      return MakeError(ErrorCode::kInvalidArgument, "mempool double-spend/conflict");
    }
  }
  for (const auto& in : tx.inputs) spent_outpoints_.insert(outpoint_key(in.prevout));
  txs_[k] = MempoolEntry{tx, id, v.fee_atoms};
  return Result<void>{};
}

bool Mempool::contains(const Hash256Bytes& txid_hash) const {
  return txs_.find(key(txid_hash)) != txs_.end();
}

std::optional<MempoolEntry> Mempool::get(const Hash256Bytes& txid_hash) const {
  auto it = txs_.find(key(txid_hash));
  if (it == txs_.end()) return std::nullopt;
  return it->second;
}

bool Mempool::spends_outpoint(const OutPoint& outpoint) const {
  return spent_outpoints_.count(outpoint_key(outpoint)) > 0;
}

void Mempool::remove(const Hash256Bytes& txid_hash) {
  auto it = txs_.find(key(txid_hash));
  if (it == txs_.end()) return;
  for (const auto& in : it->second.tx.inputs) spent_outpoints_.erase(outpoint_key(in.prevout));
  txs_.erase(it);
}

}  // namespace phantomx
