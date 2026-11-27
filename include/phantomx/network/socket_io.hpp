#pragma once

#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#else
#include <sys/types.h>
#endif

namespace phantomx::network {

#ifdef _WIN32
using socket_t = SOCKET;
inline constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
using socket_t = int;
inline constexpr socket_t kInvalidSocket = -1;
#endif

// Writes the whole buffer, retrying short writes and EINTR. Returns false on
// any socket error. Never raises SIGPIPE on POSIX (MSG_NOSIGNAL).
[[nodiscard]] bool send_all_bytes(socket_t s, const std::uint8_t* data, std::size_t len);

// Reads exactly len bytes, retrying short reads and EINTR. Returns false on
// EOF or any socket error.
[[nodiscard]] bool recv_all_bytes(socket_t s, std::uint8_t* data, std::size_t len);

}  // namespace phantomx::network
