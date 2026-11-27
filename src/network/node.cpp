#include "phantomx/network/node.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>

#include "phantomx/common/serialization.hpp"

#include "phantomx/network/socket_io.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "phantomx/blockchain/genesis.hpp"
#include "phantomx/primitives/block.hpp"
#include "phantomx/primitives/hash.hpp"

namespace phantomx::network {
namespace {

std::string addr_key(const std::string& host, std::uint16_t port) { return host + ":" + std::to_string(port); }

void close_socket(socket_t s) {
  if (s == kInvalidSocket) return;
#ifdef _WIN32
  closesocket(s);
#else
  ::close(s);
#endif
}

bool set_recv_timeout(socket_t s, std::uint32_t ms) {
#ifdef _WIN32
  DWORD t = ms;
  return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof(t)) == 0;
#else
  timeval tv{};
  tv.tv_sec = static_cast<long>(ms / 1000);
  tv.tv_usec = static_cast<decltype(tv.tv_usec)>((ms % 1000) * 1000);
  return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

std::uint64_t random_nonce() {
  static std::mt19937_64 rng{std::random_device{}()};
  static std::mutex m;
  std::scoped_lock lock(m);
  return rng();
}

#ifdef _WIN32
struct WsaInit {
  WsaInit() { WSADATA w{}; WSAStartup(MAKEWORD(2, 2), &w); }
  ~WsaInit() { WSACleanup(); }
};
void ensure_wsa() { static WsaInit init; }
#else
void ensure_wsa() {}
#endif

}  // namespace

struct P2PNode::PeerConn {
  std::uint64_t id = 0;
  socket_t fd = kInvalidSocket;
  PeerInfo info;
  std::mutex send_mu;
  std::atomic<bool> alive{true};
  std::thread thread;
  std::chrono::steady_clock::time_point last_msg{};
  std::uint32_t msg_count_window = 0;
  std::chrono::steady_clock::time_point window_start{};
  std::atomic<std::uint64_t> last_ping_nonce{0};
  std::chrono::steady_clock::time_point last_ping_sent{};
  std::mutex ping_mu;
  std::atomic<std::int64_t> latency_ms{-1};
};

P2PNode::P2PNode(NetworkConfig config, blockchain::ChainDatabase* chain, Mempool* mempool)
    : config_(std::move(config)), chain_(chain), mempool_(mempool) {
  ensure_wsa();
  local_nonce_ = random_nonce();
}

P2PNode::~P2PNode() { stop(); }

Result<void> P2PNode::start() {
  if (running_.exchange(true)) return Result<void>{};
  auto s = setup_listener();
  if (!s.ok()) { running_ = false; return s; }
  std::clog << "{\"event\":\"p2p_listener_ready\",\"port\":" << bound_port_ << ",\"reconnect_interval_ms\":" << config_.reconnect_interval_ms << ",\"seed_peers\":" << config_.seed_peers.size() << "}" << std::endl;
  accept_thread_ = std::thread([this] { accept_loop(); });
  maintenance_thread_ = std::thread([this] { maintenance_loop(); });
  for (const auto& seed : config_.seed_peers) {
    (void)(void)connect(seed.host, seed.port);
  }
  return Result<void>{};
}

void P2PNode::stop() {
  bool was_running = running_.exchange(false);
  (void)was_running;

  // Wake accept loop
  if (bound_port_ != 0) {
    socket_t w = ::socket(AF_INET, SOCK_STREAM, 0);
    if (w != kInvalidSocket) {
      sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(bound_port_);
      inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
      ::connect(w, reinterpret_cast<sockaddr*>(&a), sizeof(a));
      close_socket(w);
    }
  }

  close_socket(static_cast<socket_t>(listen_fd_));
  listen_fd_ = static_cast<std::intptr_t>(kInvalidSocket);

  std::vector<PeerPtr> to_close;
  {
    std::scoped_lock lock(mu_);
    for (auto& [_, p] : peers_) to_close.push_back(p);
  }
  for (auto& p : to_close) {
    p->alive = false;
#ifdef _WIN32
    ::shutdown(p->fd, SD_BOTH);
#else
    ::shutdown(p->fd, SHUT_RDWR);
#endif
    close_socket(p->fd);
  }
  auto safe_join = [](std::thread& t) {
    if (!t.joinable()) return;
    try {
      t.join();
    } catch (const std::system_error&) {
      // Best-effort join on shutdown; avoid terminate if join fails.
      if (t.joinable()) {
        try { t.detach(); } catch (...) {}
      }
    } catch (...) {
      if (t.joinable()) {
        try { t.detach(); } catch (...) {}
      }
    }
  };
  safe_join(accept_thread_);
  safe_join(maintenance_thread_);
  for (auto& p : to_close) safe_join(p->thread);
  std::vector<std::thread> zombies;
  {
    std::scoped_lock lock(mu_);
    zombies.swap(zombie_threads_);
  }
  for (auto& t : zombies) safe_join(t);
  std::scoped_lock lock(mu_);
  peers_.clear();
}

Result<void> P2PNode::setup_listener() {
  socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd == kInvalidSocket) return MakeError(ErrorCode::kIoError, "socket failed");
  int opt = 1;
#ifdef _WIN32
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(config_.listen_port);
  inet_pton(AF_INET, config_.listen_host.c_str(), &addr.sin_addr);
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close_socket(fd);
    return MakeError(ErrorCode::kIoError, "bind failed");
  }
  if (::listen(fd, 16) != 0) {
    close_socket(fd);
    return MakeError(ErrorCode::kIoError, "listen failed");
  }
  sockaddr_in bound{};
#ifdef _WIN32
  int blen = sizeof(bound);
#else
  socklen_t blen = sizeof(bound);
#endif
  if (getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &blen) == 0) {
    bound_port_ = ntohs(bound.sin_port);
  }
  set_recv_timeout(fd, 1000);
  listen_fd_ = static_cast<std::intptr_t>(fd);
  return Result<void>{};
}

std::uint16_t P2PNode::listen_port() const { return bound_port_; }

