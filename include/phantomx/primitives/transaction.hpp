#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "phantomx/common/error.hpp"
#include "phantomx/primitives/hash.hpp"

namespace phantomx {

inline constexpr std::uint32_t kTxVersionV1 = 1;
inline constexpr std::uint8_t kTxKindStandard = 0;
inline constexpr std::uint8_t kTxKindCoinbase = 1;

struct OutPoint {
  Hash256Bytes txid{};
  std::uint32_t vout = 0;
};

struct TxIn {
  OutPoint prevout;
  std::vector<std::uint8_t> script_sig;
  std::uint32_t sequence = 0xFFFFFFFFu;
};

struct TxOut {
  std::uint64_t amount_atoms = 0;
  std::vector<std::uint8_t> script_pubkey;
};

struct Transaction {
  std::uint32_t version = kTxVersionV1;
  std::uint8_t kind = kTxKindStandard;
  std::vector<TxIn> inputs;
  std::vector<TxOut> outputs;
  std::uint32_t locktime = 0;
};

[[nodiscard]] std::vector<std::uint8_t> serialize_transaction(const Transaction& tx);
[[nodiscard]] Result<Transaction> deserialize_transaction(std::span<const std::uint8_t> bytes);
[[nodiscard]] Hash256Bytes txid(const Transaction& tx);

}  // namespace phantomx
