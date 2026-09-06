#define WIN32_LEAN_AND_MEAN
#include "native_client.h"
#include "native_protocol.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace {

std::mutex native_mutex;
SOCKET native_socket = INVALID_SOCKET;
bool winsock_started = false;

bool Transfer(void* buffer, std::size_t size, bool writing) {
  auto* p = static_cast<char*>(buffer);
  while (size != 0) {
    const int amount = size > 1u << 20 ? 1 << 20 : static_cast<int>(size);
    const int result = writing ? send(native_socket, p, amount, 0)
                               : recv(native_socket, p, amount, 0);
    if (result <= 0) return false;
    p += result;
    size -= static_cast<std::size_t>(result);
  }
  return true;
}

bool Exchange(NrNativePacket* packet) {
  if (native_socket == INVALID_SOCKET) return false;
  packet->magic = NR_NATIVE_MAGIC;
  packet->version = NR_NATIVE_PROTOCOL_VERSION;
  if (!Transfer(packet, sizeof(*packet), true) ||
      !Transfer(packet, sizeof(*packet), false)) {
    return false;
  }
  return packet->magic == NR_NATIVE_MAGIC &&
         packet->version == NR_NATIVE_PROTOCOL_VERSION &&
         packet->status == 0;
}

void CloseSocket() {
  if (native_socket != INVALID_SOCKET) {
    closesocket(native_socket);
    native_socket = INVALID_SOCKET;
  }
}

}  // namespace

bool NativeHostConnect(NativeHostInfo* info) {
  std::lock_guard<std::mutex> lock(native_mutex);
  if (native_socket != INVALID_SOCKET) return true;
  char port_text[16]{};
  char token[NR_NATIVE_TOKEN_MAX]{};
  if (!GetEnvironmentVariableA("DLSSNR_NATIVE_PORT", port_text, sizeof(port_text)) ||
      !GetEnvironmentVariableA("DLSSNR_NATIVE_TOKEN", token, sizeof(token))) {
    return false;
  }
  char* end = nullptr;
  const unsigned long port = std::strtoul(port_text, &end, 10);
  if (end == port_text || *end != '\0' || port == 0 || port > 65535 ||
      std::strlen(token) < 32) {
    return false;
  }
  if (!winsock_started) {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    winsock_started = true;
  }
  native_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (native_socket == INVALID_SOCKET) return false;
  int yes = 1;
  setsockopt(native_socket, IPPROTO_TCP, TCP_NODELAY,
             reinterpret_cast<const char*>(&yes), sizeof(yes));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<unsigned short>(port));
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(native_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    CloseSocket();
    return false;
  }
  NrNativePacket hello{};
  hello.op = NR_NATIVE_HELLO;
  std::strncpy(hello.token, token, sizeof(hello.token) - 1);
  if (!Exchange(&hello)) {
    CloseSocket();
    return false;
  }
  NrNativePacket packet{};
  packet.op = NR_NATIVE_INFO;
  if (!Exchange(&packet)) {
    CloseSocket();
    return false;
  }
  if (info != nullptr) {
    info->abi = static_cast<std::uint32_t>(packet.a[0]);
    info->source = static_cast<std::uint32_t>(packet.a[1]);
    info->tensors = static_cast<std::uint32_t>(packet.a[2]);
    info->capabilities = packet.a[3];
    info->stage1_present = static_cast<std::uint32_t>(packet.a[4]);
    info->stage1_required = static_cast<std::uint32_t>(packet.a[5]);
    info->auxiliary_tensors = static_cast<std::uint32_t>(packet.a[6]);
    std::memcpy(info->text, packet.text, sizeof(info->text));
    info->text[sizeof(info->text) - 1] = '\0';
  }
  return true;
}

bool NativeHostConnected() {
  std::lock_guard<std::mutex> lock(native_mutex);
  return native_socket != INVALID_SOCKET;
}

bool NativeHostPing() {
  std::lock_guard<std::mutex> lock(native_mutex);
  if (native_socket == INVALID_SOCKET) return false;
  NrNativePacket packet{};
  packet.op = NR_NATIVE_PING;
  packet.a[0] = 41;
  if (!Exchange(&packet) || packet.a[0] != 42) {
    CloseSocket();
    return false;
  }
  return true;
}

void NativeHostDisconnect() {
  std::lock_guard<std::mutex> lock(native_mutex);
  if (native_socket != INVALID_SOCKET) {
    NrNativePacket packet{};
    packet.op = NR_NATIVE_CLOSE;
    Exchange(&packet);
    CloseSocket();
  }
  if (winsock_started) {
    WSACleanup();
    winsock_started = false;
  }
}