void P2PNode::accept_loop() {
  while (running_.load()) {
    sockaddr_in client{};
#ifdef _WIN32
    int len = sizeof(client);
#else
    socklen_t len = sizeof(client);
#endif
    socket_t cfd = ::accept(static_cast<socket_t>(listen_fd_), reinterpret_cast<sockaddr*>(&client), &len);
    if (cfd == kInvalidSocket) continue;
    char hostbuf[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &client.sin_addr, hostbuf, sizeof(hostbuf));
    std::string host = hostbuf;
    if (is_banned(host, ntohs(client.sin_port))) { close_socket(cfd); continue; }
    {
      std::scoped_lock lock(mu_);
      std::size_t inbound = 0;
      for (auto& [_, p] : peers_) if (p->info.inbound) ++inbound;
      if (peers_.size() >= config_.max_connections || inbound >= config_.max_inbound) {
        close_socket(cfd);
        continue;
      }
    }
    auto peer = std::make_shared<PeerConn>();
    peer->id = next_peer_id_.fetch_add(1);
    peer->fd = cfd;
    peer->info.id = peer->id;
    peer->info.host = host;
    peer->info.port = ntohs(client.sin_port);
    peer->info.inbound = true;
    peer->info.state = PeerState::kHandshaking;
    peer->info.connected_at = std::chrono::steady_clock::now();
    peer->info.last_seen = peer->info.connected_at;
    peer->last_msg = peer->info.connected_at;
    peer->window_start = peer->info.connected_at;
    set_recv_timeout(cfd, config_.handshake_timeout_ms);
    add_peer(peer);
    peer->thread = std::thread([this, peer] { peer_loop(peer); });
  }
}

Result<void> P2PNode::connect(const std::string& host, std::uint16_t port) {
  std::clog << "{\"event\":\"p2p_connect_attempt\",\"host\":\"" << host << "\",\"port\":" << port << "}" << std::endl;
  if (!running_.load()) return MakeError(ErrorCode::kInvalidArgument, "node not running");
  if (is_banned(host, port)) return MakeError(ErrorCode::kInvalidArgument, "peer banned");
  {
    std::scoped_lock lock(mu_);
    if (peers_.size() >= config_.max_connections) return MakeError(ErrorCode::kInvalidArgument, "connection limit reached");
    for (auto& [_, p] : peers_) if (p->info.host == host && p->info.listen_port == port) return Result<void>{};
  }

  addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  const auto port_s = std::to_string(port);
  if (getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res) != 0 || res == nullptr) {
    return MakeError(ErrorCode::kIoError, "resolve failed");
  }
  socket_t fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd == kInvalidSocket) { freeaddrinfo(res); return MakeError(ErrorCode::kIoError, "socket failed"); }
  if (::connect(fd, res->ai_addr, static_cast<socklen_t>(res->ai_addrlen)) != 0) {
    close_socket(fd); freeaddrinfo(res);
    return MakeError(ErrorCode::kIoError, "connect failed");
  }
  freeaddrinfo(res);
  set_recv_timeout(fd, config_.handshake_timeout_ms);

  auto peer = std::make_shared<PeerConn>();
  peer->id = next_peer_id_.fetch_add(1);
  peer->fd = fd;
  peer->info.id = peer->id;
  peer->info.host = host;
  peer->info.port = port;
  peer->info.listen_port = port;
  peer->info.inbound = false;
  {
    // Capture our ephemeral source port so both endpoints can key the same TCP
    // connection identically during duplicate-peer resolution.
    sockaddr_in local{};
    socklen_t local_len = sizeof local;
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&local), &local_len) == 0) {
      peer->info.local_port = ntohs(local.sin_port);
    }
  }
  peer->info.state = PeerState::kHandshaking;
  peer->info.connected_at = std::chrono::steady_clock::now();
  peer->info.last_seen = peer->info.connected_at;
  peer->last_msg = peer->info.connected_at;
  peer->window_start = peer->info.connected_at;
  add_peer(peer);

  // Send hello
  HelloMessage hello;
  hello.network_magic = config_.network_magic;
  hello.protocol_version = config_.protocol_version;
  hello.nonce = local_nonce_;
  hello.timestamp = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
  hello.genesis_hash = config_.genesis_hash;
  hello.agent = config_.agent;
  hello.listen_port = bound_port_;
  Message msg{config_.network_magic, MessageType::kHello, encode_hello(hello)};
  auto sent = send_message(peer, msg);
  if (!sent.ok()) { remove_peer(peer->id, "hello send failed"); return sent; }

  peer->thread = std::thread([this, peer] { peer_loop(peer); });
  return Result<void>{};
}

Result<void> P2PNode::send_message(PeerPtr peer, const Message& msg) {
  auto bytes = encode_message(msg);
  std::scoped_lock lock(peer->send_mu);
  if (!peer->alive.load()) return MakeError(ErrorCode::kIoError, "peer not alive");
  if (!send_all_bytes(peer->fd, bytes.data(), bytes.size())) return MakeError(ErrorCode::kIoError, "send failed");
  return Result<void>{};
}

Result<Message> P2PNode::read_message(PeerPtr peer) {
  std::array<std::uint8_t, 14> header{};
  if (!recv_all_bytes(peer->fd, header.data(), header.size())) return MakeError(ErrorCode::kIoError, "read header failed");
  BinaryReader hr(std::span<const std::uint8_t>(header.data(), header.size()));
  auto magic = hr.read_u32(); if (!magic.ok()) return magic.error();
  auto type = hr.read_u8(); if (!type.ok()) return type.error();
  auto reserved = hr.read_u8(); if (!reserved.ok()) return reserved.error();
  auto len = hr.read_u32(); if (!len.ok()) return len.error();
  auto csum = hr.read_u32(); if (!csum.ok()) return csum.error();
  (void)reserved;
  if (magic.value() != config_.network_magic) return MakeError(ErrorCode::kInvalidArgument, "network magic mismatch");
  if (len.value() > config_.max_message_size) return MakeError(ErrorCode::kInvalidArgument, "message too large");
  std::vector<std::uint8_t> payload(len.value());
  if (len.value() > 0 && !recv_all_bytes(peer->fd, payload.data(), payload.size())) return MakeError(ErrorCode::kIoError, "read payload failed");
  if (payload_checksum(payload) != csum.value()) return MakeError(ErrorCode::kInvalidArgument, "checksum mismatch");
  Message msg;
  msg.network_magic = magic.value();
  msg.type = static_cast<MessageType>(type.value());
  msg.payload = std::move(payload);
  return msg;
}

