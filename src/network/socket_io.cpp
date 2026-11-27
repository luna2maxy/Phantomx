#include "phantomx/network/socket_io.hpp"

#include <cerrno>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

namespace phantomx::network {

bool send_all_bytes(socket_t s, const std::uint8_t* data, std::size_t len) {
#ifdef SO_NOSIGPIPE
  // macOS/BSD have no MSG_NOSIGNAL; suppress SIGPIPE per socket instead.
  int set = 1;
  (void)::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof(set));
#endif
  std::size_t sent = 0;
  while (sent < len) {
    int flags = 0;
#ifdef MSG_NOSIGNAL
    // A closed peer must surface as an error, not as a process-killing SIGPIPE.
    flags |= MSG_NOSIGNAL;
#endif
    int n = 0;
#ifdef _WIN32
    n = ::send(s, reinterpret_cast<const char*>(data + sent), static_cast<int>(len - sent), flags);
    if (n <= 0) return false;
#else
    const auto written = ::send(s, data + sent, len - sent, flags);
    if (written < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (written == 0) return false;
    n = static_cast<int>(written);
#endif
    sent += static_cast<std::size_t>(n);
  }
  return true;
}

bool recv_all_bytes(socket_t s, std::uint8_t* data, std::size_t len) {
  std::size_t got = 0;
  while (got < len) {
#ifdef _WIN32
    const int n = ::recv(s, reinterpret_cast<char*>(data + got), static_cast<int>(len - got), 0);
    if (n <= 0) return false;
    got += static_cast<std::size_t>(n);
#else
    const auto n = ::recv(s, data + got, len - got, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (n == 0) return false;
    got += static_cast<std::size_t>(n);
#endif
  }
  return true;
}

}  // namespace phantomx::network
