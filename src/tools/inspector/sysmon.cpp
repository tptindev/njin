#include "sysmon.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#else
#include <fstream>
#include <unistd.h>
#endif

namespace inspector {
#if defined(_WIN32)

namespace {
double filetime_seconds(const FILETIME &ft) {
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  return (double)u.QuadPart * 1e-7; // 100 ns ticks
}

// One expanded GPU counter, with what kind of engine it belongs to.
struct gpu_counter {
  PDH_HCOUNTER handle = nullptr;
  enum kind_t { engine_3d, engine_copy, engine_other, dedicated, shared } kind = engine_other;
};

// A counter instance name carries the engine type: "..._engtype_3D".
gpu_counter::kind_t engine_kind(const std::wstring &path) {
  if (path.find(L"engtype_3D") != std::wstring::npos)
    return gpu_counter::engine_3d;
  if (path.find(L"engtype_Copy") != std::wstring::npos)
    return gpu_counter::engine_copy;
  return gpu_counter::engine_other;
}
} // namespace

struct sysmon::impl {
  HANDLE process = nullptr;
  double last_cpu_seconds = -1.0;
  double last_wall = 0.0;

  PDH_HQUERY query = nullptr;
  std::vector<gpu_counter> counters;
  bool query_ready = false;
  double last_expand = -1e9;
  int primed = 0; // rate counters need two collections before they read

  void close_query() {
    if (query != nullptr)
      PdhCloseQuery(query);
    query = nullptr;
    counters.clear();
    query_ready = false;
    primed = 0;
  }

  // Adds every counter of `object(pid_<pid>_*)\name` to the query.
  void add_wildcard(std::uint32_t pid, const wchar_t *object, const wchar_t *name, gpu_counter::kind_t forced,
                    bool use_engine_kind) {
    wchar_t pattern[256];
    swprintf(pattern, 256, L"\\%ls(pid_%u_*)\\%ls", object, (unsigned)pid, name);
    DWORD size = 0;
    if (PdhExpandWildCardPathW(nullptr, pattern, nullptr, &size, 0) != PDH_MORE_DATA || size == 0)
      return;
    std::vector<wchar_t> buffer(size);
    if (PdhExpandWildCardPathW(nullptr, pattern, buffer.data(), &size, 0) != ERROR_SUCCESS)
      return;
    for (const wchar_t *path = buffer.data(); *path != L'\0'; path += wcslen(path) + 1) {
      gpu_counter c;
      if (PdhAddEnglishCounterW(query, path, 0, &c.handle) != ERROR_SUCCESS)
        continue;
      c.kind = use_engine_kind ? engine_kind(path) : forced;
      counters.push_back(c);
    }
  }

  // (Re)builds the counter list: engines come and go as the game runs.
  void rebuild(std::uint32_t pid) {
    close_query();
    if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
      query = nullptr;
      return;
    }
    add_wildcard(pid, L"GPU Engine", L"Utilization Percentage", gpu_counter::engine_other, true);
    add_wildcard(pid, L"GPU Process Memory", L"Dedicated Usage", gpu_counter::dedicated, false);
    add_wildcard(pid, L"GPU Process Memory", L"Shared Usage", gpu_counter::shared, false);
    query_ready = !counters.empty();
    if (query_ready)
      PdhCollectQueryData(query);
    primed = 1;
  }

  void read_gpu(proc_sample &out) {
    if (!query_ready)
      return;
    if (PdhCollectQueryData(query) != ERROR_SUCCESS)
      return;
    if (primed < 2) {
      primed++;
      return;
    }
    double s3d = 0, scopy = 0, sother = 0, dedicated = 0, shared = 0;
    for (const gpu_counter &c : counters) {
      PDH_FMT_COUNTERVALUE v;
      DWORD type = 0;
      if (PdhGetFormattedCounterValue(c.handle, PDH_FMT_DOUBLE, &type, &v) != ERROR_SUCCESS || v.CStatus != ERROR_SUCCESS)
        continue;
      switch (c.kind) {
      case gpu_counter::engine_3d: s3d += v.doubleValue; break;
      case gpu_counter::engine_copy: scopy += v.doubleValue; break;
      case gpu_counter::engine_other: sother += v.doubleValue; break;
      case gpu_counter::dedicated: dedicated += v.doubleValue; break;
      case gpu_counter::shared: shared += v.doubleValue; break;
      }
    }
    out.gpu_valid = true;
    out.gpu_3d_percent = std::min(s3d, 100.0);
    out.gpu_copy_percent = std::min(scopy, 100.0);
    out.gpu_percent = std::min(std::max({s3d, scopy, sother}), 100.0);
    out.gpu_dedicated = dedicated;
    out.gpu_shared = shared;
  }
};

sysmon::sysmon() : p_(new impl) {
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  cores = std::max<int>(1, (int)info.dwNumberOfProcessors);
}