void P2PNode::peer_loop(PeerPtr peer) {
  while (running_.load() && peer->alive.load()) {
    auto msg = read_message(peer);
    if (!msg.ok()) {
      std::clog << "{\"event\":\"p2p_read_failed\",\"peer\":" << peer->id << ",\"error\":\"" << msg.error().to_string() << "\"}" << std::endl;
      score_peer(peer, kScoreTimeout, false, true, false);
      break;
    }
    peer->info.last_seen = std::chrono::steady_clock::now();
    peer->last_msg = peer->info.last_seen;
    if (!check_rate_limit(peer)) {
      score_peer(peer, kScoreSpam, false, false, true);
      maybe_ban(peer, "rate limit exceeded");
      break;
    }
    handle_message(peer, msg.value());
    if (peer->info.score.score <= kBanScore) {
      maybe_ban(peer, "score below ban threshold");
      break;
    }
  }
  remove_peer(peer->id, "loop exit");
}

void P2PNode::handle_message(PeerPtr peer, const Message& msg) {
  // Basic validation
  if (msg.payload.size() > config_.max_message_size) {
    score_peer(peer, kScoreInvalidMessage, true, false, false);
    return;
  }
  relay_message_count_.fetch_add(1);

  switch (msg.type) {
    case MessageType::kHello: {
      auto hello = decode_hello(msg.payload);
      if (!hello.ok()) { score_peer(peer, kScoreInvalidMessage, true, false, false); return; }
      handle_hello(peer, hello.value());
      break;
    }
    case MessageType::kHelloAck: {
      auto hello = decode_hello(msg.payload);
      if (!hello.ok()) { score_peer(peer, kScoreInvalidMessage, true, false, false); return; }
      handle_hello(peer, hello.value());
      if (peer->info.state == PeerState::kReady) {
        // request sync and addr
        (void)(void)send_message(peer, Message{config_.network_magic, MessageType::kGetAddr, {}});
        handle_get_headers(peer); // trigger our own getheaders? no - request from them
        Message gh{config_.network_magic, MessageType::kGetHeaders, encode_hash(chain_ ? chain_->tip().ok() && chain_->tip().value().has_value() ? chain_->tip().value()->hash : Hash256Bytes{} : Hash256Bytes{})};
        (void)send_message(peer, gh);
      }
      break;
    }
    case MessageType::kPing: {
      auto nonce = decode_pingpong(msg.payload);
      if (!nonce.ok()) { score_peer(peer, kScoreInvalidMessage, true, false, false); return; }
      (void)send_message(peer, Message{config_.network_magic, MessageType::kPong, encode_pingpong(nonce.value())});
      score_peer(peer, kScoreGoodMessage);
      break;
    }
    case MessageType::kPong: {
      auto nonce = decode_pingpong(msg.payload);
      if (!nonce.ok()) { score_peer(peer, kScoreInvalidMessage, true, false, false); return; }
      if (nonce.value() == peer->last_ping_nonce.load()) {
        std::chrono::steady_clock::time_point sent;
        {
          std::scoped_lock lk(peer->ping_mu);
          sent = peer->last_ping_sent;
        }
        if (sent.time_since_epoch().count() > 0) {
          const auto rtt = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - sent).count();
          const auto ms = std::max<std::int64_t>(0, static_cast<std::int64_t>(rtt));
          peer->latency_ms.store(ms);
          peer->info.latency_ms = ms;
        }
      }
      score_peer(peer, kScoreGoodMessage);
      break;
    }
    case MessageType::kGetAddr: {
      auto addrs = known_addresses();
      (void)send_message(peer, Message{config_.network_magic, MessageType::kAddr, encode_addr(addrs)});
      break;
    }
    case MessageType::kAddr: {
      auto addrs = decode_addr(msg.payload);
      if (!addrs.ok()) { score_peer(peer, kScoreInvalidMessage, true, false, false); return; }
      handle_addr(peer, addrs.value());
      break;
    }
    case MessageType::kInv: {
      auto inv = decode_inv(msg.payload);
      if (!inv.ok()) { score_peer(peer, kScoreInvalidMessage, true, false, false); return; }
      handle_inv(peer, inv.value());
      break;
    }
    case MessageType::kGetData: {
      auto inv = decode_inv(msg.payload);
      if (!inv.ok()) { score_peer(peer, kScoreInvalidMessage, true, false, false); return; }
      handle_get_data(peer, inv.value());
      break;
    }
    case MessageType::kBlock:
    case MessageType::kBftBlock: {
      if (!chain_) return;
      const bool is_bft_block = msg.type == MessageType::kBftBlock;
      auto blk = deserialize_block(msg.payload);
      if (!blk.ok()) { score_peer(peer, kScoreInvalidMessage, true, false, false); return; }
      const auto h = block_hash(blk.value().header);
      if (!seen_or_mark(h)) { score_peer(peer, kScoreGoodMessage); return; }
      auto r = chain_->write_block(blk.value());
      if (r.ok()) {
        // BFT blocks are persisted but stay unapplied until a finality
        // certificate is observed; ordinary blocks extend the tip immediately.
        auto tip = chain_->tip();
        if (tip.ok() && tip.value().has_value()) {
          if (!is_bft_block && blk.value().header.prev_hash == tip.value()->hash && blk.value().header.height == tip.value()->height + 1) {
            (void)chain_->apply_block(blk.value());
          }
        } else if (!is_bft_block && blk.value().header.height == 0) {
          (void)chain_->apply_block(blk.value());
        }
        score_peer(peer, kScoreGoodMessage);
        std::vector<InventoryItem> inv{{InventoryType::kBlock, h}};
        auto out = is_bft_block
            ? Message{config_.network_magic, MessageType::kBftBlock, serialize_block(blk.value())}
            : Message{config_.network_magic, MessageType::kInv, encode_inv(inv)};
        std::scoped_lock lock(mu_);
        for (auto& [id, p] : peers_) {
          if (id == peer->id || p->info.state != PeerState::kReady) continue;
          (void)send_message(p, out);
        }
      } else {
        score_peer(peer, kScoreInvalidMessage, true, false, false);
      }
      break;
    }
    case MessageType::kTx: {
      auto txr = deserialize_transaction(msg.payload);
      if (!txr.ok()) { score_peer(peer, kScoreInvalidMessage, true, false, false); return; }
      const auto h = txid(txr.value());
      if (!seen_or_mark(h)) { score_peer(peer, kScoreGoodMessage); return; }
      if (mempool_ && chain_) {
        auto utxos = chain_->load_utxo_set();
        if (utxos.ok()) {
          auto tip = chain_->tip();
          const std::uint32_t spend_h = (tip.ok() && tip.value().has_value()) ? tip.value()->height + 1 : 0u;
          auto ar = mempool_->add(txr.value(), utxos.value(), true, spend_h);
          if (ar.ok()) {
            score_peer(peer, kScoreGoodMessage);
            std::vector<InventoryItem> inv{{InventoryType::kTx, h}};
            auto out = Message{config_.network_magic, MessageType::kInv, encode_inv(inv)};
            std::scoped_lock lock(mu_);
            for (auto& [id, p] : peers_) {
              if (id == peer->id || p->info.state != PeerState::kReady) continue;
              (void)send_message(p, out);
            }
          } else {
            // could be duplicate/conflict; mild penalty
            score_peer(peer, -1);
          }
        }
      }
      break;
    }
    case MessageType::kGetHeaders: {
      handle_get_headers(peer);
      break;
    }
    case MessageType::kHeaders: {
      auto inv = decode_inv(msg.payload); // reuse inv encoding as hash list? easier: custom
      // headers payload encoded as inv-style block hashes
      if (!inv.ok()) { score_peer(peer, kScoreInvalidMessage, true, false, false); return; }
      std::vector<Hash256Bytes> hashes;
      hashes.reserve(inv.value().size());
      for (auto& it : inv.value()) if (it.type == InventoryType::kBlock) hashes.push_back(it.hash);
      handle_headers(peer, hashes);
      break;
    }
    case MessageType::kPoiAttestation: {
      std::string payload(msg.payload.begin(), msg.payload.end());
      Hash256Bytes ph{};
      auto d = sha256(std::span<const std::uint8_t>(msg.payload.data(), msg.payload.size()));
      std::copy(d.begin(), d.end(), ph.begin());
      // ACK back to sender (best-effort, idempotent).
      {
        Message ack{config_.network_magic, MessageType::kPoiAttestationAck, std::vector<std::uint8_t>(ph.begin(), ph.end())};
        (void)send_message(peer, ack);
      }
      if (seen_or_mark(ph)) {
        // Snapshot relay targets and the handler under mu_, then send/call
        // outside the lock. Holding mu_ across send_message() and re-locking it
        // for the handler previously self-deadlocked the non-recursive mu_.
        auto out = Message{config_.network_magic, MessageType::kPoiAttestation, msg.payload};
        std::vector<PeerPtr> targets;
        std::function<void(const std::string&)> h;
        {
          std::scoped_lock lk(mu_);
          for (auto& [id, p] : peers_) {
            if (id == peer->id || p->info.state != PeerState::kReady) continue;
            targets.push_back(p);
          }
          h = poi_attestation_handler_;
        }
        for (auto& p : targets) (void)send_message(p, out);
        if (h) h(payload);
      }
      break;
    }
    case MessageType::kPoiAttestationAck: {
      if (msg.payload.size() == 32) {
        Hash256Bytes ph{};
        std::copy(msg.payload.begin(), msg.payload.end(), ph.begin());
        const auto key = to_hex(ph);
        std::scoped_lock lock(mu_);
        pending_poi_attestations_.erase(key);
      }
      break;
    }

    case MessageType::kBftProposal: {
      auto v = decode_bft_proposal(msg.payload);
      if (!v.ok() || v.value().signature.size() != 64) {
        score_peer(peer, kScoreInvalidMessage, true, false, false);
        return;
      }
      if (config_.on_bft_proposal) config_.on_bft_proposal(v.value());
      break;
    }
    case MessageType::kBftVote: {
      auto v = decode_bft_vote(msg.payload);
      if (!v.ok()) { score_peer(peer, kScoreInvalidMessage, true, false, false); return; }
      if (config_.on_bft_vote) config_.on_bft_vote(v.value());
      break;
    }
    case MessageType::kBftQc: {
      auto v = decode_bft_qc(msg.payload);
      if (!v.ok()) { score_peer(peer, kScoreInvalidMessage, true, false, false); return; }
      if (config_.on_bft_qc) config_.on_bft_qc(v.value());
      break;
    }
    case MessageType::kBftFinality: {
      auto v = decode_bft_finality(msg.payload);
      if (!v.ok()) { score_peer(peer, kScoreInvalidMessage, true, false, false); return; }
      if (config_.on_bft_finality) config_.on_bft_finality(v.value());
      break;
    }
    case MessageType::kBftViewChange: {
      auto v = decode_bft_view_change(msg.payload);
      if (!v.ok()) { score_peer(peer, kScoreInvalidMessage, true, false, false); return; }
      if (config_.on_bft_view_change) config_.on_bft_view_change(v.value());
      break;
    }
    case MessageType::kBftNewView: {
      auto v = decode_bft_new_view(msg.payload);
      if (!v.ok()) { score_peer(peer, kScoreInvalidMessage, true, false, false); return; }
      if (config_.on_bft_new_view) config_.on_bft_new_view(v.value());
      break;
    }

    case MessageType::kReject:
    default:
      break;
  }
}

