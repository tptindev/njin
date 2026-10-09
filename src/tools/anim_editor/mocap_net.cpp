#include "mocap_net.h"
#include <chrono>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

namespace anim_editor {
namespace {
std::wstring wide(const std::string &s) {
  if (s.empty())
    return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
  return w;
}
std::wstring quoted(const std::string &s) { return L"\"" + wide(s) + L"\""; }
bool winsock() {
  static bool ready = [] {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  return ready;
}
} // namespace

MocapLink::~MocapLink() { close(); }
bool MocapLink::listen(int port, std::string &error) {
  close();
  if (!winsock()) {
    error = "Winsock did not start";
    return false;
  }
  SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) {
    error = "cannot create a UDP socket";
    return false;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons((u_short)port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  u_long nonblocking = 1;
  int big = 1 << 20;
  ::setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&big), sizeof(big));
  if (::bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 || ::ioctlsocket(s, FIONBIO, &nonblocking) != 0) {
    error = "port " + std::to_string(port) + " is in use";
    ::closesocket(s);
    return false;
  }
  socket_ = (unsigned long long)s;
  return true;
}
void MocapLink::close() {
  if (open())
    ::closesocket((SOCKET)socket_);
  socket_ = ~0ull;
}
void MocapLink::receive(std::vector<std::vector<unsigned char>> &out) {
  out.clear();
  if (!open())
    return;
  char buffer[4096];
  for (;;) {
    const int n = ::recv((SOCKET)socket_, buffer, sizeof(buffer), 0);
    if (n <= 0)
      break;
    out.emplace_back(buffer, buffer + n);
  }
}

Sidecar::~Sidecar() { stop(); }
bool Sidecar::start(const std::string &python, const std::string &script, const std::vector<std::string> &args,
                    const std::string &log, std::string &error) {
  stop();
  std::wstring command = quoted(python) + L" -u " + quoted(script);
  for (const auto &a : args)
    command += L" " + quoted(a);
  SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
  HANDLE out = CreateFileW(wide(log).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  if (out != INVALID_HANDLE_VALUE) {
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out;
    si.hStdError = out;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  }
  PROCESS_INFORMATION pi{};
  const BOOL ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                 &si, &pi);
  if (out != INVALID_HANDLE_VALUE)
    CloseHandle(out);
  if (!ok) {
    error = "cannot start " + python + " (run mocap\\setup.bat first)";
    return false;
  }
  CloseHandle(pi.hThread);
  process_ = pi.hProcess;
  exit_code_ = 0;
  return true;
}
bool Sidecar::running() {
  if (!process_)
    return false;
  DWORD code = 0;
  if (GetExitCodeProcess((HANDLE)process_, &code) && code == STILL_ACTIVE)
    return true;
  exit_code_ = (int)code;
  CloseHandle((HANDLE)process_);
  process_ = nullptr;
  return false;
}
void Sidecar::stop() {
  if (!process_)
    return;
  TerminateProcess((HANDLE)process_, 0);
  WaitForSingleObject((HANDLE)process_, 2000);
  CloseHandle((HANDLE)process_);
  process_ = nullptr;
}
double clock_seconds() {
  return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}
} // namespace anim_editor

#else
namespace anim_editor {
MocapLink::~MocapLink() {}
bool MocapLink::listen(int, std::string &error) {
  error = "webcam capture is only built on Windows";
  return false;
}
void MocapLink::close() {}
void MocapLink::receive(std::vector<std::vector<unsigned char>> &out) { out.clear(); }
Sidecar::~Sidecar() {}
bool Sidecar::start(const std::string &, const std::string &, const std::vector<std::string> &, const std::string &,
                    std::string &error) {
  error = "webcam capture is only built on Windows";
  return false;
}
bool Sidecar::running() { return false; }
void Sidecar::stop() {}
double clock_seconds() {
  return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}
} // namespace anim_editor
#endif
