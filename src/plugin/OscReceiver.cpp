// winsock2.h must precede windows.h; keeping this in its own translation unit is
// the only reliable way to guarantee that inside a plugin that also uses Win32.

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "OscReceiver.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

struct OscReceiver::Impl {
#ifdef _WIN32
  SOCKET socket = INVALID_SOCKET;
  std::thread thread;
  std::atomic<bool> running{false};
  Handler handler;
#endif
};

OscReceiver::OscReceiver() : m(new Impl()) {}

OscReceiver::~OscReceiver() { stop(); }

bool OscReceiver::isRunning() const {
#ifdef _WIN32
  return m->running.load();
#else
  return false;
#endif
}

void OscReceiver::stop() {
#ifdef _WIN32
  if (!m->running.exchange(false)) return;
  if (m->thread.joinable()) m->thread.join();
  if (m->socket != INVALID_SOCKET) {
    closesocket(m->socket);
    m->socket = INVALID_SOCKET;
    WSACleanup();
  }
#endif
}

#ifdef _WIN32
namespace {

uint32_t readInt(const char *p) {
  return (static_cast<uint32_t>(static_cast<unsigned char>(p[0])) << 24) |
         (static_cast<uint32_t>(static_cast<unsigned char>(p[1])) << 16) |
         (static_cast<uint32_t>(static_cast<unsigned char>(p[2])) << 8) |
         static_cast<uint32_t>(static_cast<unsigned char>(p[3]));
}

bool parse(const char *data, int size, std::string &address, double &value) {
  if (size < 8) return false;

  int pos = 0;
  while (pos < size && data[pos] != '\0') ++pos;
  if (pos >= size) return false;
  address.assign(data, static_cast<size_t>(pos));
  pos = (pos + 4) & ~3;

  if (pos >= size || data[pos] != ',') return false;
  const int tagStart = pos;
  while (pos < size && data[pos] != '\0') ++pos;
  if (pos >= size) return false;
  const std::string tags(data + tagStart, static_cast<size_t>(pos - tagStart));
  pos = (pos + 4) & ~3;

  if (pos + 4 > size) return false;

  const char type = tags.size() > 1 ? tags[1] : 'f';
  if (type == 'f' || type == 'd') {
    const uint32_t bits = readInt(data + pos);
    float f = 0.f;
    std::memcpy(&f, &bits, sizeof(f));
    value = static_cast<double>(f);
  } else if (type == 'i') {
    value = static_cast<double>(static_cast<int32_t>(readInt(data + pos)));
  } else {
    return false;
  }
  return true;
}

} // namespace
#endif

void OscReceiver::start(int port, Handler handler) {
#ifdef _WIN32
  if (m->running.load()) return;

  WSADATA data;
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return;

  m->socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (m->socket == INVALID_SOCKET) { WSACleanup(); return; }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<unsigned short>(port));
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (::bind(m->socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
    closesocket(m->socket);
    m->socket = INVALID_SOCKET;
    WSACleanup();
    return;
  }

  DWORD timeout = 100;
  setsockopt(m->socket, SOL_SOCKET, SO_RCVTIMEO,
             reinterpret_cast<const char *>(&timeout), sizeof(timeout));

  m->handler = std::move(handler);
  m->running.store(true);
  m->thread = std::thread([this] {
    std::vector<char> buffer(4096);
    while (m->running.load()) {
      const int received = ::recv(m->socket, buffer.data(), static_cast<int>(buffer.size()), 0);
      if (received <= 0) continue;

      std::string address;
      double value = 0.0;
      if (parse(buffer.data(), received, address, value) && m->handler) m->handler(address, value);
    }
  });
#endif
}