void P2PNode::handle_hello(PeerPtr peer, const HelloMessage& hello) {
  if (hello.network_magic != config_.network_magic) { maybe_ban(peer, "magic mismatch"); return; }
  if (hello.genesis_hash != config_.genesis_hash) { maybe_ban(peer, "genesis mismatch"); return; }
  if (hello.protocol_version != config_.protocol_version) { score_peer(peer, kScoreInvalidMessage, true, false, false); remove_peer(peer->id, "protocol mismatch"); return; }
  if (hello.nonce == local_nonce_) { remove_peer(peer->id, "self connection"); return; }

  peer->info.nonce = hello.nonce;
  // Handshake timeout must not become an idle-peer timeout.
  (void)set_recv_timeout(peer->fd, 0);
  peer->info.agent = hello.agent;
  peer->info.listen_port = hello.listen_port != 0 ? hello.listen_port : peer->info.port;
  if (is_banned(peer->info.host, peer->info.listen_port)) { remove_peer(peer->id, "banned"); return; }
  // Dedup: simultaneous outbound+inbound (or a reconnect race that dials twice)
  // can leave more than one TCP connection to the same logical peer. Collapse
  // them to exactly one.
  //
  // Symmetry requirement: both endpoints MUST independently pick the SAME TCP
  // connection to keep, otherwise each side drops the connection the other kept
  // and the link dies (mutual-drop thrash). "Prefer outbound" is NOT symmetric
  // because each side's outbound is a different TCP connection. We therefore key
  // the decision on a canonical per-connection identity that both endpoints
  // derive identically:
  //   (initiator_nonce, responder_nonce, initiator_ephemeral_port, responder_listen_port)
  // For a given TCP connection:
  //   initiator side (outbound): nonce=local_nonce_, ephemeral=local_port,
  //                              responder listen port = peer listen_port
  //   responder side (inbound):  nonce=peer nonce, ephemeral=peer source port,
  //                              responder listen port = our bound_port_
  // Distinct same-direction duplicate connections differ by the initiator's
  // ephemeral port, so they no longer collide. Both endpoints keep the
  // lexicographically smallest key, which converges to the same connection.
  PeerPtr dup;
  bool drop_self = false;
  {
    std::scoped_lock lock(mu_);
    peer->info.state = PeerState::kReady;
    auto conn_key = [&](const PeerPtr& p) {
      std::uint64_t init_nonce = 0;
      std::uint64_t resp_nonce = 0;
      std::uint16_t init_port = 0;
      std::uint16_t resp_port = 0;
      if (p->info.inbound) {
        init_nonce = p->info.nonce;      // remote initiator
        resp_nonce = local_nonce_;       // us
        init_port = p->info.port;        // remote ephemeral source port
        resp_port = bound_port_;         // our listen port
      } else {
        init_nonce = local_nonce_;       // us
        resp_nonce = p->info.nonce;      // remote responder
        init_port = p->info.local_port;  // our ephemeral source port
        resp_port = p->info.listen_port; // remote listen port
      }
      return std::array<std::uint64_t, 4>{init_nonce, resp_nonce,
                                          static_cast<std::uint64_t>(init_port),
                                          static_cast<std::uint64_t>(resp_port)};
    };
    const auto self_key = conn_key(peer);
    for (auto& [id, p] : peers_) {
      if (id == peer->id) continue;
      if (p->info.state != PeerState::kReady) continue; // only compare fully handshaken peers
      if (p->info.host != peer->info.host || p->info.listen_port != peer->info.listen_port) continue;
      const auto other_key = conn_key(p);
      if (other_key == self_key) continue; // same connection keyed twice; ignore
      // Keep the smaller canonical key on both endpoints.
      if (self_key < other_key) dup = p; else drop_self = true;
      break;
    }
  }
  // Act outside mu_: remove_peer() owns the socket close + thread lifetime rules
  // (including detaching the calling thread), which is required to avoid
  // destroying a joinable std::thread (std::terminate) in the drop-self case.
  if (dup) {
    std::clog << "{\"event\":\"p2p_duplicate_peer_replaced\",\"kept\":" << peer->id << ",\"dropped\":" << dup->id << ",\"host\":\"" << peer->info.host << "\",\"port\":" << peer->info.listen_port << "}" << std::endl;
    remove_peer(dup->id, "duplicate connection");
  } else if (drop_self) {
    std::clog << "{\"event\":\"p2p_duplicate_peer_dropped\",\"dropped\":" << peer->id << ",\"host\":\"" << peer->info.host << "\",\"port\":" << peer->info.listen_port << "}" << std::endl;
    remove_peer(peer->id, "duplicate connection");
    return;
  }
  std::clog << "{\"event\":\"p2p_peer_ready\",\"peer\":" << peer->id << ",\"inbound\":" << (peer->info.inbound ? "true" : "false") << ",\"port\":" << peer->info.listen_port << "}" << std::endl;
  score_peer(peer, kScoreGoodMessage);

  remember_address({peer->info.host, peer->info.listen_port, 0, static_cast<std::int64_t>(std::chrono::system_clock::now().time_since_epoch().count())});

  if (peer->info.inbound) {
    HelloMessage ack;
    ack.network_magic = config_.network_magic;
    ack.protocol_version = config_.protocol_version;
    ack.nonce = local_nonce_;
    ack.timestamp = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    ack.genesis_hash = config_.genesis_hash;
    ack.agent = config_.agent;
    ack.listen_port = bound_port_;
    (void)send_message(peer, Message{config_.network_magic, MessageType::kHelloAck, encode_hello(ack)});
    (void)(void)send_message(peer, Message{config_.network_magic, MessageType::kGetAddr, {}});
    // ask for headers
    Hash256Bytes tip{};
    if (chain_) {
      auto t = chain_->tip();
      if (t.ok() && t.value().has_value()) tip = t.value()->hash;
    }
    (void)send_message(peer, Message{config_.network_magic, MessageType::kGetHeaders, encode_hash(tip)});
  }
}

