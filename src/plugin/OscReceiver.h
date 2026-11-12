#pragma once

// Minimal OSC receiver, matching EC2's ability to drive parameters from a script
// over UDP. Only the forms EC2 emits are parsed: an address and one numeric
// argument.
//
// The socket implementation lives in OscReceiver.cpp because winsock2.h must be
// included before windows.h, and other units pull windows.h in first.

#include <functional>
#include <memory>
#include <string>

class OscReceiver {
 public:
  using Handler = std::function<void(const std::string &, double)>;

  OscReceiver();
  ~OscReceiver();

  OscReceiver(const OscReceiver &) = delete;
  OscReceiver &operator=(const OscReceiver &) = delete;

  /// Starts a listener on the loopback interface. No-op if the port is taken.
  void start(int port, Handler handler);
  void stop();
  bool isRunning() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> m;
};
