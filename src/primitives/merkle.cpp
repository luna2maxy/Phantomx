#include "phantomx/primitives/merkle.hpp"

namespace phantomx {

Hash256Bytes merkle_root(std::vector<Hash256Bytes> hashes) {
  if (hashes.empty()) return Hash256Bytes{};
  if (hashes.size() == 1) return hashes[0];
  while (hashes.size() > 1) {
    if (hashes.size() % 2 == 1) hashes.push_back(hashes.back());
    std::vector<Hash256Bytes> next;
    next.reserve(hashes.size() / 2);
    for (std::size_t i = 0; i < hashes.size(); i += 2) {
      std::vector<std::uint8_t> buf;
      buf.reserve(64);
      buf.insert(buf.end(), hashes[i].begin(), hashes[i].end());
      buf.insert(buf.end(), hashes[i + 1].begin(), hashes[i + 1].end());
      next.push_back(sha256d(buf));
    }
    hashes = std::move(next);
  }
  return hashes[0];
}

}  // namespace phantomx
