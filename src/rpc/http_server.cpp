#include "phantomx/rpc/http_server.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <vector>

#include <jansson.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#endif

namespace phantomx::rpc {
thread_local std::string g_rpc_remote_ip;
namespace {

#ifdef _WIN32
using socket_t = SOCKET;
inline constexpr socket_t kInvalidSocket = INVALID_SOCKET;
inline int close_socket(socket_t s) { return ::closesocket(s); }
using ssize_type = std::ptrdiff_t;
#else
using socket_t = int;
inline constexpr socket_t kInvalidSocket = -1;
inline int close_socket(socket_t s) { return ::close(s); }
using ssize_type = ssize_t;
#endif

constexpr std::size_t kMaxRequestBytes = 1u << 20;  // 1 MiB
constexpr char kVersionHeader[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n";
constexpr char kCloseHeader[] = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

struct RawRequest {
  std::string method;
  std::string id;
  std::string token;
  std::unordered_map<std::string, std::string> params;
};

Result<RawRequest> parse_request(const std::string& raw) {
  json_error_t err{};
  json_t* root = json_loads(raw.c_str(), 0, &err);
  if (root == nullptr) return MakeError(ErrorCode::kParseError, "invalid JSON request");

  RawRequest req;
  if (const json_t* id = json_object_get(root, "id"); id != nullptr) {
    if (json_is_string(id)) {
      req.id = json_string_value(id);
    } else {
      char buf[32];
      std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(json_integer_value(id)));
      req.id = buf;
    }
  }
  if (const json_t* method = json_object_get(root, "method"); method != nullptr && json_is_string(method)) {
    req.method = json_string_value(method);
  }
  if (const json_t* token = json_object_get(root, "token"); token != nullptr && json_is_string(token)) {
    req.token = json_string_value(token);
  }
  json_t* params = json_object_get(root, "params");
  if (params != nullptr) {
    if (!json_is_object(params)) {
      json_decref(root);
      return MakeError(ErrorCode::kInvalidArgument, "params must be an object");
    }
    const char* key = nullptr;
    json_t* value = nullptr;
    json_object_foreach(params, key, value) {
      if (json_is_string(value)) {
        req.params[key] = json_string_value(value);
      } else if (json_is_integer(value)) {
        // Handlers parse many params with std::stoull; coercing booleans,
        // reals, or null would turn an omitted/default value into an error.
        req.params[key] = std::to_string(json_integer_value(value));
      }
    }
  }
  json_decref(root);
  if (req.method.empty()) return MakeError(ErrorCode::kInvalidArgument, "missing method");
  return req;
}

ssize_type send_all(socket_t fd, const char* data, std::size_t len) {
  std::size_t sent = 0;
  while (sent < len) {
#ifdef _WIN32
    const int n = ::send(fd, data + sent, static_cast<int>(len - sent), 0);
    if (n <= 0) return -1;
#else
    const ssize_t n = ::send(fd, data + sent, len - sent, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    if (n == 0) return -1;
#endif
    sent += static_cast<std::size_t>(n);
  }
  return static_cast<ssize_type>(sent);
}

void respond_json(socket_t fd, const std::string& json_body) {
  std::string head = std::string(kVersionHeader) + "Content-Length: " + std::to_string(json_body.size()) +
                     "\r\nConnection: close\r\n\r\n";
  send_all(fd, head.data(), head.size());
  send_all(fd, json_body.data(), json_body.size());
}

void respond_bad_request(socket_t fd) { (void)send_all(fd, kCloseHeader, std::strlen(kCloseHeader)); }

void respond_forbidden(socket_t fd) {
  static constexpr char kForbidden[] =
      "HTTP/1.1 403 Forbidden\r\nContent-Type: application/json\r\nContent-Length: 55\r\nConnection: close\r\n\r\n"
      "{\"error\":{\"code\":-32002,\"message\":\"rpc not allowed\"}}";
  (void)send_all(fd, kForbidden, std::strlen(kForbidden));
}

std::vector<std::string> g_rpc_allow_ips;

bool ip_allowed(const std::string& peer_ip, const std::vector<std::string>& allow_ips) {
  if (allow_ips.empty()) return true;
  for (const auto& allowed : allow_ips) {
    if (allowed == peer_ip) return true;
    if (allowed == "0.0.0.0" || allowed == "::/0" || allowed == "*") return true;
  }
  return false;
}

std::string peer_ip_from_fd(socket_t fd) {
  sockaddr_in peer{};
#ifdef _WIN32
  int len = sizeof(peer);
#else
  socklen_t len = sizeof(peer);
#endif
  if (::getpeername(fd, reinterpret_cast<sockaddr*>(&peer), &len) != 0) return {};
  char buf[INET_ADDRSTRLEN] = {};
  if (::inet_ntop(AF_INET, &peer.sin_addr, buf, sizeof(buf)) == nullptr) return {};
  return std::string(buf);
}

void handle_connection(socket_t fd, RpcServer& server, const std::vector<std::string>& allow_ips) {
  const auto peer_ip = peer_ip_from_fd(fd);
  if (!ip_allowed(peer_ip, allow_ips)) {
    respond_forbidden(fd);
    return;
  }
  std::string raw;
  raw.reserve(4096);
  char buf[4096];
  std::size_t content_length = 0;
  std::size_t header_end = std::string::npos;
  while (raw.size() < kMaxRequestBytes) {
    if (header_end == std::string::npos) {
      const auto pos = raw.find("\r\n\r\n");
      if (pos != std::string::npos) {
        header_end = pos + 4;
        auto lower = raw;
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const auto pos2 = lower.find("content-length:");
        if (pos2 != std::string::npos && pos2 < header_end) {
          const char* start = raw.c_str() + pos2 + 15;
          content_length = static_cast<std::size_t>(std::strtoull(start, nullptr, 10));
          if (content_length > kMaxRequestBytes) { respond_bad_request(fd); return; }
        }
      }
    }
    if (header_end != std::string::npos && raw.size() >= header_end + content_length) break;
#ifdef _WIN32
    const int n = ::recv(fd, buf, static_cast<int>(sizeof buf), 0);
    if (n <= 0) return;
#else
    const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      return;
    }
    if (n == 0) return;
#endif
    raw.append(buf, static_cast<std::size_t>(n));
  }
  const std::string body = raw.substr(header_end == std::string::npos ? 0 : header_end,
                                      raw.size() - (header_end == std::string::npos ? 0 : header_end));
  auto request = parse_request(body);
  if (!request.ok()) {
    respond_bad_request(fd);
    return;
  }
  RpcRequest req;
  req.id = request.value().id;
  req.method = request.value().method;
  req.auth_token = request.value().token;
  req.params = request.value().params;
  g_rpc_remote_ip = peer_ip;
  const auto now = static_cast<std::uint64_t>(std::time(nullptr));
  auto response = server.call(req, now);
  g_rpc_remote_ip.clear();
  respond_json(fd, rpc_response_to_json(response));
}

}  // namespace

void set_rpc_allow_ips(std::vector<std::string> ips) { g_rpc_allow_ips = std::move(ips); }

Result<void> serve_rpc(RpcServer& server, const std::string& bind_host, std::uint16_t port,
                       const std::vector<std::string>& allow_ips) {
#ifdef _WIN32
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    return MakeError(ErrorCode::kIoError, "WSAStartup failed");
  }
#endif
  const socket_t listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd == kInvalidSocket) {
#ifdef _WIN32
    WSACleanup();
#endif
    return MakeError(ErrorCode::kIoError, "socket failed: " + std::string(std::strerror(errno)));
  }
  int opt = 1;
