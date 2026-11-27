#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "phantomx/blockchain/chain.hpp"
#include "phantomx/consensus/types.hpp"
#include "phantomx/common/error.hpp"
#include "phantomx/network/peer.hpp"
#include "phantomx/network/protocol.hpp"
#include "phantomx/utxo/mempool.hpp"

namespace phantomx::network {

using BftProposalCallback = std::function<void(const consensus::BlockProposal&)>;
using BftVoteCallback = std::function<void(const consensus::Vote&)>;
using BftQcCallback = std::function<void(const consensus::QuorumCertificate&)>;
using BftFinalityCallback = std::function<void(const consensus::FinalityCertificate&)>;
using BftViewChangeCallback = std::function<void(const consensus::ViewChange&)>;
using BftNewViewCallback = std::function<void(const consensus::NewView&)>;

struct NetworkConfig {
  std::uint32_t network_magic = 0x52505801u;
  std::uint32_t protocol_version = kP2PProtocolVersion;
  Hash256Bytes genesis_hash{};
  std::string listen_host = "127.0.0.1";
  std::uint16_t listen_port = 0; // 0 = ephemeral
  std::size_t max_connections = kMaxConnections;
  std::size_t max_inbound = kMaxInboundConnections;
  std::uint32_t handshake_timeout_ms = kHandshakeTimeoutMs;
  std::uint32_t idle_timeout_ms = kIdleTimeoutMs;
  std::uint32_t reconnect_interval_ms = 500;
  std::size_t max_message_size = kMaxMessageSize;
  std::uint32_t rate_limit_per_sec = 500;
  std::vector<NetAddress> seed_peers;
  std::string agent = "phantomx-p2p/0.1";
  BftProposalCallback on_bft_proposal;
  BftVoteCallback on_bft_vote;
  BftQcCallback on_bft_qc;
  BftFinalityCallback on_bft_finality;
  BftViewChangeCallback on_bft_view_change;
  BftNewViewCallback on_bft_new_view;
};

class P2PNode {
 public:
  P2PNode(NetworkConfig config, blockchain::ChainDatabase* chain, Mempool* mempool);
  ~P2PNode();

  P2PNode(const P2PNode&) = delete;
  P2PNode& operator=(const P2PNode&) = delete;

  [[nodiscard]] Result<void> start();
  void stop();
  [[nodiscard]] bool running() const noexcept { return running_.load(); }

  [[nodiscard]] Result<void> connect(const std::string& host, std::uint16_t port);
  [[nodiscard]] Result<void> broadcast_tx(const Transaction& tx);
  [[nodiscard]] Result<void> broadcast_block(const Block& block);
  [[nodiscard]] Result<void> broadcast_bft_block(const Block& block);
  [[nodiscard]] Result<void> broadcast_bft_proposal(const consensus::BlockProposal& proposal);
  [[nodiscard]] Result<void> broadcast_bft_vote(const consensus::Vote& vote);
  [[nodiscard]] Result<void> broadcast_bft_qc(const consensus::QuorumCertificate& qc);
  [[nodiscard]] Result<void> broadcast_bft_finality(const consensus::FinalityCertificate& cert);
  [[nodiscard]] Result<void> broadcast_bft_view_change(const consensus::ViewChange& vc);
  [[nodiscard]] Result<void> broadcast_bft_new_view(const consensus::NewView& nv);
  [[nodiscard]] Result<void> broadcast_poi_attestation(const std::string& payload);
  void retry_pending_poi_attestations(std::uint64_t now_ms, std::uint64_t retry_interval_ms = 5000, std::uint32_t max_retries = 12);
  void set_poi_attestation_handler(std::function<void(const std::string&)> h);
  void set_bft_proposal_callback(BftProposalCallback cb) { config_.on_bft_proposal = std::move(cb); }
  void set_bft_vote_callback(BftVoteCallback cb) { config_.on_bft_vote = std::move(cb); }
  void set_bft_qc_callback(BftQcCallback cb) { config_.on_bft_qc = std::move(cb); }
  void set_bft_finality_callback(BftFinalityCallback cb) { config_.on_bft_finality = std::move(cb); }
  void set_bft_view_change_callback(BftViewChangeCallback cb) { config_.on_bft_view_change = std::move(cb); }
  void set_bft_new_view_callback(BftNewViewCallback cb) { config_.on_bft_new_view = std::move(cb); }
  [[nodiscard]] Result<void> request_sync();

  [[nodiscard]] std::uint16_t listen_port() const;
  [[nodiscard]] std::vector<PeerInfo> peers() const;
  [[nodiscard]] std::size_t peer_count() const;
  [[nodiscard]] std::size_t ready_peer_count() const;
  [[nodiscard]] double average_latency_ms() const;  // -1 if no peers measured
  [[nodiscard]] std::uint64_t relay_message_count() const;
  [[nodiscard]] bool is_banned(const std::string& host, std::uint16_t port) const;

 private:
  struct PeerConn;
  using PeerPtr = std::shared_ptr<PeerConn>;

  [[nodiscard]] Result<void> setup_listener();
  void accept_loop();
  void peer_loop(PeerPtr peer);
  void maintenance_loop();

  [[nodiscard]] Result<void> send_message(PeerPtr peer, const Message& msg);
  [[nodiscard]] Result<Message> read_message(PeerPtr peer);
  void handle_message(PeerPtr peer, const Message& msg);
  void handle_hello(PeerPtr peer, const HelloMessage& hello);
  void handle_inv(PeerPtr peer, const std::vector<InventoryItem>& items);
  void handle_get_data(PeerPtr peer, const std::vector<InventoryItem>& items);
  void handle_get_headers(PeerPtr peer);
  void handle_headers(PeerPtr peer, const std::vector<Hash256Bytes>& hashes);
  void handle_addr(PeerPtr peer, const std::vector<NetAddress>& addrs);

  void add_peer(PeerPtr peer);
  void remove_peer(std::uint64_t id, std::string_view reason);
  void score_peer(PeerPtr peer, int delta, bool invalid = false, bool timeout = false, bool spam = false);
  void maybe_ban(PeerPtr peer, std::string_view reason);
  bool check_rate_limit(PeerPtr peer);
  bool seen_or_mark(const Hash256Bytes& hash);
  [[nodiscard]] std::vector<NetAddress> known_addresses() const;
  void remember_address(const NetAddress& addr);

  NetworkConfig config_;
  blockchain::ChainDatabase* chain_ = nullptr;
  Mempool* mempool_ = nullptr;

  mutable std::mutex mu_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> next_peer_id_{1};
  std::atomic<std::uint64_t> relay_message_count_{0};
  std::uint64_t local_nonce_ = 0;

  std::thread accept_thread_;
  std::thread maintenance_thread_;
  std::unordered_map<std::uint64_t, PeerPtr> peers_;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> banned_;
  std::unordered_set<std::string> known_addr_keys_;
  std::vector<NetAddress> known_addrs_;
  std::unordered_set<std::string> seen_hashes_;
  std::vector<std::thread> zombie_threads_;
  std::function<void(const std::string&)> poi_attestation_handler_;
  struct PendingPoiAttestation {
    std::string payload;
    std::uint64_t last_sent_ms = 0;
    std::uint32_t retries = 0;
  };
  std::unordered_map<std::string, PendingPoiAttestation> pending_poi_attestations_;

  std::intptr_t listen_fd_ = -1;
  std::uint16_t bound_port_ = 0;
};

}  // namespace phantomx::network
