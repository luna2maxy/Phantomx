#include "phantomx/consensus/bft.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <map>
#include <string>
#include <system_error>
#include <unistd.h>
#include <fcntl.h>

#include "phantomx/common/serialization.hpp"
#include "phantomx/primitives/hash.hpp"

namespace phantomx::consensus {
namespace {
constexpr std::uint64_t kMaxViewJump = 16;

std::string pub_hex(const std::array<std::uint8_t, 32>& pk) {
  return to_hex(std::span<const std::uint8_t>(pk.data(), pk.size()));
}
}  // namespace

std::uint64_t quorum_power(const ValidatorSet& vs) {
  // > 2/3 of total voting power
  return (vs.total_voting_power * 2 / 3) + 1;
}

bool has_quorum(const ValidatorSet& vs, std::uint64_t signed_power) {
  return signed_power * 3 > vs.total_voting_power * 2;
}

const Validator* find_validator(const ValidatorSet& vs, const std::array<std::uint8_t, 32>& pubkey) {
  for (const auto& v : vs.validators) if (v.validator_pubkey == pubkey) return &v;
  return nullptr;
}

std::array<std::uint8_t, 32> select_proposer(const ValidatorSet& vs, std::uint64_t epoch, std::uint64_t view) {
  std::array<std::uint8_t, 32> empty{};
  if (vs.validators.empty() || vs.total_voting_power == 0) return empty;
  BinaryWriter w;
  w.write_u64(epoch);
  w.write_u64(view);
  std::vector<const Validator*> ordered;
  ordered.reserve(vs.validators.size());
  for (const auto& v : vs.validators) ordered.push_back(&v);
  std::sort(ordered.begin(), ordered.end(), [](const Validator* a, const Validator* b) {
    return a->validator_pubkey < b->validator_pubkey;
  });
  for (const auto* v : ordered) w.write_bytes(std::span<const std::uint8_t>(v->validator_pubkey.data(), v->validator_pubkey.size()));
  auto h = sha256d(w.take());
  std::uint64_t pick = 0;
  for (int i = 0; i < 8; ++i) pick = (pick << 8) | h[static_cast<std::size_t>(i)];
  std::uint64_t target = pick % vs.total_voting_power;
  std::uint64_t cum = 0;
  for (const auto& v : vs.validators) {
    if (!v.active) continue;
    cum += v.voting_power;
    if (target < cum) return v.validator_pubkey;
  }
  return vs.validators.back().validator_pubkey;
}

std::vector<std::uint8_t> serialize_vote(const Vote& v) {
  BinaryWriter w;
  w.write_u8(static_cast<std::uint8_t>(v.phase));
  w.write_u64(v.epoch);
  w.write_u64(v.view);
  w.write_u32(v.height);
  w.write_bytes(std::span<const std::uint8_t>(v.block_hash.data(), v.block_hash.size()));
  w.write_bytes(std::span<const std::uint8_t>(v.validator_pubkey.data(), v.validator_pubkey.size()));
  w.write_u64(v.voting_power);
  return w.take();
}

Result<Vote> deserialize_vote(std::span<const std::uint8_t> bytes) {
  BinaryReader r(bytes);
  Vote v;
  auto phase = r.read_u8(); if (!phase.ok()) return phase.error();
  auto epoch = r.read_u64(); if (!epoch.ok()) return epoch.error();
  auto view = r.read_u64(); if (!view.ok()) return view.error();
  auto height = r.read_u32(); if (!height.ok()) return height.error();
  auto block_len = r.read_u64(); if (!block_len.ok() || block_len.value() != 32) return MakeError(ErrorCode::kParseError, "invalid block hash length");
  auto block = r.read_bytes(32); if (!block.ok()) return block.error();
  auto validator_len = r.read_u64(); if (!validator_len.ok() || validator_len.value() != 32) return MakeError(ErrorCode::kParseError, "invalid validator key length");
  auto validator = r.read_bytes(32); if (!validator.ok()) return validator.error();
  auto power = r.read_u64(); if (!power.ok()) return power.error();
  v.phase = static_cast<VotePhase>(phase.value());
  v.epoch = epoch.value(); v.view = view.value(); v.height = height.value();
  std::copy(block.value().begin(), block.value().end(), v.block_hash.begin());
  std::copy(validator.value().begin(), validator.value().end(), v.validator_pubkey.begin());
  v.voting_power = power.value();
  return v;
}

std::vector<std::uint8_t> serialize_proposal(const BlockProposal& p) {
  BinaryWriter w; w.write_u64(p.epoch); w.write_u64(p.view); w.write_u32(p.height);
  for (auto b : p.block_hash) w.write_u8(b);
  for (auto b : p.parent_hash) w.write_u8(b);
  for (auto b : p.proposer_pubkey) w.write_u8(b);
  w.write_u64(p.timestamp);
  w.write_u8(p.justify_qc.has_value() ? 1 : 0);
  if (p.justify_qc.has_value()) { auto qc = serialize_qc(*p.justify_qc); w.write_u32(static_cast<std::uint32_t>(qc.size())); for (auto b : qc) w.write_u8(b); }
  for (auto b : p.signature) w.write_u8(b);
  return w.take();
}
static std::vector<std::uint8_t> proposal_signing_bytes(const BlockProposal& p) {
  BinaryWriter w; const std::string d="PHX/bft/proposal/v2"; w.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(d.data()),d.size()));
  w.write_u64(p.epoch); w.write_u64(p.view); w.write_u32(p.height);
  for (auto b : p.block_hash) w.write_u8(b);
  for (auto b : p.parent_hash) w.write_u8(b);
  for (auto b : p.proposer_pubkey) w.write_u8(b);
  w.write_u64(p.timestamp);
  w.write_u8(p.justify_qc.has_value() ? 1 : 0);
  if (p.justify_qc.has_value()) { auto qc = serialize_qc(*p.justify_qc); w.write_u32(static_cast<std::uint32_t>(qc.size())); for (auto b : qc) w.write_u8(b); }
  return w.take();
}
Result<BlockProposal> sign_proposal(BlockProposal p, const crypto::SecretKey& sk) { auto b=proposal_signing_bytes(p); auto s=crypto::sign(b,sk); if(!s.ok())return s.error(); p.signature=s.value().bytes; return p; }
bool verify_proposal(const ValidatorSet& vs, const BlockProposal& p) { if (p.proposer_pubkey != select_proposer(vs,p.epoch,p.view)) return false; crypto::PublicKey pk{}; pk.bytes=p.proposer_pubkey; crypto::Signature s{}; s.bytes=p.signature; return crypto::verify(proposal_signing_bytes(p),s,pk); }
Result<BlockProposal> deserialize_proposal(std::span<const std::uint8_t> bytes) {
  BinaryReader r(bytes); BlockProposal p;
  auto e=r.read_u64(); auto v=r.read_u64(); auto h=r.read_u32(); if(!e.ok()||!v.ok()||!h.ok()) return MakeError(ErrorCode::kParseError,"invalid proposal");
  p.epoch=e.value(); p.view=v.value(); p.height=h.value(); auto read=[&](auto& a){auto b=r.read_bytes(a.size()); if(!b.ok()) return false; std::copy(b.value().begin(),b.value().end(),a.begin()); return true;};
  if (!read(p.block_hash) || !read(p.parent_hash) || !read(p.proposer_pubkey)) return MakeError(ErrorCode::kParseError, "invalid proposal");
  auto t = r.read_u64(); auto has_jqc = r.read_u8();
  if (!t.ok() || !has_jqc.ok()) return MakeError(ErrorCode::kParseError, "invalid proposal");
  p.timestamp = t.value();
  if (has_jqc.value()) { auto n=r.read_u32(); if(!n.ok()||n.value()>r.remaining()) return MakeError(ErrorCode::kParseError,"invalid proposal"); auto qb=r.read_bytes(n.value()); if(!qb.ok()) return qb.error(); auto qc=deserialize_qc(qb.value()); if(!qc.ok()) return qc.error(); p.justify_qc=qc.value(); }
  if (!read(p.signature)) return MakeError(ErrorCode::kParseError, "invalid proposal");
  return p;
}
std::vector<std::uint8_t> serialize_qc(const QuorumCertificate& q) {
  BinaryWriter w; w.write_u8(static_cast<std::uint8_t>(q.phase)); w.write_u64(q.epoch); w.write_u64(q.view); w.write_u32(q.height); for(auto b:q.block_hash)w.write_u8(b); w.write_u64(q.signed_voting_power); w.write_u32(static_cast<std::uint32_t>(q.votes.size())); for(const auto& v:q.votes){auto x=serialize_vote(v); w.write_u32(static_cast<std::uint32_t>(x.size()+64)); for(auto b:x)w.write_u8(b); for(auto b:v.signature)w.write_u8(b);} return w.take();
}
Result<QuorumCertificate> deserialize_qc(std::span<const std::uint8_t> bytes) {
  BinaryReader r(bytes); QuorumCertificate q; auto ph=r.read_u8();auto e=r.read_u64();auto v=r.read_u64();auto h=r.read_u32(); if(!ph.ok()||!e.ok()||!v.ok()||!h.ok())return MakeError(ErrorCode::kParseError,"invalid qc"); q.phase=static_cast<VotePhase>(ph.value());q.epoch=e.value();q.view=v.value();q.height=h.value();auto bh=r.read_bytes(32);if(!bh.ok())return bh.error();std::copy(bh.value().begin(),bh.value().end(),q.block_hash.begin());auto sp=r.read_u64();auto n=r.read_u32();if(!sp.ok()||!n.ok()||n.value()>1024)return MakeError(ErrorCode::kParseError,"invalid qc");q.signed_voting_power=sp.value();for(uint32_t i=0;i<n.value();++i){auto len=r.read_u32();if(!len.ok()||len.value()<64)return MakeError(ErrorCode::kParseError,"invalid qc");auto b=r.read_bytes(len.value());if(!b.ok())return b.error();auto vr=deserialize_vote(std::span<const uint8_t>(b.value().data(),len.value()-64));if(!vr.ok())return vr.error();std::copy(b.value().end()-64,b.value().end(),vr.value().signature.begin());q.votes.push_back(vr.value());}return q;
}

