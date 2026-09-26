// Reads how much CPU, memory and GPU another process (the game) uses, from
// the operating system, so the game itself pays nothing for it.
//
//   Windows  CPU: GetProcessTimes. RAM: GetProcessMemoryInfo (PSAPI).
//            GPU: the "GPU Engine" and "GPU Process Memory" performance
//            counters (PDH), which Task Manager shows too. They cover every
//            vendor on Windows 10 1709 and later.
//   Linux    CPU and RAM from /proc/<pid>; no GPU figure.
//   macOS    not implemented (everything reads as unavailable).
#pragma once
#include <cstdint>

namespace inspector {
struct proc_sample {
  bool valid = false;           // the process could be read at all
  double cpu_percent = 0;       // of the whole machine (all cores = 100)
  double cpu_core_percent = 0;  // of one core (4 busy cores = 400)
  double ram_working_set = 0;   // bytes resident
  double ram_private = 0;       // bytes committed by the process alone
  double ram_peak = 0;          // peak working set
  double threads = 0;
  bool gpu_valid = false;       // GPU counters available
  double gpu_percent = 0;       // busiest engine of the process
  double gpu_3d_percent = 0;
  double gpu_copy_percent = 0;
  double gpu_dedicated = 0;     // bytes of video memory
  double gpu_shared = 0;        // bytes of system memory used by the GPU
};

class sysmon {
public:
  sysmon();
  ~sysmon();
  sysmon(const sysmon &) = delete;
  sysmon &operator=(const sysmon &) = delete;

  // Starts watching a process. pid 0 stops.
  void attach(std::uint32_t pid);
  // Takes a new sample when at least `interval` seconds passed since the last
  // one. Returns true when `last` was updated.
  bool update(double now_seconds, double interval = 0.5);

  proc_sample last;
  int cores = 1;
  std::uint32_t pid() const { return pid_; }

private:
  struct impl;
  impl *p_ = nullptr;
  std::uint32_t pid_ = 0;
  double last_time_ = -1e9;
};
} // namespace inspector
