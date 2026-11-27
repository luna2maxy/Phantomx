#include "phantomx/aggregator/aggregator.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

#include "phantomx/common/serialization.hpp"
#include "phantomx/database/sqlite_db.hpp"
#include "phantomx/primitives/hash.hpp"

namespace phantomx::aggregator {
namespace {
std::array<std::uint8_t, 32> zero32() { return {}; }
bool same32(const std::array<std::uint8_t, 32>& a, const std::array<std::uint8_t, 32>& b) { return a == b; }
crypto::Signature arr_to_sig(const std::array<std::uint8_t, 64>& s) { return crypto::Signature{s}; }
crypto::PublicKey arr_to_pub(const std::array<std::uint8_t, 32>& p) { return crypto::PublicKey{p}; }
}  // namespace

std::vector<std::uint8_t> serialize_attestation(const VerifierAttestation& a) {
  BinaryWriter w;
  w.write_bytes(std::span<const std::uint8_t>(a.verifier_pubkey.data(), a.verifier_pubkey.size()));
  w.write_bytes(std::span<const std::uint8_t>(a.assignment_id.data(), a.assignment_id.size()));
  w.write_bytes(std::span<const std::uint8_t>(a.subject_id.data(), a.subject_id.size()));
  w.write_bytes(std::span<const std::uint8_t>(a.evidence_hash.data(), a.evidence_hash.size()));
  w.write_u8(static_cast<std::uint8_t>(a.subject_type));
  w.write_u8(static_cast<std::uint8_t>(a.result));
  w.write_u64(a.timestamp);
  w.write_u64(a.expires_at);
  return w.take();
}

Hash256Bytes attestation_hash(const VerifierAttestation& a) {
  const std::string domain = "PHX/aggregator/attestation/v1";
  auto body = serialize_attestation(a);
  std::vector<std::uint8_t> pre(domain.begin(), domain.end());
  pre.insert(pre.end(), body.begin(), body.end());
  return sha256d(pre);
}

bool verify_attestation_signature(const VerifierAttestation& a) {
  auto h = attestation_hash(a);
  return crypto::verify(std::span<const std::uint8_t>(h.data(), h.size()), arr_to_sig(a.signature), arr_to_pub(a.verifier_pubkey));
}

std::vector<std::uint8_t> serialize_certificate_body(const VerificationCertificate& c) {
  BinaryWriter w;
  w.write_u32(c.version);
  w.write_string(c.network_id);
  w.write_u64(c.epoch_id);
  w.write_bytes(std::span<const std::uint8_t>(c.assignment_id.data(), c.assignment_id.size()));
  w.write_bytes(std::span<const std::uint8_t>(c.subject_id.data(), c.subject_id.size()));
  w.write_u8(static_cast<std::uint8_t>(c.subject_type));
  w.write_bytes(std::span<const std::uint8_t>(c.evidence_hash.data(), c.evidence_hash.size()));
  w.write_u64(static_cast<std::uint64_t>(c.verifier_set.size()));
  for (const auto& v : c.verifier_set) w.write_bytes(std::span<const std::uint8_t>(v.data(), v.size()));
  w.write_u64(static_cast<std::uint64_t>(c.attestation_hashes.size()));
  for (const auto& h : c.attestation_hashes) w.write_bytes(std::span<const std::uint8_t>(h.data(), h.size()));
  w.write_u8(static_cast<std::uint8_t>(c.result));
  w.write_u64(static_cast<std::uint64_t>(c.quorum_required));
  w.write_u64(static_cast<std::uint64_t>(c.quorum_achieved));
  w.write_u64(c.issued_at);
  w.write_u64(c.expires_at);
  w.write_bytes(std::span<const std::uint8_t>(c.aggregator_pubkey.data(), c.aggregator_pubkey.size()));
  return w.take();
}

Hash256Bytes certificate_hash(const VerificationCertificate& c) {
  const std::string domain = "PHX/aggregator/certificate/v1";
  auto body = serialize_certificate_body(c);
  std::vector<std::uint8_t> pre(domain.begin(), domain.end());
  pre.insert(pre.end(), body.begin(), body.end());
  return sha256d(pre);
}

Result<VerificationCertificate> sign_certificate(VerificationCertificate c, const crypto::SecretKey& aggregator_secret) {
  auto h = certificate_hash(c);
  auto sig = crypto::sign(std::span<const std::uint8_t>(h.data(), h.size()), aggregator_secret);
  if (!sig.ok()) return sig.error();
  c.aggregator_signature = sig.value().bytes;
  return c;
}

bool verify_certificate_signature(const VerificationCertificate& c) {
  auto h = certificate_hash(c);
  return crypto::verify(std::span<const std::uint8_t>(h.data(), h.size()), arr_to_sig(c.aggregator_signature), arr_to_pub(c.aggregator_pubkey));
}

std::string AggregatorRegistry::key(const std::array<std::uint8_t, 32>& pk) {
  return to_hex(std::span<const std::uint8_t>(pk.data(), pk.size()));
}

Result<void> AggregatorRegistry::register_aggregator(AggregatorRecord r, std::uint64_t now) {
  if (same32(r.aggregator_pubkey, zero32())) return MakeError(ErrorCode::kInvalidArgument, "empty aggregator pubkey");
  const auto k = key(r.aggregator_pubkey);
  if (records_.find(k) != records_.end()) return MakeError(ErrorCode::kInvalidArgument, "aggregator already registered");
  r.registered_at = now;
  r.last_seen = now;
  records_[k] = std::move(r);
  return Result<void>{};
}

const AggregatorRecord* AggregatorRegistry::find(const std::array<std::uint8_t, 32>& pk) const {
  auto it = records_.find(key(pk));
  return it == records_.end() ? nullptr : &it->second;
}

Result<void> AggregatorRegistry::set_status(const std::array<std::uint8_t, 32>& pk, AggregatorStatus status, std::uint64_t now) {
  auto it = records_.find(key(pk));
  if (it == records_.end()) return MakeError(ErrorCode::kNotFound, "aggregator not found");
  it->second.status = status;
  it->second.last_seen = now;
  return Result<void>{};
}

std::string AggregationEngine::assignment_key(const std::array<std::uint8_t, 32>& assignment_id) {
  return to_hex(std::span<const std::uint8_t>(assignment_id.data(), assignment_id.size()));
}
std::string AggregationEngine::verifier_key(const std::array<std::uint8_t, 32>& assignment_id, const std::array<std::uint8_t, 32>& verifier_pubkey) {
  return assignment_key(assignment_id) + ":" + to_hex(std::span<const std::uint8_t>(verifier_pubkey.data(), verifier_pubkey.size()));
}
std::string AggregationEngine::cert_key(const Hash256Bytes& h) {
  return to_hex(std::span<const std::uint8_t>(h.data(), h.size()));
}

Result<void> AggregationEngine::submit_attestation(const AggregationRequest& req, const VerifierAttestation& a, std::uint64_t now) {
  if (same32(a.verifier_pubkey, zero32())) return MakeError(ErrorCode::kInvalidArgument, "empty verifier pubkey");
  if (a.assignment_id != req.assignment_id) return MakeError(ErrorCode::kInvalidArgument, "assignment mismatch");
  if (a.subject_id != req.subject_id) return MakeError(ErrorCode::kInvalidArgument, "subject mismatch");
  if (a.evidence_hash != req.evidence_hash) return MakeError(ErrorCode::kInvalidArgument, "evidence hash mismatch");
  if (a.subject_type != req.subject_type) return MakeError(ErrorCode::kInvalidArgument, "subject type mismatch");
  if (a.expires_at != 0 && now > a.expires_at) return MakeError(ErrorCode::kInvalidArgument, "attestation expired");
  if (!verify_attestation_signature(a)) return MakeError(ErrorCode::kInvalidArgument, "invalid attestation signature");

  const auto vk = verifier_key(req.assignment_id, a.verifier_pubkey);
  if (seen_verifier_.find(vk) != seen_verifier_.end()) return MakeError(ErrorCode::kInvalidArgument, "duplicate verifier attestation");

  auto& vec = by_assignment_[assignment_key(req.assignment_id)];
  // conflict detection: same verifier already handled by seen_verifier; different verifiers conflicting results are allowed as records,
  // but certificate build will require quorum for a single result.
  vec.push_back(a);
  seen_verifier_[vk] = true;
  return Result<void>{};
}

std::size_t AggregationEngine::attestation_count(const std::array<std::uint8_t, 32>& assignment_id) const {
  auto it = by_assignment_.find(assignment_key(assignment_id));
  return it == by_assignment_.end() ? 0 : it->second.size();
}

Result<VerificationCertificate> AggregationEngine::build_certificate(const AggregationRequest& req, const crypto::SecretKey& aggregator_secret,
                                                                     const std::array<std::uint8_t, 32>& aggregator_pubkey, std::uint64_t now) {
  auto it = by_assignment_.find(assignment_key(req.assignment_id));
  if (it == by_assignment_.end()) return MakeError(ErrorCode::kNotFound, "no attestations for assignment");
  const auto& all = it->second;

  // Count accepts and rejects from valid, non-expired attestations.
  std::vector<VerifierAttestation> accepts;
  std::vector<VerifierAttestation> rejects;
  for (const auto& a : all) {
    if (a.expires_at != 0 && now > a.expires_at) continue;
    if (!verify_attestation_signature(a)) continue;
    if (a.assignment_id != req.assignment_id || a.subject_id != req.subject_id || a.evidence_hash != req.evidence_hash) continue;
    if (a.result == AttestationResult::kAccept) accepts.push_back(a);
    else rejects.push_back(a);
  }

  const auto required = std::max<std::size_t>(2, req.quorum.required);
  std::vector<VerifierAttestation> selected;
  AttestationResult result = AttestationResult::kReject;
  if (accepts.size() >= required) { selected = accepts; result = AttestationResult::kAccept; }
  else if (rejects.size() >= required) { selected = rejects; result = AttestationResult::kReject; }
  else return MakeError(ErrorCode::kInvalidArgument, "insufficient quorum");

  std::sort(selected.begin(), selected.end(), [](const VerifierAttestation& a, const VerifierAttestation& b) {
    return a.verifier_pubkey < b.verifier_pubkey;
  });

  VerificationCertificate cert;
  cert.version = 1;
  cert.network_id = req.network_id;
  cert.epoch_id = req.epoch_id;
  cert.assignment_id = req.assignment_id;
  cert.subject_id = req.subject_id;
  cert.subject_type = req.subject_type;
  cert.evidence_hash = req.evidence_hash;
  cert.result = result;
  cert.quorum_required = required;
  cert.quorum_achieved = selected.size();
  cert.issued_at = now;
  cert.expires_at = now + 3600;
  cert.aggregator_pubkey = aggregator_pubkey;
  for (const auto& a : selected) {
    cert.verifier_set.push_back(a.verifier_pubkey);
    cert.attestation_hashes.push_back(attestation_hash(a));
    cert.attestations.push_back(a);
  }
  auto signed_cert = sign_certificate(std::move(cert), aggregator_secret);
  if (signed_cert.ok()) {
    auto sh = certificate_hash(signed_cert.value());
    certificates_[cert_key(sh)] = signed_cert.value();
  }
  return signed_cert;
}

Result<void> AggregationEngine::store_certificate(const VerificationCertificate& cert) {
  if (!verify_certificate(cert)) return MakeError(ErrorCode::kInvalidArgument, "invalid certificate");
  auto h = certificate_hash(cert);
  certificates_[cert_key(h)] = cert;
  return Result<void>{};
}

const VerificationCertificate* AggregationEngine::get_certificate(const Hash256Bytes& cert_hash) const {
  auto it = certificates_.find(cert_key(cert_hash));
  return it == certificates_.end() ? nullptr : &it->second;
}

std::vector<Hash256Bytes> AggregationEngine::certificate_hashes() const {
  std::vector<Hash256Bytes> out;
  out.reserve(certificates_.size());
  for (const auto& [k, c] : certificates_) out.push_back(certificate_hash(c));
  std::sort(out.begin(), out.end());
  return out;
}

bool AggregationEngine::verify_certificate(const VerificationCertificate& cert) const {
  if (cert.version != 1) return false;
  if (cert.quorum_achieved < cert.quorum_required) return false;
  if (cert.quorum_required < 2) return false;
  if (cert.verifier_set.size() != cert.attestation_hashes.size()) return false;
  if (cert.verifier_set.size() != cert.quorum_achieved) return false;
  if (!verify_certificate_signature(cert)) return false;

  // Ensure verifier set is unique and ordered
  for (std::size_t i = 1; i < cert.verifier_set.size(); ++i) {
    if (!(cert.verifier_set[i - 1] < cert.verifier_set[i])) return false;
  }

  // If full attestations included, verify them; otherwise fail closed (cannot fully verify).
  if (cert.attestations.size() != cert.verifier_set.size()) return false;
  for (std::size_t i = 0; i < cert.attestations.size(); ++i) {
    const auto& a = cert.attestations[i];
    if (a.verifier_pubkey != cert.verifier_set[i]) return false;
    if (attestation_hash(a) != cert.attestation_hashes[i]) return false;
    if (a.assignment_id != cert.assignment_id) return false;
    if (a.subject_id != cert.subject_id) return false;
    if (a.evidence_hash != cert.evidence_hash) return false;
    if (a.subject_type != cert.subject_type) return false;
    if (a.result != cert.result) return false;
    if (!verify_attestation_signature(a)) return false;
  }
  return true;
}

Result<DisputeRecord> AggregationEngine::open_dispute(const std::array<std::uint8_t, 32>& assignment_id, const std::array<std::uint8_t, 32>& reporter, std::string reason, std::uint64_t now) {
  if (same32(reporter, zero32())) return MakeError(ErrorCode::kInvalidArgument, "empty reporter");
  DisputeRecord d;
  d.assignment_id = assignment_id;
  d.reporter_pubkey = reporter;
  d.reason = std::move(reason);
  d.status = DisputeStatus::kOpen;
  d.created_at = now;
  BinaryWriter w;
  w.write_bytes(std::span<const std::uint8_t>(assignment_id.data(), assignment_id.size()));
  w.write_bytes(std::span<const std::uint8_t>(reporter.data(), reporter.size()));
  w.write_u64(now);
  w.write_string(d.reason);
  auto bytes = w.take();
  auto h = sha256d(bytes);
  std::copy(h.begin(), h.end(), d.dispute_id.begin());
  disputes_.push_back(d);
  return d;
}

Result<void> AggregationEngine::resolve_dispute(const std::array<std::uint8_t, 32>& dispute_id, DisputeStatus status) {
  for (auto& d : disputes_) {
    if (d.dispute_id == dispute_id) {
      d.status = status;
      return Result<void>{};
    }
  }
  return MakeError(ErrorCode::kNotFound, "dispute not found");
}

Result<void> AggregationEngine::register_assignment(AssignmentRecord r) {
  if (same32(r.assignment_id, zero32())) return MakeError(ErrorCode::kInvalidArgument, "empty assignment_id");
  const auto k = assignment_key(r.assignment_id);
  if (assignments_.find(k) != assignments_.end()) return MakeError(ErrorCode::kInvalidArgument, "assignment already exists");
  assignments_[k] = std::move(r);
  return Result<void>{};
}

const AssignmentRecord* AggregationEngine::get_assignment(const std::array<std::uint8_t, 32>& assignment_id) const {
  auto it = assignments_.find(assignment_key(assignment_id));
  return it == assignments_.end() ? nullptr : &it->second;
}

Result<void> AggregationEngine::rotate_assignment(const std::array<std::uint8_t, 32>& assignment_id, const std::array<std::uint8_t, 32>& new_aggregator_pubkey, std::uint64_t now) {
  auto it = assignments_.find(assignment_key(assignment_id));
  if (it == assignments_.end()) return MakeError(ErrorCode::kNotFound, "assignment not found");
  if (same32(new_aggregator_pubkey, zero32())) return MakeError(ErrorCode::kInvalidArgument, "empty aggregator pubkey");
  it->second.aggregator_pubkey = new_aggregator_pubkey;
  it->second.expires_at = now + 3600;
  it->second.active = true;
  return Result<void>{};
}

namespace {
std::string hex32(const std::array<std::uint8_t,32>& a){ return to_hex(std::span<const std::uint8_t>(a.data(),a.size())); }
bool unhex32(const std::string& s, std::array<std::uint8_t,32>& out){ std::vector<std::uint8_t> b; if(!from_hex(s,b)||b.size()!=32) return false; std::copy(b.begin(),b.end(),out.begin()); return true; }
std::string hex64(const std::array<std::uint8_t,64>& a){ return to_hex(std::span<const std::uint8_t>(a.data(),a.size())); }
bool unhex64(const std::string& s, std::array<std::uint8_t,64>& out){ std::vector<std::uint8_t> b; if(!from_hex(s,b)||b.size()!=64) return false; std::copy(b.begin(),b.end(),out.begin()); return true; }
}

Result<void> AggregationEngine::save_state(const std::string& path) const {
  std::ofstream out(path, std::ios::trunc);
  if (!out.is_open()) return MakeError(ErrorCode::kInternalError, "cannot open aggregator state for write");
  for (const auto& [k,c] : certificates_) {
    out << "cert=" << hex32(certificate_hash(c)) << "|" << c.version << "|" << c.network_id << "|" << c.epoch_id
        << "|" << hex32(c.assignment_id) << "|" << hex32(c.subject_id) << "|" << static_cast<int>(c.subject_type)
        << "|" << hex32(c.evidence_hash) << "|" << static_cast<int>(c.result) << "|" << c.quorum_required
        << "|" << c.quorum_achieved << "|" << c.issued_at << "|" << c.expires_at
        << "|" << hex32(c.aggregator_pubkey) << "|" << hex64(c.aggregator_signature)
        << "|" << c.verifier_set.size();
    for (size_t i=0;i<c.verifier_set.size();++i) {
      out << "|" << hex32(c.verifier_set[i]) << "|" << hex32(c.attestation_hashes[i]);
    }
    out << "|" << c.attestations.size();
    for (const auto& a : c.attestations) {
      out << "|" << hex32(a.verifier_pubkey) << "|" << hex32(a.assignment_id) << "|" << hex32(a.subject_id)
          << "|" << hex32(a.evidence_hash) << "|" << static_cast<int>(a.subject_type) << "|" << static_cast<int>(a.result)
          << "|" << a.timestamp << "|" << a.expires_at << "|" << hex64(a.signature);
    }
    out << "\n";
  }
  return Result<void>{};
}

Result<void> AggregationEngine::load_state(const std::string& path) {
  std::ifstream in(path);
  if (!in.is_open()) return MakeError(ErrorCode::kNotFound, "aggregator state not found");
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("cert=",0)!=0) continue;
    std::stringstream ss(line.substr(5));
    std::vector<std::string> parts; std::string pstr;
    while (std::getline(ss,pstr,'|')) parts.push_back(pstr);
    if (parts.size() < 16) continue;
    VerificationCertificate c;
    c.version = static_cast<std::uint32_t>(std::stoul(parts[1]));
    c.network_id = parts[2];
    c.epoch_id = std::stoull(parts[3]);
    if(!unhex32(parts[4],c.assignment_id)||!unhex32(parts[5],c.subject_id)) continue;
    c.subject_type = static_cast<SubjectType>(std::stoi(parts[6]));
    if(!unhex32(parts[7],c.evidence_hash)) continue;
    c.result = static_cast<AttestationResult>(std::stoi(parts[8]));
    c.quorum_required = static_cast<std::size_t>(std::stoull(parts[9]));
    c.quorum_achieved = static_cast<std::size_t>(std::stoull(parts[10]));
    c.issued_at = std::stoull(parts[11]);
    c.expires_at = std::stoull(parts[12]);
    if(!unhex32(parts[13],c.aggregator_pubkey)||!unhex64(parts[14],c.aggregator_signature)) continue;
    size_t n = static_cast<std::size_t>(std::stoull(parts[15]));
    size_t idx=16;
    bool ok=true;
    for(size_t i=0;i<n && idx+1<parts.size();++i,idx+=2){
      std::array<std::uint8_t,32> v{},ah{};
      if(!unhex32(parts[idx],v)||!unhex32(parts[idx+1],ah)){ ok=false; break; }
      c.verifier_set.push_back(v); c.attestation_hashes.push_back(ah);
    }
    if(!ok) continue;
    if (idx < parts.size()) {
      size_t an = static_cast<size_t>(std::stoull(parts[idx++])); 
      for(size_t i=0;i<an && idx+8<parts.size();++i,idx+=9){
        VerifierAttestation a;
        if(!unhex32(parts[idx],a.verifier_pubkey)||!unhex32(parts[idx+1],a.assignment_id)||!unhex32(parts[idx+2],a.subject_id)||!unhex32(parts[idx+3],a.evidence_hash)||!unhex64(parts[idx+8],a.signature)){ ok=false; break; }
        a.subject_type = static_cast<SubjectType>(std::stoi(parts[idx+4]));
        a.result = static_cast<AttestationResult>(std::stoi(parts[idx+5]));
        a.timestamp = std::stoull(parts[idx+6]);
        a.expires_at = std::stoull(parts[idx+7]);
        c.attestations.push_back(a);
      }
    }
    if(!ok) continue;
    auto h = certificate_hash(c);
    certificates_[cert_key(h)] = c;
  }
  return Result<void>{};
}