Hash256Bytes vote_hash(const Vote& v) {
  auto bytes = serialize_vote(v);
  const std::string domain = "PHX/bft/vote/v1";
  std::vector<std::uint8_t> pre(domain.begin(), domain.end());
  pre.insert(pre.end(), bytes.begin(), bytes.end());
  return sha256d(pre);
}

Result<Vote> sign_vote(Vote v, const crypto::SecretKey& sk) {
  auto h = vote_hash(v);
  auto sig = crypto::sign(std::span<const std::uint8_t>(h.data(), h.size()), sk);
  if (!sig.ok()) return sig.error();
  v.signature = sig.value().bytes;
  return v;
}

bool verify_vote(const ValidatorSet& vs, const Vote& v) {
  const auto* val = find_validator(vs, v.validator_pubkey);
  if (val == nullptr || !val->active) return false;
  if (val->voting_power != v.voting_power) return false;
  crypto::PublicKey pk{}; pk.bytes = v.validator_pubkey;
  crypto::Signature sig{}; sig.bytes = v.signature;
  auto h = vote_hash(v);
  return crypto::verify(std::span<const std::uint8_t>(h.data(), h.size()), sig, pk);
}

Result<QuorumCertificate> build_qc(const ValidatorSet& vs, const std::vector<Vote>& votes) {
  if (votes.empty()) return MakeError(ErrorCode::kInvalidArgument, "no votes");
  QuorumCertificate qc;
  qc.phase = votes.front().phase;
  qc.epoch = votes.front().epoch;
  qc.view = votes.front().view;
  qc.height = votes.front().height;
  qc.block_hash = votes.front().block_hash;
  std::uint64_t power = 0;
  std::vector<std::string> seen;
  for (const auto& v : votes) {
    if (v.phase != qc.phase || v.epoch != qc.epoch || v.view != qc.view || v.block_hash != qc.block_hash || v.height != qc.height) {
      return MakeError(ErrorCode::kInvalidArgument, "mismatched votes in QC");
    }
    if (!verify_vote(vs, v)) return MakeError(ErrorCode::kInvalidArgument, "invalid vote in QC");
    auto k = pub_hex(v.validator_pubkey);
    if (std::find(seen.begin(), seen.end(), k) != seen.end()) continue; // duplicate
    seen.push_back(k);
    power += v.voting_power;
    qc.votes.push_back(v);
  }
  qc.signed_voting_power = power;
  if (!has_quorum(vs, power)) return MakeError(ErrorCode::kInvalidArgument, "insufficient quorum");
  return qc;
}

bool verify_qc(const ValidatorSet& vs, const QuorumCertificate& qc) {
  std::uint64_t power = 0;
  std::vector<std::string> seen;
  for (const auto& v : qc.votes) {
    if (v.phase != qc.phase || v.epoch != qc.epoch || v.view != qc.view || v.block_hash != qc.block_hash || v.height != qc.height) return false;
    if (!verify_vote(vs, v)) return false;
    auto k = pub_hex(v.validator_pubkey);
    if (std::find(seen.begin(), seen.end(), k) != seen.end()) return false;
    seen.push_back(k);
    power += v.voting_power;
  }
  return has_quorum(vs, power);
}

std::vector<std::uint8_t> serialize_view_change(const ViewChange& v) {
  BinaryWriter w;
  w.write_u64(v.epoch);
  w.write_u32(v.height);
  w.write_u64(v.new_view);
  for (auto b : v.validator_pubkey) w.write_u8(b);
  w.write_u64(v.voting_power);
  w.write_u8(v.high_qc.has_value() ? 1 : 0);
  if (v.high_qc.has_value()) { auto qc = serialize_qc(*v.high_qc); w.write_u32(static_cast<std::uint32_t>(qc.size())); for (auto b : qc) w.write_u8(b); }
  return w.take();
}

