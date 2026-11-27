#include "phantomx/utxo/validation.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_set>

#include "phantomx/common/crypto.hpp"
#include "phantomx/primitives/bech32m.hpp"

namespace phantomx {
namespace {
bool add_u64_checked(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
  if (b > UINT64_MAX - a) return false;
  out = a + b;
  return true;
}

struct OutPointHashLocal {
  std::size_t operator()(const OutPoint& p) const noexcept { return OutPointHash{}(p); }
};
struct OutPointEqLocal {
  bool operator()(const OutPoint& a, const OutPoint& b) const noexcept { return OutPointEq{}(a, b); }
};
}  // namespace

Hash256Bytes tx_sighash_v1(const Transaction& tx, std::size_t input_index,
                           const std::vector<std::uint8_t>& script_pubkey) {
  Transaction tmp = tx;
  for (auto& in : tmp.inputs) in.script_sig.clear();
  if (input_index < tmp.inputs.size()) tmp.inputs[input_index].script_sig = script_pubkey;
  auto bytes = serialize_transaction(tmp);
  const std::string domain = "PHX/sighash/v1";
  std::vector<std::uint8_t> preimage(domain.begin(), domain.end());
  preimage.insert(preimage.end(), bytes.begin(), bytes.end());
  return sha256d(preimage);
}

bool verify_p2pk_ed25519_input(const Transaction& tx, std::size_t input_index,
                               const std::vector<std::uint8_t>& script_pubkey) {
  if (input_index >= tx.inputs.size()) return false;
  // script_pubkey: 0x01 || 32-byte sha256(pubkey)
  if (script_pubkey.size() != 34 || script_pubkey[0] != 0x01) return false;
  const auto& script_sig = tx.inputs[input_index].script_sig;
  // script_sig: 64-byte signature || 32-byte pubkey
  if (script_sig.size() != 96) return false;
  std::vector<std::uint8_t> pub(script_sig.begin() + 64, script_sig.end());
  auto pk_hash = sha256(std::span<const std::uint8_t>(pub.data(), pub.size()));
  if (!std::equal(pk_hash.begin(), pk_hash.end(), script_pubkey.begin() + 2)) return false;

  crypto::Signature sig{};
  std::copy(script_sig.begin(), script_sig.begin() + 64, sig.bytes.begin());
  crypto::PublicKey pk{};
  std::copy(pub.begin(), pub.end(), pk.bytes.begin());
  const auto sighash = tx_sighash_v1(tx, input_index, script_pubkey);
  return crypto::verify(std::span<const std::uint8_t>(sighash.data(), sighash.size()), sig, pk);
}

bool validate_address(std::string_view address, std::string_view expected_hrp) {
  auto decoded = decode_phantomx_address(expected_hrp, address);
  return decoded.ok();
}

std::vector<std::uint8_t> script_pubkey_for_pubkey(const std::vector<std::uint8_t>& pubkey32) {
  std::vector<std::uint8_t> script;
  script.reserve(34);
  script.push_back(0x01); // P2PK-Ed25519 v1
  const auto h = sha256(std::span<const std::uint8_t>(pubkey32.data(), pubkey32.size()));
  script.push_back(0x20);
  script.insert(script.end(), h.begin(), h.end());
  return script;
}

Result<std::vector<std::uint8_t>> script_pubkey_for_address(std::string_view address, std::string_view expected_hrp) {
  auto decoded = decode_phantomx_address(expected_hrp, address);
  if (!decoded.ok()) return decoded.error();
  const auto& payload = decoded.value().second;
  if (payload.size() != 32) return MakeError(ErrorCode::kInvalidArgument, "unsupported address payload size");
  // Address payload is a 32-byte pubkey; P2PK-Ed25519 v1 script commits to sha256(pubkey).
  std::vector<std::uint8_t> script;
  script.reserve(34);
  script.push_back(0x01);
  const auto h = sha256(std::span<const std::uint8_t>(payload.data(), payload.size()));
  script.push_back(0x20);
  script.insert(script.end(), h.begin(), h.end());
  return script;
}

TxValidationResult validate_transaction_basic(const Transaction& tx) {
  if (tx.version != kTxVersionV1) return {false, MakeError(ErrorCode::kInvalidArgument, "unsupported tx version"), 0};
  if (tx.kind != kTxKindStandard && tx.kind != kTxKindCoinbase) return {false, MakeError(ErrorCode::kInvalidArgument, "unsupported tx kind"), 0};
  if (tx.inputs.empty()) return {false, MakeError(ErrorCode::kInvalidArgument, "tx must have inputs"), 0};
  if (tx.outputs.empty()) return {false, MakeError(ErrorCode::kInvalidArgument, "tx must have outputs"), 0};

  std::unordered_set<OutPoint, OutPointHashLocal, OutPointEqLocal> seen;
  for (const auto& in : tx.inputs) {
    if (tx.kind != kTxKindCoinbase) {
      bool all_zero = std::all_of(in.prevout.txid.begin(), in.prevout.txid.end(), [](std::uint8_t b){return b==0;});
      if (all_zero) return {false, MakeError(ErrorCode::kInvalidArgument, "null prevout in non-coinbase tx"), 0};
    }
    auto [_, inserted] = seen.insert(in.prevout);
    if (!inserted) return {false, MakeError(ErrorCode::kInvalidArgument, "duplicate input"), 0};
  }

  std::uint64_t total_out = 0;
  for (const auto& out : tx.outputs) {
    if (out.amount_atoms == 0 && tx.kind != kTxKindCoinbase) {
      return {false, MakeError(ErrorCode::kInvalidArgument, "output amount must be > 0"), 0};
    }
    if (out.amount_atoms > kMaxMoneyAtoms) return {false, MakeError(ErrorCode::kInvalidArgument, "output exceeds max money"), 0};
    if (out.script_pubkey.empty()) return {false, MakeError(ErrorCode::kInvalidArgument, "empty script_pubkey"), 0};
    if (!add_u64_checked(total_out, out.amount_atoms, total_out)) {
      return {false, MakeError(ErrorCode::kInvalidArgument, "output amount overflow"), 0};
    }
  }
  if (total_out > kMaxMoneyAtoms) return {false, MakeError(ErrorCode::kInvalidArgument, "total output exceeds max money"), 0};
  return {true, {}, 0};
}

TxValidationResult validate_transaction(const Transaction& tx, const UtxoSet& utxos, bool verify_signatures,
                                       std::uint32_t spend_height, std::uint32_t coinbase_maturity) {
  auto basic = validate_transaction_basic(tx);
  if (!basic.ok) return basic;
  if (tx.kind == kTxKindCoinbase) return {true, {}, 0};

  std::uint64_t total_in = 0;
  for (std::size_t i = 0; i < tx.inputs.size(); ++i) {
    const auto& in = tx.inputs[i];
    auto utxo = utxos.get(in.prevout);
    if (!utxo.has_value()) return {false, MakeError(ErrorCode::kNotFound, "input UTXO not found"), 0};
    if (utxo->coinbase) {
      if (spend_height == 0xFFFFFFFFu) {
        return {false, MakeError(ErrorCode::kInvalidArgument, "coinbase maturity context required"), 0};
      }
      const std::uint64_t mature_at = static_cast<std::uint64_t>(utxo->height) + coinbase_maturity;
      if (static_cast<std::uint64_t>(spend_height) < mature_at) {
        return {false, MakeError(ErrorCode::kInvalidArgument, "coinbase immature"), 0};
      }
    }
    if (utxo->amount_atoms == 0 || utxo->amount_atoms > kMaxMoneyAtoms) {
      return {false, MakeError(ErrorCode::kInvalidArgument, "invalid UTXO amount"), 0};
    }
    if (!add_u64_checked(total_in, utxo->amount_atoms, total_in)) {
      return {false, MakeError(ErrorCode::kInvalidArgument, "input amount overflow"), 0};
    }
    if (verify_signatures) {
      if (!verify_p2pk_ed25519_input(tx, i, utxo->script_pubkey)) {
        return {false, MakeError(ErrorCode::kInvalidArgument, "signature verification failed"), 0};
      }
    }
  }

  std::uint64_t total_out = 0;
  for (const auto& out : tx.outputs) {
    if (!add_u64_checked(total_out, out.amount_atoms, total_out)) {
      return {false, MakeError(ErrorCode::kInvalidArgument, "output amount overflow"), 0};
    }
  }
  if (total_in < total_out) return {false, MakeError(ErrorCode::kInvalidArgument, "insufficient input amount"), 0};
  const std::uint64_t fee = total_in - total_out;
  if (fee < kMinFeeAtoms) return {false, MakeError(ErrorCode::kInvalidArgument, "fee below minimum"), 0};
  return {true, {}, fee};
}

}  // namespace phantomx
