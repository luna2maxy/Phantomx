#include "phantomx/primitives/transaction.hpp"

#include "phantomx/common/serialization.hpp"

namespace phantomx {
namespace {
void write_outpoint(BinaryWriter& w, const OutPoint& p) {
  w.write_bytes(std::span<const std::uint8_t>(p.txid.data(), p.txid.size()));
  w.write_u32(p.vout);
}
}  // namespace

std::vector<std::uint8_t> serialize_transaction(const Transaction& tx) {
  BinaryWriter w;
  w.write_u32(tx.version);
  w.write_u8(tx.kind);
  w.write_u32(static_cast<std::uint32_t>(tx.inputs.size()));
  for (const auto& in : tx.inputs) {
    write_outpoint(w, in.prevout);
    w.write_bytes(in.script_sig);
    w.write_u32(in.sequence);
  }
  w.write_u32(static_cast<std::uint32_t>(tx.outputs.size()));
  for (const auto& out : tx.outputs) {
    w.write_u64(out.amount_atoms);
    w.write_bytes(out.script_pubkey);
  }
  w.write_u32(tx.locktime);
  return w.take();
}

Result<Transaction> deserialize_transaction(std::span<const std::uint8_t> bytes) {
  BinaryReader r(bytes);
  Transaction tx;
  auto version = r.read_u32();
  if (!version.ok()) return version.error();
  tx.version = version.value();
  auto kind = r.read_u8();
  if (!kind.ok()) return kind.error();
  tx.kind = kind.value();
  auto in_count = r.read_u32();
  if (!in_count.ok()) return in_count.error();
  tx.inputs.resize(in_count.value());
  for (auto& in : tx.inputs) {
    auto txid_len = r.read_u64();
    if (!txid_len.ok()) return txid_len.error();
    if (txid_len.value() != 32) return MakeError(ErrorCode::kSerializationError, "invalid txid length");
    auto txid_bytes = r.read_bytes(32);
    if (!txid_bytes.ok()) return txid_bytes.error();
    std::copy(txid_bytes.value().begin(), txid_bytes.value().end(), in.prevout.txid.begin());
    auto vout = r.read_u32();
    if (!vout.ok()) return vout.error();
    in.prevout.vout = vout.value();
    auto len = r.read_u64();
    if (!len.ok()) return len.error();
    auto script = r.read_bytes(static_cast<std::size_t>(len.value()));
    if (!script.ok()) return script.error();
    in.script_sig = std::move(script.value());
    auto seq = r.read_u32();
    if (!seq.ok()) return seq.error();
    in.sequence = seq.value();
  }
  auto out_count = r.read_u32();
  if (!out_count.ok()) return out_count.error();
  tx.outputs.resize(out_count.value());
  for (auto& out : tx.outputs) {
    auto amount = r.read_u64();
    if (!amount.ok()) return amount.error();
    out.amount_atoms = amount.value();
    auto len = r.read_u64();
    if (!len.ok()) return len.error();
    auto script = r.read_bytes(static_cast<std::size_t>(len.value()));
    if (!script.ok()) return script.error();
    out.script_pubkey = std::move(script.value());
  }
  auto locktime = r.read_u32();
  if (!locktime.ok()) return locktime.error();
  tx.locktime = locktime.value();
  if (!r.eof()) return MakeError(ErrorCode::kSerializationError, "trailing bytes in transaction");
  return tx;
}

Hash256Bytes txid(const Transaction& tx) {
  const auto bytes = serialize_transaction(tx);
  return sha256d(bytes);
}

}  // namespace phantomx