void P2PNode::handle_inv(PeerPtr peer, const std::vector<InventoryItem>& items) {
  std::vector<InventoryItem> want;
  for (const auto& it : items) {
    if (it.type == InventoryType::kBlock) {
      if (chain_) {
        auto bi = chain_->get_block_index(it.hash);
        if (bi.ok() && bi.value().has_value()) continue;
      }
    } else if (it.type == InventoryType::kTx) {
      if (mempool_) {
        auto e = mempool_->get(it.hash);
        if (e.has_value()) continue;
      }
    }
    const auto key = to_hex(std::span<const std::uint8_t>(it.hash.data(), it.hash.size()));
    bool already_seen = false;
    {
      std::scoped_lock lock(mu_);
      already_seen = seen_hashes_.find(key) != seen_hashes_.end();
    }
    if (!already_seen) want.push_back(it);
  }
  if (!want.empty()) (void)send_message(peer, Message{config_.network_magic, MessageType::kGetData, encode_inv(want)});
}

void P2PNode::handle_get_data(PeerPtr peer, const std::vector<InventoryItem>& items) {
  for (const auto& it : items) {
    if (it.type == InventoryType::kBlock && chain_) {
      auto b = chain_->read_block(it.hash);
      if (b.ok()) (void)send_message(peer, Message{config_.network_magic, MessageType::kBlock, serialize_block(b.value())});
    } else if (it.type == InventoryType::kTx && mempool_) {
      auto e = mempool_->get(it.hash);
      if (e.has_value()) (void)send_message(peer, Message{config_.network_magic, MessageType::kTx, serialize_transaction(e->tx)});
    }
  }
}