Result<void> AggregationEngine::save_state_db(const std::string& path) const {
  auto db = phantomx::db::SqliteDb::open(path);
  if (!db.ok()) return db.error();
  auto c1 = db.value().exec("CREATE TABLE IF NOT EXISTS certificates(cert_hash TEXT PRIMARY KEY, cert_line TEXT NOT NULL, assignment_id TEXT, subject_id TEXT, result INTEGER, issued_at INTEGER);");
  if (!c1.ok()) return c1;
  (void)db.value().exec("CREATE INDEX IF NOT EXISTS idx_cert_assignment ON certificates(assignment_id);");
  (void)db.value().exec("CREATE INDEX IF NOT EXISTS idx_cert_subject ON certificates(subject_id);");
  if (!c1.ok()) return c1;
  auto c2 = db.value().exec("CREATE INDEX IF NOT EXISTS idx_cert_assignment ON certificates(assignment_id);");
  if (!c2.ok()) return c2;
  auto c3 = db.value().exec("CREATE INDEX IF NOT EXISTS idx_cert_subject ON certificates(subject_id);");
  if (!c1.ok()) return c1;
  auto tx = db.value().begin(); if (!tx.ok()) return tx;
  for (const auto& [k,c] : certificates_) {
    std::ostringstream out;
    out << hex32(certificate_hash(c)) << "|" << c.version << "|" << c.network_id << "|" << c.epoch_id
        << "|" << hex32(c.assignment_id) << "|" << hex32(c.subject_id) << "|" << static_cast<int>(c.subject_type)
        << "|" << hex32(c.evidence_hash) << "|" << static_cast<int>(c.result) << "|" << c.quorum_required
        << "|" << c.quorum_achieved << "|" << c.issued_at << "|" << c.expires_at
        << "|" << hex32(c.aggregator_pubkey) << "|" << hex64(c.aggregator_signature)
        << "|" << c.verifier_set.size();
    for (size_t i=0;i<c.verifier_set.size();++i) out << "|" << hex32(c.verifier_set[i]) << "|" << hex32(c.attestation_hashes[i]);
    out << "|" << c.attestations.size();
    for (const auto& a : c.attestations) {
      out << "|" << hex32(a.verifier_pubkey) << "|" << hex32(a.assignment_id) << "|" << hex32(a.subject_id)
          << "|" << hex32(a.evidence_hash) << "|" << static_cast<int>(a.subject_type) << "|" << static_cast<int>(a.result)
          << "|" << a.timestamp << "|" << a.expires_at << "|" << hex64(a.signature);
    }
    auto st = db.value().prepare("INSERT OR REPLACE INTO certificates(cert_hash,cert_line,assignment_id,subject_id,result,issued_at) VALUES(?,?,?,?,?,?);");
    if (!st.ok()) { (void)db.value().rollback(); return st.error(); }
    auto hhex = hex32(certificate_hash(c));
    auto line = out.str();
    (void)st.value().bind_text(1, hhex);
    (void)st.value().bind_text(2, line);
    (void)st.value().bind_text(3, hex32(c.assignment_id));
    (void)st.value().bind_text(4, hex32(c.subject_id));
    (void)st.value().bind_int(5, static_cast<std::int64_t>(c.result));
    (void)st.value().bind_u64(6, c.issued_at);
    auto ex = st.value().execute();
    if (!ex.ok()) { (void)db.value().rollback(); return ex; }
  }
  auto cm = db.value().commit(); if (!cm.ok()) return cm;
  return Result<void>{};
}

