#include "phantomx/utxo/utxo.hpp"

namespace phantomx {

void UtxoSet::add(const OutPoint& outpoint, UtxoEntry entry) {
  entries_[outpoint] = std::move(entry);
}

bool UtxoSet::spend(const OutPoint& outpoint) {
  return entries_.erase(outpoint) > 0;
}

bool UtxoSet::contains(const OutPoint& outpoint) const {
  return entries_.find(outpoint) != entries_.end();
}

std::optional<UtxoEntry> UtxoSet::get(const OutPoint& outpoint) const {
  auto it = entries_.find(outpoint);
  if (it == entries_.end()) return std::nullopt;
  return it->second;
}

}  // namespace phantomx