void P2PNode::handle_get_headers(PeerPtr peer) {
  if (!chain_) return;
  auto tip = chain_->tip();
  if (!tip.ok() || !tip.value().has_value()) return;
  // send active chain hashes from genesis upward (simple foundation sync)
  std::vector<InventoryItem> inv;
  for (std::uint32_t h = 0; h <= tip.value()->height && inv.size() < 2000; ++h) {
    auto hh = chain_->get_hash_by_height(h);
    if (!hh.ok() || !hh.value().has_value()) continue;
    inv.push_back({InventoryType::kBlock, hh.value().value()});
  }
  (void)send_message(peer, Message{config_.network_magic, MessageType::kHeaders, encode_inv(inv)});
}

void P2PNode::handle_headers(PeerPtr peer, const std::vector<Hash256Bytes>& hashes) {
  if (!chain_) return;
  std::vector<InventoryItem> want;
  for (const auto& h : hashes) {
    auto bi = chain_->get_block_index(h);
    if (bi.ok() && bi.value().has_value()) continue;
    want.push_back({InventoryType::kBlock, h});
  }
  if (!want.empty()) (void)send_message(peer, Message{config_.network_magic, MessageType::kGetData, encode_inv(want)});
}

void P2PNode::handle_addr(PeerPtr peer, const std::vector<NetAddress>& addrs) {
  (void)peer;
  for (const auto& a : addrs) {
    if (a.host.empty() || a.port == 0) continue;
    remember_address(a);
  }
}

void P2PNode::add_peer(PeerPtr peer) {
  std::scoped_lock lock(mu_);
  peers_[peer->id] = peer;
}

void P2PNode::remove_peer(std::uint64_t id, std::string_view /*reason*/) {
  PeerPtr peer;
  {
    std::scoped_lock lock(mu_);
    auto it = peers_.find(id);
    if (it == peers_.end()) return;
    peer = it->second;
    peers_.erase(it);
  }
  peer->alive = false;
  close_socket(peer->fd);
  peer->fd = kInvalidSocket;
  if (peer->thread.joinable() && std::this_thread::get_id() != peer->thread.get_id()) {
    std::scoped_lock lock(mu_);
    zombie_threads_.push_back(std::move(peer->thread));
  } else if (peer->thread.joinable()) {
    // self-exiting thread: detach as last resort; should be rare
    peer->thread.detach();
  }
}

void P2PNode::score_peer(PeerPtr peer, int delta, bool invalid, bool timeout, bool spam) {
  peer->info.score.score += delta;
  if (invalid) peer->info.score.invalid_messages++;
  if (timeout) peer->info.score.timeouts++;
  if (spam) peer->info.score.spam_events++;
}

void P2PNode::maybe_ban(PeerPtr peer, std::string_view /*reason*/) {
  peer->info.state = PeerState::kBanned;
  std::scoped_lock lock(mu_);
  // Never ban by host alone: all regtest validators share 127.0.0.1. Ban the
  // authenticated listen port (learned from hello) so a new inbound ephemeral
  // source port cannot evade the ban.
  const auto key_port = peer->info.listen_port != 0 ? peer->info.listen_port : peer->info.port;
  banned_[addr_key(peer->info.host, key_port)] = std::chrono::steady_clock::now() + std::chrono::minutes(10);
}

bool P2PNode::check_rate_limit(PeerPtr peer) {
  const auto now = std::chrono::steady_clock::now();
  if (now - peer->window_start > std::chrono::seconds(1)) {
    peer->window_start = now;
    peer->msg_count_window = 0;
  }
  ++peer->msg_count_window;
  return peer->msg_count_window <= config_.rate_limit_per_sec;
}

bool P2PNode::seen_or_mark(const Hash256Bytes& hash) {
  const auto key = to_hex(std::span<const std::uint8_t>(hash.data(), hash.size()));
  std::scoped_lock lock(mu_);
  if (seen_hashes_.size() > 10000) seen_hashes_.clear();
  auto [_, inserted] = seen_hashes_.insert(key);
  return inserted;
}

std::vector<NetAddress> P2PNode::known_addresses() const {
  std::scoped_lock lock(mu_);
  std::vector<NetAddress> out;
  for (const auto& a : known_addrs_) {
    if (out.size() >= kMaxAddr) break;
    out.push_back(a);
  }
  return out;
}

void P2PNode::remember_address(const NetAddress& addr) {
  const auto k = addr_key(addr.host, addr.port);
  std::scoped_lock lock(mu_);
  if (known_addr_keys_.insert(k).second) known_addrs_.push_back(addr);
}

bool P2PNode::is_banned(const std::string& host, std::uint16_t port) const {
  std::scoped_lock lock(mu_);
  auto it = banned_.find(addr_key(host, port));
  if (it == banned_.end()) return false;
  if (std::chrono::steady_clock::now() > it->second) return false;
  return true;
}