Result<void> AggregationEngine::load_state_db(const std::string& path) {
  auto db = phantomx::db::SqliteDb::open(path);
  if (!db.ok()) return db.error();
  auto c1 = db.value().exec("CREATE TABLE IF NOT EXISTS certificates(cert_hash TEXT PRIMARY KEY, cert_line TEXT NOT NULL, assignment_id TEXT, subject_id TEXT, result INTEGER, issued_at INTEGER);");
  if (!c1.ok()) return c1;
  auto st = db.value().prepare("SELECT cert_line FROM certificates;");
  if (!st.ok()) return st.error();
  while (true) {
    auto row = st.value().step();
    if (!row.ok()) return row.error();
    if (!row.value()) break;
    std::string line = st.value().column_text(0);
    // reuse parsing by loading through temp logic: parse line directly
    std::stringstream ss(line);
    std::vector<std::string> parts; std::string pstr;
    while (std::getline(ss,pstr,'|')) parts.push_back(pstr);
    if (parts.size() < 16) continue;
    VerificationCertificate c;
    c.version = static_cast<std::uint32_t>(std::stoul(parts[1]));
    c.network_id = parts[2];
    c.epoch_id = std::stoull(parts[3]);
    if(!unhex32(parts[4],c.assignment_id)||!unhex32(parts[5],c.subject_id)) continue;
    c.subject_type = static_cast<SubjectType>(std::stoi(parts[6]));
    if(!unhex32(parts[7],c.evidence_hash)) continue;
    c.result = static_cast<AttestationResult>(std::stoi(parts[8]));
    c.quorum_required = static_cast<std::size_t>(std::stoull(parts[9]));
    c.quorum_achieved = static_cast<std::size_t>(std::stoull(parts[10]));
    c.issued_at = std::stoull(parts[11]);
    c.expires_at = std::stoull(parts[12]);
    if(!unhex32(parts[13],c.aggregator_pubkey)||!unhex64(parts[14],c.aggregator_signature)) continue;
    size_t n = static_cast<std::size_t>(std::stoull(parts[15]));
    size_t idx=16; bool ok=true;
    for(size_t i=0;i<n && idx+1<parts.size();++i,idx+=2){ std::array<std::uint8_t,32> v{},ah{}; if(!unhex32(parts[idx],v)||!unhex32(parts[idx+1],ah)){ ok=false; break; } c.verifier_set.push_back(v); c.attestation_hashes.push_back(ah); }
    if(!ok) continue;
    if (idx < parts.size()) { size_t an = static_cast<size_t>(std::stoull(parts[idx++])); for(size_t i=0;i<an && idx+8<parts.size();++i,idx+=9){ VerifierAttestation a; if(!unhex32(parts[idx],a.verifier_pubkey)||!unhex32(parts[idx+1],a.assignment_id)||!unhex32(parts[idx+2],a.subject_id)||!unhex32(parts[idx+3],a.evidence_hash)||!unhex64(parts[idx+8],a.signature)){ ok=false; break; } a.subject_type=static_cast<SubjectType>(std::stoi(parts[idx+4])); a.result=static_cast<AttestationResult>(std::stoi(parts[idx+5])); a.timestamp=std::stoull(parts[idx+6]); a.expires_at=std::stoull(parts[idx+7]); c.attestations.push_back(a);} }
    auto hh = certificate_hash(c);
    certificates_[cert_key(hh)] = c;
  }
  return Result<void>{};
}

}  // namespace phantomx::aggregator
