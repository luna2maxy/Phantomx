#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "phantomx/common/crypto.hpp"
#include "phantomx/common/error.hpp"
#include "phantomx/primitives/hash.hpp"

namespace phantomx::aggregator {

enum class AggregatorStatus : std::uint8_t { kActive = 1, kOffline = 2, kSuspended = 3, kBanned = 4 };
enum class SubjectType : std::uint8_t { kPoiEvidence = 1, kTransaction = 2, kBlock = 3 };
enum class AttestationResult : std::uint8_t { kReject = 0, kAccept = 1 };

struct AggregatorRecord {
  std::array<std::uint8_t, 32> aggregator_pubkey{};
  std::array<std::uint8_t, 32> operator_pubkey{};
  AggregatorStatus status = AggregatorStatus::kActive;
  std::string endpoint;
  std::uint64_t registered_at = 0;
  std::uint64_t last_seen = 0;
  std::int64_t reputation = 0;
};

struct VerifierAttestation {
  std::array<std::uint8_t, 32> verifier_pubkey{};
  std::array<std::uint8_t, 32> assignment_id{};
  std::array<std::uint8_t, 32> subject_id{};
  Hash256Bytes evidence_hash{};
  SubjectType subject_type = SubjectType::kPoiEvidence;
  AttestationResult result = AttestationResult::kReject;
  std::uint64_t timestamp = 0;
  std::uint64_t expires_at = 0;
  std::array<std::uint8_t, 64> signature{};
};

struct QuorumPolicy {
  std::size_t required = 2; // minimum independent verifier attestations
};

struct AggregationRequest {
  std::string network_id;
  std::uint64_t epoch_id = 0;
  std::array<std::uint8_t, 32> assignment_id{};
  std::array<std::uint8_t, 32> subject_id{};
  SubjectType subject_type = SubjectType::kPoiEvidence;
  Hash256Bytes evidence_hash{};
  QuorumPolicy quorum;
};

struct VerificationCertificate {
  std::uint32_t version = 1;
  std::string network_id;
  std::uint64_t epoch_id = 0;
  std::array<std::uint8_t, 32> assignment_id{};
  std::array<std::uint8_t, 32> subject_id{};
  SubjectType subject_type = SubjectType::kPoiEvidence;
  Hash256Bytes evidence_hash{};
  std::vector<std::array<std::uint8_t, 32>> verifier_set; // ordered unique
  std::vector<Hash256Bytes> attestation_hashes;            // ordered, aligned with verifier_set
  std::vector<VerifierAttestation> attestations;           // optional full attestations
  AttestationResult result = AttestationResult::kReject;
  std::size_t quorum_required = 0;
  std::size_t quorum_achieved = 0;
  std::uint64_t issued_at = 0;
  std::uint64_t expires_at = 0;
  std::array<std::uint8_t, 32> aggregator_pubkey{};
  std::array<std::uint8_t, 64> aggregator_signature{};
};

enum class DisputeStatus : std::uint8_t { kOpen = 1, kAccepted = 2, kRejected = 3 };
struct DisputeRecord {
  std::array<std::uint8_t, 32> dispute_id{};
  std::array<std::uint8_t, 32> assignment_id{};
  std::array<std::uint8_t, 32> reporter_pubkey{};
  std::string reason;
  DisputeStatus status = DisputeStatus::kOpen;
  std::uint64_t created_at = 0;
};

struct AssignmentRecord {
  std::array<std::uint8_t, 32> assignment_id{};
  std::array<std::uint8_t, 32> subject_id{};
  std::vector<std::array<std::uint8_t, 32>> verifiers;
  std::array<std::uint8_t, 32> aggregator_pubkey{};
  std::uint64_t created_at = 0;
  std::uint64_t expires_at = 0;
  bool active = true;
};

[[nodiscard]] std::vector<std::uint8_t> serialize_attestation(const VerifierAttestation& a);
[[nodiscard]] Hash256Bytes attestation_hash(const VerifierAttestation& a);
[[nodiscard]] bool verify_attestation_signature(const VerifierAttestation& a);

[[nodiscard]] std::vector<std::uint8_t> serialize_certificate_body(const VerificationCertificate& c);
[[nodiscard]] Hash256Bytes certificate_hash(const VerificationCertificate& c);
[[nodiscard]] Result<VerificationCertificate> sign_certificate(VerificationCertificate c, const crypto::SecretKey& aggregator_secret);
[[nodiscard]] bool verify_certificate_signature(const VerificationCertificate& c);

class AggregatorRegistry {
 public:
  [[nodiscard]] Result<void> register_aggregator(AggregatorRecord r, std::uint64_t now);
  [[nodiscard]] const AggregatorRecord* find(const std::array<std::uint8_t, 32>& pk) const;
  [[nodiscard]] Result<void> set_status(const std::array<std::uint8_t, 32>& pk, AggregatorStatus status, std::uint64_t now);
 private:
  static std::string key(const std::array<std::uint8_t, 32>& pk);
  std::unordered_map<std::string, AggregatorRecord> records_;
};

class AggregationEngine {
 public:
  [[nodiscard]] Result<void> submit_attestation(const AggregationRequest& req, const VerifierAttestation& a, std::uint64_t now);
  [[nodiscard]] Result<VerificationCertificate> build_certificate(const AggregationRequest& req, const crypto::SecretKey& aggregator_secret,
                                                                const std::array<std::uint8_t, 32>& aggregator_pubkey, std::uint64_t now);
  [[nodiscard]] bool verify_certificate(const VerificationCertificate& cert) const;
  [[nodiscard]] std::size_t attestation_count(const std::array<std::uint8_t, 32>& assignment_id) const;
  [[nodiscard]] Result<void> store_certificate(const VerificationCertificate& cert);
  [[nodiscard]] const VerificationCertificate* get_certificate(const Hash256Bytes& cert_hash) const;
  [[nodiscard]] std::vector<Hash256Bytes> certificate_hashes() const;
  [[nodiscard]] Result<DisputeRecord> open_dispute(const std::array<std::uint8_t, 32>& assignment_id, const std::array<std::uint8_t, 32>& reporter, std::string reason, std::uint64_t now);
  [[nodiscard]] Result<void> resolve_dispute(const std::array<std::uint8_t, 32>& dispute_id, DisputeStatus status);
  [[nodiscard]] const std::vector<DisputeRecord>& disputes() const noexcept { return disputes_; }
  [[nodiscard]] Result<void> register_assignment(AssignmentRecord r);
  [[nodiscard]] const AssignmentRecord* get_assignment(const std::array<std::uint8_t, 32>& assignment_id) const;
  [[nodiscard]] Result<void> rotate_assignment(const std::array<std::uint8_t, 32>& assignment_id, const std::array<std::uint8_t, 32>& new_aggregator_pubkey, std::uint64_t now);
  [[nodiscard]] Result<void> save_state(const std::string& path) const;
  [[nodiscard]] Result<void> load_state(const std::string& path);
  [[nodiscard]] Result<void> save_state_db(const std::string& path) const;
  [[nodiscard]] Result<void> load_state_db(const std::string& path);

  AggregatorRegistry& registry() noexcept { return registry_; }
  [[nodiscard]] const AggregatorRegistry& registry() const noexcept { return registry_; }

 private:
  static std::string assignment_key(const std::array<std::uint8_t, 32>& assignment_id);
  static std::string verifier_key(const std::array<std::uint8_t, 32>& assignment_id, const std::array<std::uint8_t, 32>& verifier_pubkey);
  static std::string cert_key(const Hash256Bytes& h);
  std::unordered_map<std::string, std::vector<VerifierAttestation>> by_assignment_;
  std::unordered_map<std::string, bool> seen_verifier_;
  std::unordered_map<std::string, VerificationCertificate> certificates_;
  std::vector<DisputeRecord> disputes_;
  std::unordered_map<std::string, AssignmentRecord> assignments_;
  AggregatorRegistry registry_;
};

}  // namespace phantomx::aggregator