std::vector<PeerInfo> P2PNode::peers() const {
  std::scoped_lock lock(mu_);
  std::vector<PeerInfo> out;
  out.reserve(peers_.size());
  for (auto& [_, p] : peers_) out.push_back(p->info);
  return out;
}

std::size_t P2PNode::peer_count() const { std::scoped_lock lock(mu_); return peers_.size(); }
std::size_t P2PNode::ready_peer_count() const {
  std::scoped_lock lock(mu_);
  std::size_t n = 0;
  for (auto& [_, p] : peers_) if (p->info.state == PeerState::kReady) ++n;
  return n;
}

double P2PNode::average_latency_ms() const {
  std::scoped_lock lock(mu_);
  double sum = 0.0;
  std::size_t n = 0;
  for (const auto& [_, p] : peers_) {
    if (p->info.state != PeerState::kReady) continue;
    const auto ms = p->latency_ms.load();
    if (ms >= 0) { sum += static_cast<double>(ms); ++n; }
  }
  if (n == 0) return -1.0;
  return sum / static_cast<double>(n);
}

std::uint64_t P2PNode::relay_message_count() const { return relay_message_count_.load(); }

Result<void> P2PNode::broadcast_tx(const Transaction& tx) {
  const auto h = txid(tx);
  if (!seen_or_mark(h)) return Result<void>{};
  std::vector<InventoryItem> inv{{InventoryType::kTx, h}};
  auto msg = Message{config_.network_magic, MessageType::kInv, encode_inv(inv)};
  std::scoped_lock lock(mu_);
  for (auto& [_, p] : peers_) {
    if (p->info.state == PeerState::kReady) { (void)send_message(p, msg); relay_message_count_.fetch_add(1); }
  }
  return Result<void>{};
}

Result<void> P2PNode::broadcast_block(const Block& block) {
  const auto h = block_hash(block.header);
  if (!seen_or_mark(h)) return Result<void>{};
  // Proposals carry only the block hash. Deliver the concrete block first so
  // validators can retrieve it before their proposal callback votes.
  auto msg = Message{config_.network_magic, MessageType::kBlock, serialize_block(block)};
  std::scoped_lock lock(mu_);
  for (auto& [_, p] : peers_) {
    if (p->info.state == PeerState::kReady) { (void)send_message(p, msg); relay_message_count_.fetch_add(1); }
  }
  return Result<void>{};
}

