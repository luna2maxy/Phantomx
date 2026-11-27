#include "phantomx/utxo/tx_builder.hpp"

#include <cstdint>

#include "phantomx/utxo/validation.hpp"

namespace phantomx {

void TransactionBuilder::add_input(BuilderInput in) { inputs_.push_back(std::move(in)); }
void TransactionBuilder::add_output(BuilderOutput out) { outputs_.push_back(std::move(out)); }

Result<Transaction> TransactionBuilder::build_and_sign() const {
  if (inputs_.empty()) return MakeError(ErrorCode::kInvalidArgument, "builder requires at least one input");
  if (outputs_.empty()) return MakeError(ErrorCode::kInvalidArgument, "builder requires at least one output");

  Transaction tx;
  tx.version = kTxVersionV1;
  tx.kind = kTxKindStandard;
  tx.locktime = locktime_;
  tx.inputs.reserve(inputs_.size());
  for (const auto& in : inputs_) {
    TxIn txin;
    txin.prevout = in.outpoint;
    txin.sequence = 0xFFFFFFFFu;
    tx.inputs.push_back(std::move(txin));
  }
  tx.outputs.reserve(outputs_.size());
  std::uint64_t total_out = 0;
  for (const auto& out : outputs_) {
    if (out.amount_atoms == 0) return MakeError(ErrorCode::kInvalidArgument, "output amount must be > 0");
    if (out.amount_atoms > kMaxMoneyAtoms) return MakeError(ErrorCode::kInvalidArgument, "output exceeds max money");
    if (UINT64_MAX - total_out < out.amount_atoms) return MakeError(ErrorCode::kInvalidArgument, "output overflow");
    total_out += out.amount_atoms;
    TxOut txout;
    txout.amount_atoms = out.amount_atoms;
    txout.script_pubkey = out.script_pubkey;
    tx.outputs.push_back(std::move(txout));
  }

  std::uint64_t total_in = 0;
  for (const auto& in : inputs_) {
    if (UINT64_MAX - total_in < in.utxo.amount_atoms) return MakeError(ErrorCode::kInvalidArgument, "input overflow");
    total_in += in.utxo.amount_atoms;
  }
  if (total_in < total_out) return MakeError(ErrorCode::kInvalidArgument, "insufficient input amount");
  if (total_in - total_out < kMinFeeAtoms) return MakeError(ErrorCode::kInvalidArgument, "fee below minimum");

  // Sign inputs
  for (std::size_t i = 0; i < inputs_.size(); ++i) {
    const auto& in = inputs_[i];
    const auto sighash = tx_sighash_v1(tx, i, in.utxo.script_pubkey);
    auto sig = crypto::sign(std::span<const std::uint8_t>(sighash.data(), sighash.size()), in.secret_key);
    if (!sig.ok()) return sig.error();
    std::vector<std::uint8_t> script_sig;
    script_sig.reserve(96);
    script_sig.insert(script_sig.end(), sig.value().bytes.begin(), sig.value().bytes.end());
    script_sig.insert(script_sig.end(), in.public_key.bytes.begin(), in.public_key.bytes.end());
    tx.inputs[i].script_sig = std::move(script_sig);
  }

  return tx;
}

}  // namespace phantomx