Result<ViewChange> deserialize_view_change(std::span<const std::uint8_t> bytes) {
  BinaryReader r(bytes);
  ViewChange v;
  auto epoch = r.read_u64(); auto height = r.read_u32(); auto new_view = r.read_u64();
  if (!epoch.ok() || !height.ok() || !new_view.ok()) return MakeError(ErrorCode::kParseError, "invalid view change");
  auto pk = r.read_bytes(32); auto power = r.read_u64(); auto has_hqc = r.read_u8();
  if (!pk.ok() || !power.ok() || !has_hqc.ok()) return MakeError(ErrorCode::kParseError, "invalid view change");
  v.epoch = epoch.value(); v.height = height.value(); v.new_view = new_view.value();
  std::copy(pk.value().begin(), pk.value().end(), v.validator_pubkey.begin());
  v.voting_power = power.value();
  if (has_hqc.value()) {
    auto n = r.read_u32(); if (!n.ok() || n.value() > r.remaining()) return MakeError(ErrorCode::kParseError, "invalid view change");
    auto qb = r.read_bytes(n.value()); if (!qb.ok()) return qb.error();
    auto qc = deserialize_qc(qb.value()); if (!qc.ok()) return qc.error();
    v.high_qc = qc.value();
  }
  return v;
}

Hash256Bytes view_change_hash(const ViewChange& v) {
  auto bytes = serialize_view_change(v);
  const std::string domain = "PHX/bft/viewchange/v2";
  std::vector<std::uint8_t> preimage(domain.begin(), domain.end());
  preimage.insert(preimage.end(), bytes.begin(), bytes.end());
  return sha256d(std::span<const std::uint8_t>(preimage.data(), preimage.size()));
}

Result<ViewChange> sign_view_change(ViewChange v, const crypto::SecretKey& sk) {
  auto h = view_change_hash(v);
  auto sig = crypto::sign(std::span<const std::uint8_t>(h.data(), h.size()), sk);
  if (!sig.ok()) return sig.error();
  v.signature = sig.value().bytes;
  return v;
}

bool verify_view_change(const ValidatorSet& vs, const ViewChange& v) {
  const auto* val = find_validator(vs, v.validator_pubkey);
  if (val == nullptr || !val->active) return false;
  if (val->voting_power != v.voting_power) return false;
  crypto::PublicKey pk{}; pk.bytes = v.validator_pubkey;
  crypto::Signature sig{}; sig.bytes = v.signature;
  auto h = view_change_hash(v);
  return crypto::verify(std::span<const std::uint8_t>(h.data(), h.size()), sig, pk);
}

std::vector<std::uint8_t> serialize_view_change_qc(const ViewChangeQC& qc) {
  BinaryWriter w;
  w.write_u64(qc.epoch); w.write_u32(qc.height); w.write_u64(qc.new_view);
  w.write_u64(qc.signed_voting_power);
  w.write_u32(static_cast<std::uint32_t>(qc.votes.size()));
  for (const auto& v : qc.votes) {
    auto vb = serialize_view_change(v);
    w.write_u32(static_cast<std::uint32_t>(vb.size() + 64));
    for (auto b : vb) w.write_u8(b);
    for (auto b : v.signature) w.write_u8(b);
  }
  return w.take();
}

Result<ViewChangeQC> deserialize_view_change_qc(std::span<const std::uint8_t> bytes) {
  BinaryReader r(bytes);
  ViewChangeQC qc;
  auto epoch=r.read_u64(); auto height=r.read_u32(); auto nv=r.read_u64(); auto power=r.read_u64(); auto n=r.read_u32();
  if (!epoch.ok()||!height.ok()||!nv.ok()||!power.ok()||!n.ok()) return MakeError(ErrorCode::kParseError, "invalid view change qc");
  qc.epoch=epoch.value(); qc.height=height.value(); qc.new_view=nv.value(); qc.signed_voting_power=power.value();
  for (uint32_t i=0;i<n.value();++i) {
    auto sz=r.read_u32(); if(!sz.ok()||sz.value()<64) return MakeError(ErrorCode::kParseError, "invalid view change qc");
    auto b=r.read_bytes(sz.value()); if(!b.ok()) return b.error();
    auto vc=deserialize_view_change(std::span<const uint8_t>(b.value().data(), sz.value()-64));
    if(!vc.ok()) return vc.error();
    std::copy(b.value().end()-64,b.value().end(),vc.value().signature.begin());
    qc.votes.push_back(vc.value());
  }
  return qc;
}

bool verify_view_change_qc(const ValidatorSet& vs, const ViewChangeQC& qc) {
  std::uint64_t power = 0;
  std::vector<std::string> seen;
  for (const auto& v : qc.votes) {
    if (v.epoch != qc.epoch || v.height != qc.height || v.new_view != qc.new_view) return false;
    if (!verify_view_change(vs, v)) return false;
    auto who = pub_hex(v.validator_pubkey);
    if (std::find(seen.begin(), seen.end(), who) != seen.end()) continue;
    seen.push_back(who);
    power += v.voting_power;
  }
  return power == qc.signed_voting_power && has_quorum(vs, power);
}

std::vector<std::uint8_t> serialize_new_view(const NewView& nv) {
  BinaryWriter w;
  w.write_u64(nv.epoch); w.write_u32(nv.height); w.write_u64(nv.view);
  auto qc = serialize_view_change_qc(nv.view_change_qc);
  w.write_u32(static_cast<std::uint32_t>(qc.size())); for (auto b : qc) w.write_u8(b);
  w.write_u8(nv.proposal.has_value() ? 1 : 0);
  if (nv.proposal.has_value()) { auto p = serialize_proposal(*nv.proposal); w.write_u32(static_cast<std::uint32_t>(p.size())); for (auto b : p) w.write_u8(b); }
  for (auto b : nv.proposer_pubkey) w.write_u8(b);
  for (auto b : nv.signature) w.write_u8(b);
  return w.take();
}

Result<NewView> deserialize_new_view(std::span<const std::uint8_t> bytes) {
  BinaryReader r(bytes);
  NewView nv;
  auto epoch=r.read_u64(); auto height=r.read_u32(); auto view=r.read_u64(); auto qcn=r.read_u32();
  if (!epoch.ok()||!height.ok()||!view.ok()||!qcn.ok()||qcn.value()>r.remaining()) return MakeError(ErrorCode::kParseError, "invalid new view");
  nv.epoch=epoch.value(); nv.height=height.value(); nv.view=view.value();
  auto qcb=r.read_bytes(qcn.value()); if(!qcb.ok()) return qcb.error();
  auto qc=deserialize_view_change_qc(qcb.value()); if(!qc.ok()) return qc.error(); nv.view_change_qc=qc.value();
  auto has_p=r.read_u8(); if(!has_p.ok()) return has_p.error();
  if (has_p.value()) { auto pn=r.read_u32(); if(!pn.ok()||pn.value()>r.remaining()) return MakeError(ErrorCode::kParseError,"invalid new view"); auto pb=r.read_bytes(pn.value()); if(!pb.ok()) return pb.error(); auto p=deserialize_proposal(pb.value()); if(!p.ok()) return p.error(); nv.proposal=p.value(); }
  auto pk=r.read_bytes(32); auto sig=r.read_bytes(64);
  if(!pk.ok()||!sig.ok()) return MakeError(ErrorCode::kParseError, "invalid new view");
  std::copy(pk.value().begin(),pk.value().end(),nv.proposer_pubkey.begin());
  std::copy(sig.value().begin(),sig.value().end(),nv.signature.begin());
  return nv;
}

