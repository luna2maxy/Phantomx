#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include "phantomx/primitives/transaction.hpp"

namespace phantomx {

struct UtxoEntry {
  std::uint64_t amount_atoms = 0;
  std::vector<std::uint8_t> script_pubkey;
  bool coinbase = false;
  std::uint32_t height = 0;
};

struct OutPointHash {
  std::size_t operator()(const OutPoint& p) const noexcept {
    // FNV-1a over txid bytes + vout
    std::size_t h = 1469598103934665603ULL;
    for (auto b : p.txid) { h ^= b; h *= 1099511628211ULL; }
    for (int i = 0; i < 4; ++i) { h ^= static_cast<std::size_t>((p.vout >> (i * 8)) & 0xFFu); h *= 1099511628211ULL; }
    return h;
  }
};

struct OutPointEq {
  bool operator()(const OutPoint& a, const OutPoint& b) const noexcept {
    return a.vout == b.vout && a.txid == b.txid;
  }
};

class UtxoSet {
 public:
  void add(const OutPoint& outpoint, UtxoEntry entry);
  bool spend(const OutPoint& outpoint);
  [[nodiscard]] bool contains(const OutPoint& outpoint) const;
  [[nodiscard]] std::optional<UtxoEntry> get(const OutPoint& outpoint) const;
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
  [[nodiscard]] const std::unordered_map<OutPoint, UtxoEntry, OutPointHash, OutPointEq>& entries() const noexcept { return entries_; }

 private:
  std::unordered_map<OutPoint, UtxoEntry, OutPointHash, OutPointEq> entries_;
};

}  // namespace phantomx
