#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "phantomx/common/error.hpp"
#include "phantomx/primitives/transaction.hpp"
#include "phantomx/utxo/utxo.hpp"
#include "phantomx/utxo/validation.hpp"

namespace phantomx {

struct MempoolEntry {
  Transaction tx;
  Hash256Bytes id{};
  std::uint64_t fee_atoms = 0;
};

class Mempool {
 public:
  [[nodiscard]] Result<void> add(const Transaction& tx, const UtxoSet& utxos, bool verify_signatures = true,
                                 std::uint32_t spend_height = 0xFFFFFFFFu);
  [[nodiscard]] bool contains(const Hash256Bytes& txid_hash) const;
  [[nodiscard]] std::optional<MempoolEntry> get(const Hash256Bytes& txid_hash) const;
  [[nodiscard]] bool spends_outpoint(const OutPoint& outpoint) const;
  void remove(const Hash256Bytes& txid_hash);
  [[nodiscard]] std::size_t size() const noexcept { return txs_.size(); }
  [[nodiscard]] const std::unordered_map<std::string, MempoolEntry>& entries() const noexcept { return txs_; }

 private:
  static std::string key(const Hash256Bytes& id);
  std::unordered_map<std::string, MempoolEntry> txs_;
  std::unordered_set<std::string> spent_outpoints_;
};

}  // namespace phantomx