static std::vector<std::uint8_t> new_view_signing_bytes(const NewView& nv) {
  BinaryWriter w; const std::string d="PHX/bft/newview/v1"; w.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(d.data()),d.size()));
  w.write_u64(nv.epoch); w.write_u32(nv.height); w.write_u64(nv.view);
  auto qc = serialize_view_change_qc(nv.view_change_qc); w.write_u32(static_cast<std::uint32_t>(qc.size())); for (auto b : qc) w.write_u8(b);
  w.write_u8(nv.proposal.has_value()?1:0);
  if (nv.proposal.has_value()) { auto p=serialize_proposal(*nv.proposal); w.write_u32(static_cast<std::uint32_t>(p.size())); for (auto b:p) w.write_u8(b); }
  for (auto b : nv.proposer_pubkey) w.write_u8(b);
  return w.take();
}

Result<NewView> sign_new_view(NewView nv, const crypto::SecretKey& sk) {
  auto b=new_view_signing_bytes(nv); auto s=crypto::sign(b,sk); if(!s.ok()) return s.error(); nv.signature=s.value().bytes; return nv;
}

std::array<std::uint8_t, 32> select_new_view_proposer(const ValidatorSet& vs, const ViewChangeQC& qc) {
  std::map<std::array<std::uint8_t, 32>, Validator> by_key;
  for (const auto& vc : qc.votes) {
    const auto* validator = find_validator(vs, vc.validator_pubkey);
    if (validator == nullptr || !validator->active) continue;
    by_key.emplace(validator->validator_pubkey, *validator);
  }
  ValidatorSet signers;
  signers.epoch = qc.epoch;
  for (const auto& [_, validator] : by_key) {
    signers.validators.push_back(validator);
    signers.total_voting_power += validator.voting_power;
  }
  return select_proposer(signers, qc.epoch, qc.new_view);
}

bool verify_new_view(const ValidatorSet& vs, const NewView& nv) {
  if (nv.proposer_pubkey != select_new_view_proposer(vs, nv.view_change_qc)) return false;
  if (!verify_view_change_qc(vs, nv.view_change_qc)) return false;
  if (nv.view_change_qc.epoch != nv.epoch || nv.view_change_qc.height != nv.height || nv.view_change_qc.new_view != nv.view) return false;
  crypto::PublicKey pk{}; pk.bytes=nv.proposer_pubkey; crypto::Signature sig{}; sig.bytes=nv.signature;
  return crypto::verify(new_view_signing_bytes(nv), sig, pk);
}

std::optional<ViewChangeQC> BftEngine::maybe_view_change_qc() const {
  std::map<std::uint64_t, std::vector<ViewChange>> by_view;
  for (const auto& [_, vc] : view_changes_) {
    if (vc.epoch != state_.epoch) continue;
    if (vc.height != state_.height) continue;
    if (vc.new_view <= state_.view) continue;
    by_view[vc.new_view].push_back(vc);
  }
  for (auto& [nv, votes] : by_view) {
    std::uint64_t power = 0;
    std::vector<std::string> seen;
    std::vector<ViewChange> uniq;
    for (const auto& v : votes) {
      auto who = pub_hex(v.validator_pubkey);
      if (std::find(seen.begin(), seen.end(), who) != seen.end()) continue;
      seen.push_back(who); uniq.push_back(v); power += v.voting_power;
    }
    if (has_quorum(validators_, power)) {
      ViewChangeQC qc; qc.epoch=state_.epoch; qc.height=state_.height; qc.new_view=nv; qc.votes=std::move(uniq); qc.signed_voting_power=power;
      return qc;
    }
  }
  return std::nullopt;
}

std::string BftEngine::view_change_key(const ViewChange& v) {
  return pub_hex(v.validator_pubkey) + ":" + std::to_string(v.epoch) + ":" + std::to_string(v.height) + ":" + std::to_string(v.new_view);
}

Result<void> BftEngine::submit_view_change(const ViewChange& vc) {
  if (is_slashed(vc.validator_pubkey)) return MakeError(ErrorCode::kInvalidArgument, "slashed validator");
  if (!verify_view_change(validators_, vc)) return MakeError(ErrorCode::kInvalidArgument, "invalid view change");
  if (vc.epoch != state_.epoch) return MakeError(ErrorCode::kInvalidArgument, "epoch mismatch");
  if (vc.height != state_.height) return MakeError(ErrorCode::kInvalidArgument, "height mismatch");
  if (vc.new_view <= state_.view) return MakeError(ErrorCode::kInvalidArgument, "stale view change");
  if (vc.new_view > state_.view + kMaxViewJump) return MakeError(ErrorCode::kInvalidArgument, "view change too far ahead");
  if (vc.high_qc.has_value()) {
    // high_qc may reference a prior height in the same epoch after height advance.
    if (vc.high_qc->epoch != vc.epoch || vc.high_qc->height > vc.height) {
      return MakeError(ErrorCode::kInvalidArgument, "high qc mismatch");
    }
    auto observed = observe_qc(vc.high_qc.value());
    if (!observed.ok()) return observed;
  }
  auto k = view_change_key(vc);
  auto it = view_changes_.find(k);
  if (it != view_changes_.end()) {
    // Same validator/slot with a different payload is equivocation; a byte-for-byte
    // resend is benign deduplication.
    if (view_change_hash(it->second) != view_change_hash(vc)) {
      ++view_change_equivocations_;
      slashed_validators_.insert(pub_hex(vc.validator_pubkey));
    }
    return Result<void>{};
  }
  view_changes_[k] = vc;
  return Result<void>{};
}