sysmon::~sysmon() {
  attach(0);
  delete p_;
}

void sysmon::attach(std::uint32_t pid) {
  if (pid == pid_)
    return;
  if (p_->process != nullptr)
    CloseHandle(p_->process);
  p_->process = nullptr;
  p_->close_query();
  p_->last_cpu_seconds = -1.0;
  pid_ = pid;
  last = proc_sample{};
  if (pid != 0) {
    p_->process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    p_->rebuild(pid);
    p_->last_expand = -1e9;
  }
  last_time_ = -1e9;
}

bool sysmon::update(double now, double interval) {
  if (pid_ == 0 || now - last_time_ < interval)
    return false;
  last_time_ = now;
  proc_sample s;
  if (p_->process != nullptr) {
    FILETIME create, exit_time, kernel, user;
    if (GetProcessTimes(p_->process, &create, &exit_time, &kernel, &user)) {
      const double cpu = filetime_seconds(kernel) + filetime_seconds(user);
      const double wall = now;
      if (p_->last_cpu_seconds >= 0.0 && wall > p_->last_wall) {
        const double busy = (cpu - p_->last_cpu_seconds) / (wall - p_->last_wall);
        s.cpu_core_percent = std::max(0.0, busy * 100.0);
        s.cpu_percent = std::min(100.0, s.cpu_core_percent / (double)cores);
      }
      p_->last_cpu_seconds = cpu;
      p_->last_wall = wall;
      s.valid = true;
    }
    PROCESS_MEMORY_COUNTERS_EX mem{};
    mem.cb = sizeof mem;
    if (GetProcessMemoryInfo(p_->process, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&mem), sizeof mem)) {
      s.ram_working_set = (double)mem.WorkingSetSize;
      s.ram_private = (double)mem.PrivateUsage;
      s.ram_peak = (double)mem.PeakWorkingSetSize;
      s.valid = true;
    }
    DWORD exit_code = 0;
    if (GetExitCodeProcess(p_->process, &exit_code) && exit_code != STILL_ACTIVE)
      s.valid = false;
  }
  // Re-expand the GPU counters now and then: engines appear once used.
  if (now - p_->last_expand > 5.0) {
    p_->last_expand = now;
    if (p_->primed >= 2 || p_->counters.empty())
      p_->rebuild(pid_);
  }
  p_->read_gpu(s);
  last = s;
  return true;
}

#else // ---------------------------------------------------------- Linux, macOS

struct sysmon::impl {
  double last_ticks = -1.0;
  double last_wall = 0.0;
};

sysmon::sysmon() : p_(new impl) {
#if defined(__linux__)
  cores = std::max<int>(1, (int)sysconf(_SC_NPROCESSORS_ONLN));
#endif
}
sysmon::~sysmon() { delete p_; }

void sysmon::attach(std::uint32_t pid) {
  if (pid == pid_)
    return;
  pid_ = pid;
  last = proc_sample{};
  p_->last_ticks = -1.0;
  last_time_ = -1e9;
}

bool sysmon::update(double now, double interval) {
  if (pid_ == 0 || now - last_time_ < interval)
    return false;
  last_time_ = now;
  proc_sample s;
#if defined(__linux__)
  const std::string base = "/proc/" + std::to_string(pid_);
  {
    std::ifstream stat(base + "/stat");
    std::string line;
    if (std::getline(stat, line)) {
      // Fields after the parenthesised name: state ppid ... utime(14) stime(15) ... threads(20)
      const std::size_t close = line.rfind(')');
      if (close != std::string::npos) {
        std::vector<std::string> f;
        std::size_t i = close + 2;
        while (i < line.size()) {
          const std::size_t sp = line.find(' ', i);
          f.push_back(line.substr(i, sp == std::string::npos ? std::string::npos : sp - i));
          if (sp == std::string::npos)
            break;
          i = sp + 1;
        }
        if (f.size() > 18) {
          const double ticks = std::stod(f[11]) + std::stod(f[12]);
          const double hz = (double)sysconf(_SC_CLK_TCK);
          if (p_->last_ticks >= 0.0 && now > p_->last_wall) {
            s.cpu_core_percent = std::max(0.0, (ticks - p_->last_ticks) / hz / (now - p_->last_wall) * 100.0);
            s.cpu_percent = std::min(100.0, s.cpu_core_percent / (double)cores);
          }
          p_->last_ticks = ticks;
          p_->last_wall = now;
          s.threads = std::stod(f[17]);
          s.valid = true;
        }
      }
    }
  }
  {
    std::ifstream statm(base + "/statm");
    double size = 0, resident = 0, shared = 0;
    if (statm >> size >> resident >> shared) {
      const double page = (double)sysconf(_SC_PAGESIZE);
      s.ram_working_set = resident * page;
      s.ram_private = (resident - shared) * page;
      s.valid = true;
    }
  }
#endif
  last = s;
  return true;
}

#endif
} // namespace inspector