#ifdef _WIN32
  ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
  ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (bind_host.empty() || bind_host == "0.0.0.0") {
    addr.sin_addr.s_addr = INADDR_ANY;
  } else if (::inet_pton(AF_INET, bind_host.c_str(), &addr.sin_addr) != 1) {
    close_socket(listen_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return MakeError(ErrorCode::kInvalidArgument, "invalid bind host: " + bind_host);
  }
  if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close_socket(listen_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return MakeError(ErrorCode::kIoError, "bind failed: " + std::string(std::strerror(errno)));
  }
  if (::listen(listen_fd, SOMAXCONN) != 0) {
    close_socket(listen_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return MakeError(ErrorCode::kIoError, "listen failed: " + std::string(std::strerror(errno)));
  }
  std::cerr << "phantomxd: rpc listening on http://" << bind_host << ":" << port << std::endl;

  const auto effective_allow = allow_ips.empty() ? g_rpc_allow_ips : allow_ips;
  while (true) {
    sockaddr_in peer{};
#ifdef _WIN32
    int peer_len = sizeof(peer);
#else
    socklen_t peer_len = sizeof(peer);
#endif
    const socket_t conn = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&peer), &peer_len);
    if (conn == kInvalidSocket) {
#ifndef _WIN32
      if (errno == EINTR) continue;
#endif
      continue;
    }
#ifdef _WIN32
    DWORD tv = 5000;
    ::setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    ::setsockopt(conn, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    timeval tv{};
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    ::setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(conn, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    handle_connection(conn, server, effective_allow);
    close_socket(conn);
  }
}

Result<void> serve_rpc(RpcServer& server, const std::string& bind_host, std::uint16_t port) {
  return serve_rpc(server, bind_host, port, g_rpc_allow_ips);
}

}  // namespace phantomx::rpc