Result<void> BftEngine::check_proposal_lock(const BlockProposal& proposal) const {
  if (!state_.locked_hash.has_value()) return Result<void>{};
  const auto& locked_hash = state_.locked_hash.value();
  if (locked_hash == proposal.block_hash) return Result<void>{};
  // A child of the locked block is safe without a new QC: it cannot rewrite
  // the locked prefix. This is the normal path after height h is finalized.
  if (proposal.height == state_.locked_height + 1 && proposal.parent_hash == locked_hash) {
    return Result<void>{};
  }
  if (!proposal.justify_qc.has_value()) {
    return MakeError(ErrorCode::kInvalidArgument, "locked on different hash without justify qc");
  }
  const auto& justify = proposal.justify_qc.value();
  if (justify.phase != VotePhase::kPrevote) {
    return MakeError(ErrorCode::kInvalidArgument, "justify qc must be a prevote qc");
  }
  const bool extends_locked = state_.locked_hash.has_value() &&
                              proposal.parent_hash == *state_.locked_hash &&
                              proposal.height == state_.locked_height + 1;
  const bool justify_matches_proposal = justify.epoch == proposal.epoch && justify.height == proposal.height &&
                                        justify.block_hash == proposal.block_hash;
  const bool justify_matches_locked_parent = extends_locked && justify.epoch == proposal.epoch &&
                                             justify.height == state_.locked_height &&
                                             justify.block_hash == *state_.locked_hash;
  const bool justify_matches_parent = justify.epoch == proposal.epoch &&
                                      proposal.height == justify.height + 1 &&
                                      justify.block_hash == proposal.parent_hash;
  if (!justify_matches_proposal && !justify_matches_locked_parent && !justify_matches_parent) {
    return MakeError(ErrorCode::kInvalidArgument,
                     "justify qc mismatch p_h=" + std::to_string(proposal.height) +
                     " p_v=" + std::to_string(proposal.view) +
                     " j_h=" + std::to_string(justify.height) +
                     " j_v=" + std::to_string(justify.view) +
                     " locked_h=" + std::to_string(state_.locked_height) +
                     " locked_v=" + std::to_string(state_.locked_view) +
                     " par=" + (justify.block_hash == proposal.parent_hash ? "1" : "0") +
                     " ext=" + (extends_locked ? "1" : "0"));
  }
  // A justify QC must not unlock to a conflicting branch unless it advances
  // the lock view. Extending the locked branch is safe without an advance.
  if (!extends_locked && justify.view <= state_.locked_view) {
    return MakeError(ErrorCode::kInvalidArgument, "justify qc does not advance the lock");
  }
  if (!verify_qc(validators_, justify)) {
    return MakeError(ErrorCode::kInvalidArgument, "invalid justify qc");
  }
  return Result<void>{};
}

std::optional<std::uint64_t> BftEngine::maybe_advance_view_from_view_changes() const {
  auto qc = maybe_view_change_qc();
  if (qc.has_value()) return qc->new_view;
  return std::nullopt;
}

std::string BftEngine::vote_key(const Vote& v) {
  return pub_hex(v.validator_pubkey) + ":" + std::to_string(static_cast<int>(v.phase)) + ":" +
         std::to_string(v.epoch) + ":" + std::to_string(v.view) + ":" +
         std::to_string(v.height) + ":" + pub_hex(v.block_hash);
}

bool BftEngine::is_slashed(const std::array<std::uint8_t, 32>& validator_pubkey) const {
  return slashed_validators_.find(pub_hex(validator_pubkey)) != slashed_validators_.end();
}

Result<void> BftEngine::submit_vote(const Vote& vote) {
  if (is_slashed(vote.validator_pubkey)) return MakeError(ErrorCode::kInvalidArgument, "slashed validator");
  if (!verify_vote(validators_, vote)) return MakeError(ErrorCode::kInvalidArgument, "invalid vote");
  if (vote.epoch != state_.epoch) return MakeError(ErrorCode::kInvalidArgument, "epoch mismatch");
  if (vote.view != state_.view) return MakeError(ErrorCode::kInvalidArgument, "view mismatch");
  auto k = vote_key(vote);
  for (const auto& [_, prior] : votes_) {
    if (prior.validator_pubkey == vote.validator_pubkey && prior.phase == vote.phase &&
        prior.epoch == vote.epoch && prior.view == vote.view && prior.height == vote.height &&
        prior.block_hash != vote.block_hash) {
      equivocations_.push_back({vote.validator_pubkey, vote.phase, vote.epoch, vote.view, prior.block_hash, vote.block_hash});
      slashed_validators_.insert(pub_hex(vote.validator_pubkey));
      return MakeError(ErrorCode::kInvalidArgument, "equivocation detected");
    }
  }
  if (votes_.find(k) != votes_.end()) return Result<void>{}; // duplicate
  votes_[k] = vote;
  return Result<void>{};
}

std::optional<QuorumCertificate> BftEngine::maybe_qc(VotePhase phase, std::uint64_t epoch, std::uint64_t view, const Hash256Bytes& block_hash) const {
  std::vector<Vote> matching;
  for (const auto& [_, v] : votes_) {
    if (v.phase == phase && v.epoch == epoch && v.view == view && v.block_hash == block_hash) matching.push_back(v);
  }
  auto qc = build_qc(validators_, matching);
  if (!qc.ok()) return std::nullopt;
  return qc.value();
}

Result<void> BftEngine::observe_qc(const QuorumCertificate& qc) {
  if (!verify_qc(validators_, qc)) return MakeError(ErrorCode::kInvalidArgument, "invalid qc");
  if (qc.epoch != state_.epoch) return MakeError(ErrorCode::kInvalidArgument, "epoch mismatch");
  if (qc.phase == VotePhase::kPrevote) {
    if (qc.view >= state_.valid_view) {
      state_.valid_view = qc.view;
      state_.valid_qc = qc;
    }
  } else if (qc.phase == VotePhase::kPrecommit) {
    if (qc.view >= state_.locked_view) {
      state_.locked_view = qc.view;
      state_.locked_height = qc.height;
      state_.locked_hash = qc.block_hash;
    }
  }
  return Result<void>{};
}

Result<void> BftEngine::finalize(const FinalityCertificate& cert) {
  if (!verify_qc(validators_, cert.precommit_qc)) return MakeError(ErrorCode::kInvalidArgument, "invalid finality QC");
  if (cert.precommit_qc.phase != VotePhase::kPrecommit) return MakeError(ErrorCode::kInvalidArgument, "finality requires precommit QC");
  (void)observe_qc(cert.precommit_qc);
  if (cert.precommit_qc.block_hash != cert.block_hash || cert.precommit_qc.height != cert.height) {
    return MakeError(ErrorCode::kInvalidArgument, "finality certificate mismatch");
  }
  if (state_.last_finalized.has_value()) {
    if (cert.height < state_.last_finalized->height) return MakeError(ErrorCode::kInvalidArgument, "stale finality");
    if (cert.height == state_.last_finalized->height) {
      if (cert.block_hash != state_.last_finalized->block_hash) {
        return MakeError(ErrorCode::kInvalidArgument, "conflicting finalized block");
      }
      return Result<void>{}; // idempotent duplicate certificate
    }
  }
  state_.last_finalized = cert;
  state_.height = cert.height + 1;
  state_.view = 0;
  state_.locked_hash = cert.block_hash;
  state_.locked_height = cert.height;
  state_.locked_view = 0;
  state_.valid_qc.reset();
  state_.valid_view = 0;
  votes_.clear();
  view_changes_.clear();
  (void)enforce_randomness_deadline(cert.height);
  return Result<void>{};
}

void BftEngine::set_pacemaker_config(PacemakerConfig cfg) {
  if (cfg.base_timeout_ms == 0) cfg.base_timeout_ms = 1;
  if (cfg.max_timeout_ms < cfg.base_timeout_ms) cfg.max_timeout_ms = cfg.base_timeout_ms;
  if (cfg.backoff_denominator == 0) cfg.backoff_denominator = 1;
  pacemaker_cfg_ = cfg;
  pacemaker_.timeout_ms = cfg.base_timeout_ms;
}

