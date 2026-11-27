#pragma once

#include <cstdint>
#include <vector>

#include "phantomx/common/crypto.hpp"
#include "phantomx/common/error.hpp"
#include "phantomx/primitives/transaction.hpp"
#include "phantomx/utxo/utxo.hpp"

namespace phantomx {

struct BuilderInput {
  OutPoint outpoint;
  UtxoEntry utxo;
  crypto::SecretKey secret_key;
  crypto::PublicKey public_key;
};

struct BuilderOutput {
  std::uint64_t amount_atoms = 0;
  std::vector<std::uint8_t> script_pubkey;
};

class TransactionBuilder {
 public:
  void add_input(BuilderInput in);
  void add_output(BuilderOutput out);
  void set_locktime(std::uint32_t locktime) { locktime_ = locktime; }
  [[nodiscard]] Result<Transaction> build_and_sign() const;

 private:
  std::vector<BuilderInput> inputs_;
  std::vector<BuilderOutput> outputs_;
  std::uint32_t locktime_ = 0;
};

}  // namespace phantomx
