#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "phantomx/common/error.hpp"
#include "phantomx/primitives/transaction.hpp"
#include "phantomx/utxo/utxo.hpp"

namespace phantomx {

inline constexpr std::uint64_t kMaxMoneyAtoms = 10'500'000'000'000'000ULL;
inline constexpr std::uint64_t kMinFeeAtoms = 1'000ULL; // foundation minimum relay fee
inline constexpr std::uint32_t kCoinbaseMaturity = 100;     // coinbase outputs spendable after 100 confirmations

struct TxValidationResult {
  bool ok = false;
  Error error;
  std::uint64_t fee_atoms = 0;
};

[[nodiscard]] Hash256Bytes tx_sighash_v1(const Transaction& tx, std::size_t input_index,
                                        const std::vector<std::uint8_t>& script_pubkey);
[[nodiscard]] bool verify_p2pk_ed25519_input(const Transaction& tx, std::size_t input_index,
                                            const std::vector<std::uint8_t>& script_pubkey);
[[nodiscard]] bool validate_address(std::string_view address, std::string_view expected_hrp = "phantomx");
[[nodiscard]] Result<std::vector<std::uint8_t>> script_pubkey_for_address(std::string_view address, std::string_view expected_hrp = "phantomx");
[[nodiscard]] std::vector<std::uint8_t> script_pubkey_for_pubkey(const std::vector<std::uint8_t>& pubkey32);

[[nodiscard]] TxValidationResult validate_transaction_basic(const Transaction& tx);
[[nodiscard]] TxValidationResult validate_transaction(const Transaction& tx, const UtxoSet& utxos,
                                                     bool verify_signatures = true,
                                                     std::uint32_t spend_height = 0xFFFFFFFFu,
                                                     std::uint32_t coinbase_maturity = kCoinbaseMaturity);

}  // namespace phantomx