void BftEngine::note_progress(std::uint64_t now_ms) {
  pacemaker_.last_progress_ms = now_ms;
  pacemaker_.timeouts_in_view = 0;
  pacemaker_.timeout_ms = pacemaker_cfg_.base_timeout_ms;
  pacemaker_.armed = true;
}

bool BftEngine::should_timeout(std::uint64_t now_ms) const {
  if (!pacemaker_.armed) return false;
  return now_ms >= pacemaker_.last_progress_ms + pacemaker_.timeout_ms;
}

Result<void> BftEngine::enter_view(std::uint64_t new_view) {
  if (new_view <= state_.view) return MakeError(ErrorCode::kInvalidArgument, "stale view");
  state_.view = new_view;
  votes_.clear();
  for (auto it = view_changes_.begin(); it != view_changes_.end();) {
    if (it->second.new_view <= state_.view) it = view_changes_.erase(it);
    else ++it;
  }
  pacemaker_.timeouts_in_view = 0;
  pacemaker_.timeout_ms = pacemaker_cfg_.base_timeout_ms;
  return Result<void>{};
}

Result<void> BftEngine::timeout(std::uint64_t now) {
  ++pacemaker_.timeouts_in_view;
  const auto next_timeouts = pacemaker_.timeouts_in_view;
  const auto scaled = pacemaker_.timeout_ms * pacemaker_cfg_.backoff_numerator / pacemaker_cfg_.backoff_denominator;
  const auto next_timeout = std::min(pacemaker_cfg_.max_timeout_ms, std::max(pacemaker_.timeout_ms + 1, scaled));
  auto r = enter_view(state_.view + 1);
  if (!r.ok()) return r;
  pacemaker_.timeouts_in_view = next_timeouts;
  pacemaker_.timeout_ms = next_timeout;
  pacemaker_.last_progress_ms = now;
  return Result<void>{};
}

void BftEngine::rotate_validators(ValidatorSet next) {
  validators_ = std::move(next);
  state_.epoch += 1;
  state_.view = 0;
  votes_.clear();
}

static Hash256Bytes randomness_commitment(const std::array<std::uint8_t,32>& reveal, const std::array<std::uint8_t,32>& pubkey, std::uint64_t epoch) {
  BinaryWriter w;
  const std::string dom = "PHX/rand/commit/v1";
  w.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(dom.data()), dom.size()));
  w.write_bytes(std::span<const std::uint8_t>(reveal.data(), reveal.size()));
  w.write_bytes(std::span<const std::uint8_t>(pubkey.data(), pubkey.size()));
  w.write_u64(epoch);
  auto bytes = w.take();
  return sha256d(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
}

static std::vector<std::uint8_t> randomness_sign_bytes(std::string_view dom, std::uint64_t epoch, const std::array<std::uint8_t,32>& pubkey, const std::array<std::uint8_t,32>& value) {
  BinaryWriter w;
  w.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(dom.data()), dom.size()));
  w.write_u64(epoch);
  w.write_bytes(std::span<const std::uint8_t>(pubkey.data(), pubkey.size()));
  w.write_bytes(std::span<const std::uint8_t>(value.data(), value.size()));
  return w.take();
}

Result<void> BftEngine::submit_randomness_commit(const RandomnessCommit& c) {
  const auto* val = find_validator(validators_, c.validator_pubkey);
  if (val == nullptr || !val->active) return MakeError(ErrorCode::kInvalidArgument, "unknown validator");
  if (is_slashed(c.validator_pubkey)) return MakeError(ErrorCode::kInvalidArgument, "slashed validator");
  if (c.epoch != state_.epoch) return MakeError(ErrorCode::kInvalidArgument, "epoch mismatch");
  crypto::PublicKey pk{}; pk.bytes = c.validator_pubkey; crypto::Signature sig{}; sig.bytes = c.signature;
  auto msg = randomness_sign_bytes("PHX/rand/commit-sig/v1", c.epoch, c.validator_pubkey, c.commitment);
  if (!crypto::verify(std::span<const std::uint8_t>(msg.data(), msg.size()), sig, pk)) return MakeError(ErrorCode::kInvalidArgument, "bad commit signature");
  auto key = std::to_string(c.epoch) + ":" + pub_hex(c.validator_pubkey);
  if (randomness_commits_.count(key)) return Result<void>{};
  RandomnessCommit stored = c;
  if (stored.height == 0) stored.height = state_.height;
  randomness_commits_[key] = stored;
  return Result<void>{};
}

Result<void> BftEngine::submit_randomness_reveal(const RandomnessReveal& r) {
  const auto* val = find_validator(validators_, r.validator_pubkey);
  if (val == nullptr || !val->active) return MakeError(ErrorCode::kInvalidArgument, "unknown validator");
  if (is_slashed(r.validator_pubkey)) return MakeError(ErrorCode::kInvalidArgument, "slashed validator");
  if (r.epoch != state_.epoch) return MakeError(ErrorCode::kInvalidArgument, "epoch mismatch");
  auto key = std::to_string(r.epoch) + ":" + pub_hex(r.validator_pubkey);
  auto it = randomness_commits_.find(key);
  if (it == randomness_commits_.end()) return MakeError(ErrorCode::kInvalidArgument, "missing commit");
  auto expected = randomness_commitment(r.reveal, r.validator_pubkey, r.epoch);
  if (expected != it->second.commitment) return MakeError(ErrorCode::kInvalidArgument, "reveal does not match commit");
  crypto::PublicKey pk{}; pk.bytes = r.validator_pubkey; crypto::Signature sig{}; sig.bytes = r.signature;
  auto msg = randomness_sign_bytes("PHX/rand/reveal-sig/v1", r.epoch, r.validator_pubkey, r.reveal);
  if (!crypto::verify(std::span<const std::uint8_t>(msg.data(), msg.size()), sig, pk)) return MakeError(ErrorCode::kInvalidArgument, "bad reveal signature");
  randomness_reveals_[key] = r;
  return Result<void>{};
}

std::optional<Hash256Bytes> BftEngine::randomness_beacon(std::uint64_t epoch) const {
  std::vector<Hash256Bytes> reveals;
  for (const auto& [k, r] : randomness_reveals_) {
    if (r.epoch == epoch) reveals.push_back(r.reveal);
  }
  if (reveals.empty()) return std::nullopt;
  std::sort(reveals.begin(), reveals.end());
  BinaryWriter w;
  const std::string dom = "PHX/rand/beacon/v1";
  w.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(dom.data()), dom.size()));
  w.write_u64(epoch);
  for (const auto& rv : reveals) w.write_bytes(std::span<const std::uint8_t>(rv.data(), rv.size()));
  auto bytes = w.take();
  return sha256d(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
}

