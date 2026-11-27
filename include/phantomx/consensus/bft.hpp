#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "phantomx/common/crypto.hpp"
#include "phantomx/common/error.hpp"
#include "phantomx/consensus/types.hpp"

namespace phantomx::consensus {

struct EquivocationEvidence {
  std::array<std::uint8_t, 32> validator_pubkey{};
  VotePhase phase = VotePhase::kPrevote;
  std::uint64_t epoch = 0;
  std::uint64_t view = 0;
  Hash256Bytes hash_a{};
  Hash256Bytes hash_b{};
};

struct BftState {
  std::uint64_t epoch = 0;
  std::uint64_t view = 0;
  std::uint32_t height = 0;
  std::optional<Hash256Bytes> locked_hash;
  std::uint32_t locked_height = 0;
  std::uint64_t locked_view = 0;
  std::optional<QuorumCertificate> valid_qc;
  std::uint64_t valid_view = 0;
  std::optional<FinalityCertificate> last_finalized;
};

struct PacemakerConfig {
  std::uint64_t base_timeout_ms = 3000;
  std::uint64_t max_timeout_ms = 60000;
  std::uint64_t backoff_numerator = 3;
  std::uint64_t backoff_denominator = 2;
};

struct PacemakerState {
  std::uint64_t last_progress_ms = 0;
  std::uint64_t timeout_ms = 3000;
  std::uint64_t timeouts_in_view = 0;
  bool armed = false;
};

struct RandomnessCommit {
  std::uint64_t epoch = 0;
  std::uint32_t height = 0;
  std::array<std::uint8_t, 32> validator_pubkey{};
  std::array<std::uint8_t, 32> commitment{}; // sha256d(reveal || pubkey || epoch_le)
  std::array<std::uint8_t, 64> signature{};
};

struct RandomnessReveal {
  std::uint64_t epoch = 0;
  std::array<std::uint8_t, 32> validator_pubkey{};
  std::array<std::uint8_t, 32> reveal{};
  std::array<std::uint8_t, 64> signature{};
};

enum class SlashingReason : std::uint8_t {
  kNone = 0,
  kVoteEquivocation = 1,
  kViewChangeEquivocation = 2,
  kInvalidProposal = 3,
  kRandomnessWithholding = 4,
};

struct SlashingRecord {
  std::array<std::uint8_t, 32> validator_pubkey{};
  SlashingReason reason = SlashingReason::kNone;
  std::uint64_t epoch = 0;
  std::uint64_t view = 0;
  std::uint32_t height = 0;
  std::uint64_t penalty_atoms = 0;
  bool appealed = false;
  bool appeal_resolved = false;
  bool appeal_accepted = false;
  bool settled = false;
  std::string settlement_ref;
  std::string evidence_hex;
};

[[nodiscard]] std::uint64_t quorum_power(const ValidatorSet& vs);
[[nodiscard]] bool has_quorum(const ValidatorSet& vs, std::uint64_t signed_power);
[[nodiscard]] const Validator* find_validator(const ValidatorSet& vs, const std::array<std::uint8_t, 32>& pubkey);
[[nodiscard]] std::array<std::uint8_t, 32> select_proposer(const ValidatorSet& vs, std::uint64_t epoch, std::uint64_t view);

[[nodiscard]] std::vector<std::uint8_t> serialize_vote(const Vote& v);
[[nodiscard]] Result<Vote> deserialize_vote(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::vector<std::uint8_t> serialize_proposal(const BlockProposal& p);
[[nodiscard]] Result<BlockProposal> deserialize_proposal(std::span<const std::uint8_t> bytes);
[[nodiscard]] Result<BlockProposal> sign_proposal(BlockProposal p, const crypto::SecretKey& sk);
[[nodiscard]] bool verify_proposal(const ValidatorSet& vs, const BlockProposal& p);
[[nodiscard]] std::vector<std::uint8_t> serialize_qc(const QuorumCertificate& qc);
[[nodiscard]] Result<QuorumCertificate> deserialize_qc(std::span<const std::uint8_t> bytes);
[[nodiscard]] Hash256Bytes vote_hash(const Vote& v);
[[nodiscard]] Result<Vote> sign_vote(Vote v, const crypto::SecretKey& sk);
[[nodiscard]] bool verify_vote(const ValidatorSet& vs, const Vote& v);

[[nodiscard]] Result<QuorumCertificate> build_qc(const ValidatorSet& vs, const std::vector<Vote>& votes);
[[nodiscard]] bool verify_qc(const ValidatorSet& vs, const QuorumCertificate& qc);

[[nodiscard]] std::vector<std::uint8_t> serialize_view_change(const ViewChange& v);
[[nodiscard]] Result<ViewChange> deserialize_view_change(std::span<const std::uint8_t> bytes);
[[nodiscard]] Hash256Bytes view_change_hash(const ViewChange& v);
[[nodiscard]] Result<ViewChange> sign_view_change(ViewChange v, const crypto::SecretKey& sk);
[[nodiscard]] bool verify_view_change(const ValidatorSet& vs, const ViewChange& v);

[[nodiscard]] std::vector<std::uint8_t> serialize_view_change_qc(const ViewChangeQC& qc);
[[nodiscard]] Result<ViewChangeQC> deserialize_view_change_qc(std::span<const std::uint8_t> bytes);
[[nodiscard]] bool verify_view_change_qc(const ValidatorSet& vs, const ViewChangeQC& qc);
// Deterministically selects the NewView issuer from the validators that signed
// the ViewChangeQC. If the nominal next-view proposer crashed, the quorum that
// timed out can still rotate to a live signer.
[[nodiscard]] std::array<std::uint8_t, 32> select_new_view_proposer(const ValidatorSet& vs, const ViewChangeQC& qc);

[[nodiscard]] std::vector<std::uint8_t> serialize_new_view(const NewView& nv);
[[nodiscard]] Result<NewView> deserialize_new_view(std::span<const std::uint8_t> bytes);
[[nodiscard]] Result<NewView> sign_new_view(NewView nv, const crypto::SecretKey& sk);
[[nodiscard]] bool verify_new_view(const ValidatorSet& vs, const NewView& nv);

class BftEngine {
 public:
  explicit BftEngine(ValidatorSet validators) : validators_(std::move(validators)) {}

  [[nodiscard]] const BftState& state() const noexcept { return state_; }
  [[nodiscard]] const ValidatorSet& validators() const noexcept { return validators_; }
  [[nodiscard]] const std::vector<EquivocationEvidence>& equivocations() const noexcept { return equivocations_; }
  [[nodiscard]] const std::unordered_set<std::string>& slashed_validators() const noexcept { return slashed_validators_; }
  [[nodiscard]] bool is_slashed(const std::array<std::uint8_t, 32>& validator_pubkey) const;
  [[nodiscard]] const std::unordered_map<std::string, Vote>& votes() const noexcept { return votes_; }
  [[nodiscard]] const std::unordered_map<std::string, ViewChange>& view_changes() const noexcept { return view_changes_; }
  [[nodiscard]] std::uint64_t view_change_equivocations() const noexcept { return view_change_equivocations_; }

  [[nodiscard]] Result<void> submit_vote(const Vote& vote);
  [[nodiscard]] std::optional<QuorumCertificate> maybe_qc(VotePhase phase, std::uint64_t epoch, std::uint64_t view, const Hash256Bytes& block_hash) const;
  [[nodiscard]] Result<void> observe_qc(const QuorumCertificate& qc);
  [[nodiscard]] Result<void> finalize(const FinalityCertificate& cert);
  [[nodiscard]] Result<void> timeout(std::uint64_t now);
  void set_pacemaker_config(PacemakerConfig cfg);
  void note_progress(std::uint64_t now_ms);
  [[nodiscard]] bool should_timeout(std::uint64_t now_ms) const;
  [[nodiscard]] std::uint64_t pacemaker_timeout_ms() const noexcept { return pacemaker_.timeout_ms; }
  [[nodiscard]] const PacemakerState& pacemaker() const noexcept { return pacemaker_; }
  [[nodiscard]] Result<void> submit_randomness_commit(const RandomnessCommit& c);
  [[nodiscard]] Result<void> submit_randomness_reveal(const RandomnessReveal& r);
  [[nodiscard]] std::optional<Hash256Bytes> randomness_beacon(std::uint64_t epoch) const;
  void set_randomness_reveal_deadline_blocks(std::uint32_t blocks) { randomness_reveal_deadline_blocks_ = blocks; }
  [[nodiscard]] std::uint32_t randomness_reveal_deadline_blocks() const noexcept { return randomness_reveal_deadline_blocks_; }
  [[nodiscard]] Result<void> enforce_randomness_deadline(std::uint32_t current_height);
  [[nodiscard]] const std::vector<SlashingRecord>& slashing_records() const noexcept { return slashing_records_; }
  [[nodiscard]] Result<void> record_slashing(const std::array<std::uint8_t, 32>& validator_pubkey, SlashingReason reason, std::uint64_t epoch, std::uint64_t view, std::uint32_t height, std::uint64_t penalty_atoms, std::string evidence_hex);
  [[nodiscard]] Result<void> appeal_slashing(std::size_t index, const std::string& evidence_hex);
  [[nodiscard]] Result<void> resolve_appeal(std::size_t index, bool accepted);
  [[nodiscard]] Result<void> settle_slashing(std::size_t index, const std::string& settlement_ref);
  [[nodiscard]] std::uint64_t total_slashed_penalty_atoms() const;
  [[nodiscard]] std::uint64_t unsettled_slashed_penalty_atoms() const;
  [[nodiscard]] Result<void> submit_view_change(const ViewChange& vc);
  [[nodiscard]] std::optional<std::uint64_t> maybe_advance_view_from_view_changes() const;
  [[nodiscard]] std::optional<ViewChangeQC> maybe_view_change_qc() const;
  // Enforces the cross-view lock rule: a proposal may only carry a different
  // hash than the locked one when it presents a valid prevote QC from a later view.
  [[nodiscard]] Result<void> check_proposal_lock(const BlockProposal& proposal) const;
  [[nodiscard]] Result<void> enter_view(std::uint64_t new_view);
  void rotate_validators(ValidatorSet next);
  [[nodiscard]] Result<void> save_state(const std::string& path) const;
  [[nodiscard]] Result<void> load_state(const std::string& path);

 private:
  static std::string vote_key(const Vote& v);
  static std::string view_change_key(const ViewChange& v);
  ValidatorSet validators_;
  BftState state_;
  std::unordered_map<std::string, Vote> votes_; // by validator+phase+epoch+view
  std::unordered_map<std::string, ViewChange> view_changes_;
  std::vector<EquivocationEvidence> equivocations_;
  std::unordered_set<std::string> slashed_validators_;
  std::uint64_t view_change_equivocations_ = 0;
  PacemakerConfig pacemaker_cfg_{};
  PacemakerState pacemaker_{};
  std::map<std::string, RandomnessCommit> randomness_commits_; // key: epoch:pubhex
  std::map<std::string, RandomnessReveal> randomness_reveals_; // key: epoch:pubhex
  std::vector<SlashingRecord> slashing_records_;
  std::uint32_t randomness_reveal_deadline_blocks_ = 0;
};

}  // namespace phantomx::consensus
