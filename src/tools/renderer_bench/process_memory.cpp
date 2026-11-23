// Isolate Windows headers from the legacy raylib types in internal diagnostics.
#include <array>
#include <cstdint>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif
std::array<std::uint64_t,2> njin_bench_process_memory() {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS_EX pm{};pm.cb=sizeof(pm);
  if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm),sizeof(pm)))
    return {pm.WorkingSetSize,pm.PrivateUsage};
#endif
  return {};
}