Result<void> BftEngine::record_slashing(const std::array<std::uint8_t, 32>& validator_pubkey, SlashingReason reason, std::uint64_t epoch, std::uint64_t view, std::uint32_t height, std::uint64_t penalty_atoms, std::string evidence_hex) {
  const auto* val = find_validator(validators_, validator_pubkey);
  if (val == nullptr) return MakeError(ErrorCode::kInvalidArgument, "unknown validator");
  SlashingRecord rec;
  rec.validator_pubkey = validator_pubkey;
  rec.reason = reason;
  rec.epoch = epoch; rec.view = view; rec.height = height;
  rec.penalty_atoms = penalty_atoms;
  rec.evidence_hex = std::move(evidence_hex);
  slashing_records_.push_back(std::move(rec));
  slashed_validators_.insert(pub_hex(validator_pubkey));
  return Result<void>{};
}

Result<void> BftEngine::appeal_slashing(std::size_t index, const std::string& evidence_hex) {
  if (index >= slashing_records_.size()) return MakeError(ErrorCode::kInvalidArgument, "bad slashing index");
  auto& rec = slashing_records_[index];
  if (rec.appeal_resolved) return MakeError(ErrorCode::kInvalidArgument, "appeal already resolved");
  rec.appealed = true;
  if (!evidence_hex.empty()) rec.evidence_hex += ";appeal=" + evidence_hex;
  return Result<void>{};
}

Result<void> BftEngine::resolve_appeal(std::size_t index, bool accepted) {
  if (index >= slashing_records_.size()) return MakeError(ErrorCode::kInvalidArgument, "bad slashing index");
  auto& rec = slashing_records_[index];
  if (!rec.appealed) return MakeError(ErrorCode::kInvalidArgument, "no appeal");
  if (rec.appeal_resolved) return MakeError(ErrorCode::kInvalidArgument, "appeal already resolved");
  rec.appeal_resolved = true;
  rec.appeal_accepted = accepted;
  if (accepted) {
    slashed_validators_.erase(pub_hex(rec.validator_pubkey));
  }
  return Result<void>{};
}

Result<void> BftEngine::enforce_randomness_deadline(std::uint32_t current_height) {
  if (randomness_reveal_deadline_blocks_ == 0) return Result<void>{};
  for (const auto& [key, c] : randomness_commits_) {
    if (c.epoch != state_.epoch) continue;
    if (randomness_reveals_.count(key)) continue;
    if (is_slashed(c.validator_pubkey)) continue;
    const std::uint64_t due = static_cast<std::uint64_t>(c.height) + randomness_reveal_deadline_blocks_;
    if (static_cast<std::uint64_t>(current_height) < due) continue;
    auto rec = record_slashing(c.validator_pubkey, SlashingReason::kRandomnessWithholding, c.epoch, state_.view, current_height, 0,
                               "randomness_reveal_deadline");
    (void)rec;
  }
  return Result<void>{};
}

Result<void> BftEngine::settle_slashing(std::size_t index, const std::string& settlement_ref) {
  if (index >= slashing_records_.size()) return MakeError(ErrorCode::kInvalidArgument, "bad slashing index");
  auto& rec = slashing_records_[index];
  if (rec.appeal_resolved && rec.appeal_accepted) return MakeError(ErrorCode::kInvalidArgument, "accepted appeal cannot be settled");
  if (rec.settled) return Result<void>{};
  rec.settled = true;
  rec.settlement_ref = settlement_ref;
  return Result<void>{};
}

std::uint64_t BftEngine::total_slashed_penalty_atoms() const {
  std::uint64_t total = 0;
  for (const auto& r : slashing_records_) {
    if (r.appeal_resolved && r.appeal_accepted) continue;
    total += r.penalty_atoms;
  }
  return total;
}

std::uint64_t BftEngine::unsettled_slashed_penalty_atoms() const {
  std::uint64_t total = 0;
  for (const auto& r : slashing_records_) {
    if (r.appeal_resolved && r.appeal_accepted) continue;
    if (r.settled) continue;
    total += r.penalty_atoms;
  }
  return total;
}

Result<void> BftEngine::save_state(const std::string& path) const {
  BinaryWriter w;
  w.write_u32(0x50485741u); // PHWA
  w.write_u32(3u); // v3 adds slashed validators and equivocation evidence
  w.write_u64(state_.epoch); w.write_u64(state_.view); w.write_u32(state_.height);
  w.write_u64(state_.locked_view); w.write_u8(state_.locked_hash.has_value() ? 1 : 0);
  if (state_.locked_hash.has_value()) for (auto b : *state_.locked_hash) w.write_u8(b);
  w.write_u32(state_.locked_height);
  w.write_u64(state_.valid_view); w.write_u8(state_.valid_qc.has_value() ? 1 : 0);
  if (state_.valid_qc.has_value()) { auto qc = serialize_qc(*state_.valid_qc); w.write_u32(static_cast<std::uint32_t>(qc.size())); for (auto b : qc) w.write_u8(b); }
  w.write_u8(state_.last_finalized.has_value() ? 1 : 0);
  if (state_.last_finalized.has_value()) {
    w.write_u32(state_.last_finalized->height);
    for (auto b : state_.last_finalized->block_hash) w.write_u8(b);
    auto qc = serialize_qc(state_.last_finalized->precommit_qc); w.write_u32(static_cast<std::uint32_t>(qc.size())); for (auto b : qc) w.write_u8(b);
  }
  w.write_u32(static_cast<std::uint32_t>(votes_.size()));
  for (const auto& [_, v] : votes_) { auto vb = serialize_vote(v); w.write_u32(static_cast<std::uint32_t>(vb.size()+64)); for (auto b : vb) w.write_u8(b); for (auto b : v.signature) w.write_u8(b); }
  w.write_u32(static_cast<std::uint32_t>(view_changes_.size()));
  for (const auto& [_, vc] : view_changes_) { auto b = serialize_view_change(vc); w.write_u32(static_cast<std::uint32_t>(b.size()+64)); for (auto x : b) w.write_u8(x); for (auto x : vc.signature) w.write_u8(x); }
  w.write_u32(static_cast<std::uint32_t>(slashed_validators_.size()));
  for (const auto& who : slashed_validators_) {
    w.write_u32(static_cast<std::uint32_t>(who.size()));
    for (unsigned char c : who) w.write_u8(c);
  }
  w.write_u32(static_cast<std::uint32_t>(equivocations_.size()));
  for (const auto& ev : equivocations_) {
    for (auto b : ev.validator_pubkey) w.write_u8(b);
    w.write_u8(static_cast<std::uint8_t>(ev.phase));
    w.write_u64(ev.epoch);
    w.write_u64(ev.view);
    for (auto b : ev.hash_a) w.write_u8(b);
    for (auto b : ev.hash_b) w.write_u8(b);
  }
  auto bytes = w.take();
  const std::string tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return MakeError(ErrorCode::kIoError, "failed to open wal for write");
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out.good()) return MakeError(ErrorCode::kIoError, "failed to write wal");
  }
  int fd = ::open(tmp.c_str(), O_RDONLY);
  if (fd >= 0) { ::fsync(fd); ::close(fd); }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) return MakeError(ErrorCode::kIoError, "failed to rename wal");
  std::filesystem::path p(path);
  auto dir = p.parent_path().empty() ? std::filesystem::path(".") : p.parent_path();
  int dfd = ::open(dir.c_str(), O_RDONLY);
  if (dfd >= 0) { ::fsync(dfd); ::close(dfd); }
  return Result<void>{};
}

