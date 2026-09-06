#pragma once
#include <cstdint>

struct NativeHostInfo {
  std::uint32_t abi = 0;
  std::uint32_t source = 0;
  std::uint32_t tensors = 0;
  std::uint64_t capabilities = 0;
  char text[256]{};
};

bool NativeHostConnect(NativeHostInfo* info);
bool NativeHostConnected();
bool NativeHostPing();
void NativeHostDisconnect();