Result<void> P2PNode::broadcast_poi_attestation(const std::string& payload) {
  std::vector<std::uint8_t> bytes(payload.begin(), payload.end());
  Hash256Bytes ph{};
  auto d = sha256(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
  std::copy(d.begin(), d.end(), ph.begin());
  const auto key = to_hex(ph);
  const auto now_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
  if (!seen_or_mark(ph)) return Result<void>{};
  auto msg = Message{config_.network_magic, MessageType::kPoiAttestation, bytes};
  std::scoped_lock lock(mu_);
  pending_poi_attestations_[key] = PendingPoiAttestation{payload, now_ms, 0};
  if (pending_poi_attestations_.size() > 4096) {
    for (auto it = pending_poi_attestations_.begin(); it != pending_poi_attestations_.end() && pending_poi_attestations_.size() > 3072; ) {
      it = pending_poi_attestations_.erase(it);
    }
  }
  for (auto& [_, p] : peers_) {
    if (p->info.state == PeerState::kReady) { (void)send_message(p, msg); relay_message_count_.fetch_add(1); }
  }
  return Result<void>{};
}

void P2PNode::retry_pending_poi_attestations(std::uint64_t now_ms, std::uint64_t retry_interval_ms, std::uint32_t max_retries) {
  std::vector<std::vector<std::uint8_t>> to_send;
  {
    std::scoped_lock lock(mu_);
    for (auto it = pending_poi_attestations_.begin(); it != pending_poi_attestations_.end(); ) {
      auto& p = it->second;
      if (p.retries >= max_retries) { it = pending_poi_attestations_.erase(it); continue; }
      if (p.last_sent_ms != 0 && now_ms < p.last_sent_ms + retry_interval_ms) { ++it; continue; }
      p.last_sent_ms = now_ms;
      ++p.retries;
      to_send.emplace_back(p.payload.begin(), p.payload.end());
      ++it;
    }
  }
  if (to_send.empty()) return;
  std::scoped_lock lock(mu_);
  for (auto& bytes : to_send) {
    auto msg = Message{config_.network_magic, MessageType::kPoiAttestation, std::move(bytes)};
    for (auto& [_, peer] : peers_) {
      if (peer->info.state == PeerState::kReady) { (void)send_message(peer, msg); relay_message_count_.fetch_add(1); }
    }
  }
}

void P2PNode::set_poi_attestation_handler(std::function<void(const std::string&)> h) {
  std::scoped_lock lock(mu_);
  poi_attestation_handler_ = std::move(h);
}

Result<void> P2PNode::broadcast_bft_block(const Block& block) {
  const auto h = block_hash(block.header);
  if (!seen_or_mark(h)) return Result<void>{};
  Message msg{config_.network_magic, MessageType::kBftBlock, serialize_block(block)};
  std::scoped_lock lock(mu_);
  for (auto& [_, p] : peers_) if (p->info.state == PeerState::kReady) (void)send_message(p, msg);
  return {};
}

Result<void> P2PNode::broadcast_bft_proposal(const consensus::BlockProposal& v) {
  auto msg = Message{config_.network_magic, MessageType::kBftProposal, encode_bft_proposal(v)};
  std::scoped_lock lock(mu_);
  std::size_t sent = 0;
  for (auto& [_, p] : peers_) {
    if (p->info.state == PeerState::kReady) { (void)send_message(p, msg); ++sent; }
  }
  if (sent == 0) return MakeError(ErrorCode::kInvalidArgument, "no ready peers");
  return {};
}

Result<void> P2PNode::broadcast_bft_vote(const consensus::Vote& v) {
  auto msg = Message{config_.network_magic, MessageType::kBftVote, encode_bft_vote(v)};
  std::scoped_lock lock(mu_);
  std::size_t sent = 0;
  for (auto& [_, p] : peers_) if (p->info.state == PeerState::kReady) { (void)send_message(p, msg); ++sent; }
  if (sent == 0) return MakeError(ErrorCode::kInvalidArgument, "no ready peers");
  return {};
}

Result<void> P2PNode::broadcast_bft_qc(const consensus::QuorumCertificate& v) {
  auto msg = Message{config_.network_magic, MessageType::kBftQc, encode_bft_qc(v)};
  std::scoped_lock lock(mu_);
  std::size_t sent = 0;
  for (auto& [_, p] : peers_) if (p->info.state == PeerState::kReady) { (void)send_message(p, msg); ++sent; }
  if (sent == 0) return MakeError(ErrorCode::kInvalidArgument, "no ready peers");
  return {};
}

Result<void> P2PNode::broadcast_bft_finality(const consensus::FinalityCertificate& v) {
  auto msg = Message{config_.network_magic, MessageType::kBftFinality, encode_bft_finality(v)};
  std::scoped_lock lock(mu_);
  std::size_t sent = 0;
  for (auto& [_, p] : peers_) if (p->info.state == PeerState::kReady) { (void)send_message(p, msg); ++sent; }
  if (sent == 0) return MakeError(ErrorCode::kInvalidArgument, "no ready peers");
  return {};
}

Result<void> P2PNode::broadcast_bft_view_change(const consensus::ViewChange& v) {
  auto msg = Message{config_.network_magic, MessageType::kBftViewChange, encode_bft_view_change(v)};
  std::scoped_lock lock(mu_);
  std::size_t sent = 0;
  for (auto& [_, p] : peers_) if (p->info.state == PeerState::kReady) { (void)send_message(p, msg); ++sent; }
  if (sent == 0) return MakeError(ErrorCode::kInvalidArgument, "no ready peers");
  return {};
}

Result<void> P2PNode::broadcast_bft_new_view(const consensus::NewView& v) {
  auto msg = Message{config_.network_magic, MessageType::kBftNewView, encode_bft_new_view(v)};
  std::scoped_lock lock(mu_);
  std::size_t sent = 0;
  for (auto& [_, p] : peers_) if (p->info.state == PeerState::kReady) { (void)send_message(p, msg); ++sent; }
  if (sent == 0) return MakeError(ErrorCode::kInvalidArgument, "no ready peers");
  return {};
}

Result<void> P2PNode::request_sync() {
  Hash256Bytes tip{};
  if (chain_) {
    auto t = chain_->tip();
    if (t.ok() && t.value().has_value()) tip = t.value()->hash;
  }
  auto msg = Message{config_.network_magic, MessageType::kGetHeaders, encode_hash(tip)};
  std::scoped_lock lock(mu_);
  for (auto& [_, p] : peers_) if (p->info.state == PeerState::kReady) (void)send_message(p, msg);
  return Result<void>{};
}

void P2PNode::maintenance_loop() {
  std::clog << "{\"event\":\"p2p_reconnect_loop_started\",\"targets\":" << config_.seed_peers.size() << ",\"reconnect_interval_ms\":" << config_.reconnect_interval_ms << "}" << std::endl;
  while (running_.load()) {
    std::clog << "{\"event\":\"p2p_reconnect_loop_tick\",\"targets\":" << config_.seed_peers.size() << "}" << std::endl;
    const auto now = std::chrono::steady_clock::now();

    std::vector<PeerPtr> timed_out;
    {
      std::scoped_lock lock(mu_);
      for (auto& [_, p] : peers_) {
        if (p->info.state == PeerState::kHandshaking) {
          auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - p->info.connected_at).count();
          if (ms > config_.handshake_timeout_ms) timed_out.push_back(p);
        }
      }
    }
    for (auto& p : timed_out) {
      score_peer(p, kScoreTimeout, false, true, false);
      maybe_ban(p, "handshake timeout");
      remove_peer(p->id, "handshake timeout");
    }

    // Reap finished zombie threads best-effort
    {
      std::vector<std::thread> zombies;
      {
        std::scoped_lock lock(mu_);
        zombies.swap(zombie_threads_);
      }
      for (auto& t : zombies) if (t.joinable()) t.join();
    }

    // Retry each configured target independently; reconnect attempts are bounded
    // by reconnect_interval_ms and only missing targets are attempted.
    if (running_.load()) {
      for (const auto& seed : config_.seed_peers) {
        if (!running_.load()) break;
        bool connected = false;
        {
          std::scoped_lock lock(mu_);
          for (const auto& [_, p] : peers_) {
            if (p->info.host == seed.host && p->info.listen_port == seed.port) {
              connected = true;
              break;
            }
          }
        }
        std::clog << "{\"event\":\"p2p_reconnect_decision\",\"target\":\"" << seed.host << ":" << seed.port << "\",\"in_peers\":" << (connected ? "true" : "false") << ",\"action\":\"" << (connected ? "skip" : "dial") << "\",\"reason\":\"" << (connected ? "target_present" : "target_missing") << "\"}" << std::endl;
        if (!connected) (void)(void)connect(seed.host, seed.port);
      }
    }
    // Ping ready peers for latency measurement (Phase B)
    if (running_.load()) {
      std::vector<PeerPtr> ready;
      {
        std::scoped_lock lock(mu_);
        for (auto& [_, p] : peers_) if (p->alive.load() && p->info.state == PeerState::kReady) ready.push_back(p);
      }
      for (auto& p : ready) {
        const auto nonce = random_nonce();
        {
          std::scoped_lock lk(p->ping_mu);
          p->last_ping_nonce.store(nonce);
          p->last_ping_sent = std::chrono::steady_clock::now();
        }
        (void)send_message(p, Message{config_.network_magic, MessageType::kPing, encode_pingpong(nonce)});
      }
    }

    if (running_.load()) std::this_thread::sleep_for(std::chrono::milliseconds(config_.reconnect_interval_ms));
  }
}

}  // namespace phantomx::network