Result<void> BftEngine::load_state(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return MakeError(ErrorCode::kNotFound, "wal not found");
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  BinaryReader r(bytes);
  auto magic = r.read_u32(); auto ver = r.read_u32();
  if (!magic.ok() || !ver.ok() || magic.value() != 0x50485741u || (ver.value() < 1u || ver.value() > 3u)) return MakeError(ErrorCode::kParseError, "invalid wal");
  auto epoch=r.read_u64(); auto view=r.read_u64(); auto height=r.read_u32(); auto locked_view=r.read_u64(); auto has_lock=r.read_u8();
  if(!epoch.ok()||!view.ok()||!height.ok()||!locked_view.ok()||!has_lock.ok()) return MakeError(ErrorCode::kParseError,"invalid wal");
  state_.epoch=epoch.value(); state_.view=view.value(); state_.height=height.value(); state_.locked_view=locked_view.value();
  if (has_lock.value()) { auto h=r.read_bytes(32); if(!h.ok()) return h.error(); Hash256Bytes hh{}; std::copy(h.value().begin(),h.value().end(),hh.begin()); state_.locked_hash=hh; } else state_.locked_hash.reset();
  if (ver.value() >= 2u) {
    auto locked_height = r.read_u32();
    if (!locked_height.ok()) return locked_height.error();
    state_.locked_height = locked_height.value();
  }
  auto valid_view=r.read_u64(); auto has_vqc=r.read_u8(); if(!valid_view.ok()||!has_vqc.ok()) return MakeError(ErrorCode::kParseError,"invalid wal"); state_.valid_view=valid_view.value();
  if (has_vqc.value()) { auto n=r.read_u32(); if(!n.ok()) return n.error(); auto qb=r.read_bytes(n.value()); if(!qb.ok()) return qb.error(); auto qc=deserialize_qc(qb.value()); if(!qc.ok()) return qc.error(); state_.valid_qc=qc.value(); } else state_.valid_qc.reset();
  auto has_fin=r.read_u8(); if(!has_fin.ok()) return has_fin.error();
  if (has_fin.value()) { FinalityCertificate fc; auto fh=r.read_u32(); auto fbh=r.read_bytes(32); auto qn=r.read_u32(); if(!fh.ok()||!fbh.ok()||!qn.ok()) return MakeError(ErrorCode::kParseError,"invalid wal"); fc.height=fh.value(); std::copy(fbh.value().begin(),fbh.value().end(),fc.block_hash.begin()); auto qb=r.read_bytes(qn.value()); if(!qb.ok()) return qb.error(); auto qc=deserialize_qc(qb.value()); if(!qc.ok()) return qc.error(); fc.precommit_qc=qc.value(); state_.last_finalized=fc; } else state_.last_finalized.reset();
  if (ver.value() == 1u && state_.locked_hash.has_value()) {
    // v1 WALs omitted locked_height. Recover it from the matching precommit
    // QC in the finality certificate, not from the current chain height.
    if (state_.last_finalized.has_value() && state_.last_finalized->block_hash == *state_.locked_hash) {
      state_.locked_height = state_.last_finalized->height;
    } else if (state_.valid_qc.has_value() && state_.valid_qc->block_hash == *state_.locked_hash) {
      state_.locked_height = state_.valid_qc->height;
    } else {
      state_.locked_height = state_.height;
    }
  }
  auto vn=r.read_u32(); if(!vn.ok()) return vn.error(); votes_.clear();
  for(uint32_t i=0;i<vn.value();++i){ auto n=r.read_u32(); if(!n.ok()||n.value()<64) return MakeError(ErrorCode::kParseError,"invalid wal"); auto b=r.read_bytes(n.value()); if(!b.ok()) return b.error(); auto vv=deserialize_vote(std::span<const uint8_t>(b.value().data(), n.value()-64)); if(!vv.ok()) return vv.error(); std::copy(b.value().end()-64,b.value().end(),vv.value().signature.begin()); votes_[vote_key(vv.value())]=vv.value(); }
  auto vcn=r.read_u32(); if(!vcn.ok()) return vcn.error(); view_changes_.clear();
  for(uint32_t i=0;i<vcn.value();++i){ auto n=r.read_u32(); if(!n.ok()||n.value()<64) return MakeError(ErrorCode::kParseError,"invalid wal"); auto b=r.read_bytes(n.value()); if(!b.ok()) return b.error(); auto vc=deserialize_view_change(std::span<const uint8_t>(b.value().data(), n.value()-64)); if(!vc.ok()) return vc.error(); std::copy(b.value().end()-64,b.value().end(),vc.value().signature.begin()); view_changes_[view_change_key(vc.value())]=vc.value(); }
  slashed_validators_.clear();
  equivocations_.clear();
  if (ver.value() >= 3u) {
    auto sn = r.read_u32(); if (!sn.ok()) return sn.error();
    for (uint32_t i = 0; i < sn.value(); ++i) {
      auto sl = r.read_u32(); if (!sl.ok()) return sl.error();
      auto sb = r.read_bytes(sl.value()); if (!sb.ok()) return sb.error();
      slashed_validators_.insert(std::string(sb.value().begin(), sb.value().end()));
    }
    auto en = r.read_u32(); if (!en.ok()) return en.error();
    for (uint32_t i = 0; i < en.value(); ++i) {
      EquivocationEvidence ev;
      auto pk = r.read_bytes(32); auto ph = r.read_u8(); auto ep = r.read_u64(); auto vw = r.read_u64();
      auto ha = r.read_bytes(32); auto hb = r.read_bytes(32);
      if (!pk.ok() || !ph.ok() || !ep.ok() || !vw.ok() || !ha.ok() || !hb.ok()) return MakeError(ErrorCode::kParseError, "invalid wal");
      std::copy(pk.value().begin(), pk.value().end(), ev.validator_pubkey.begin());
      ev.phase = static_cast<VotePhase>(ph.value()); ev.epoch = ep.value(); ev.view = vw.value();
      std::copy(ha.value().begin(), ha.value().end(), ev.hash_a.begin());
      std::copy(hb.value().begin(), hb.value().end(), ev.hash_b.begin());
      equivocations_.push_back(std::move(ev));
    }
  }
  if (state_.locked_view > state_.view) state_.locked_view = state_.view;
  if (state_.valid_view > state_.view) state_.valid_view = state_.view;
  for (auto it = votes_.begin(); it != votes_.end();) {
    if (it->second.epoch != state_.epoch) { it = votes_.erase(it); continue; }
    ++it;
  }
  for (auto it = view_changes_.begin(); it != view_changes_.end();) {
    if (it->second.epoch != state_.epoch) { it = view_changes_.erase(it); continue; }
    ++it;
  }
  return Result<void>{};
}

}  // namespace phantomx::consensus
