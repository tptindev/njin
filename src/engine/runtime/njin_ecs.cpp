#include "njin_ecs.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_log.h"
#include <algorithm>
#include <chrono>

namespace njin {
namespace {
constexpr const char *phase_names[phase_count] = {
    "startup",    "pre_update", "fixed_update", "update",  "post_update",
    "pre_render", "render",     "post_render",  "shutdown"};

// Adjacency list of "a must run before b" edges between systems of one
// module and phase, indexed by position in the pending list.
struct dep_graph {
  std::vector<std::vector<usize>> next;
  std::vector<usize> indegree;

  explicit dep_graph(usize count) : next(count), indegree(count, 0) {}

  void add(usize first, usize then) {
    std::vector<usize> &edges = next[first];
    if (std::find(edges.begin(), edges.end(), then) == edges.end()) {
      edges.push_back(then);
      ++indegree[then];
    }
  }
};

const char *module_label(const mod_desc &desc) {
  return desc.name != nullptr ? desc.name : "<unnamed>";
}

bool find_system(const std::vector<sys_desc> &systems, sys_fnc fnc,
                 usize &index) {
  for (usize i = 0; i < systems.size(); ++i) {
    if (systems[i].fnc == fnc) {
      index = i;
      return true;
    }
  }
  return false;
}

dep_graph build_graph(const std::vector<sys_desc> &systems, const char *label,
                      sys_phase phase) {
  dep_graph graph(systems.size());
  for (usize i = 0; i < systems.size(); ++i) {
    usize other = 0;
    for (const sys_fnc fnc : systems[i].after) {
      if (find_system(systems, fnc, other)) {
        graph.add(other, i);
      } else {
        NJIN_WARN("%s/%s: system #%zu: 'after' target is not in this "
                  "module and phase, ignored",
                  label, phase_names[phase], i);
      }
    }
    for (const sys_fnc fnc : systems[i].before) {
      if (find_system(systems, fnc, other)) {
        graph.add(i, other);
      } else {
        NJIN_WARN("%s/%s: system #%zu: 'before' target is not in this "
                  "module and phase, ignored",
                  label, phase_names[phase], i);
      }
    }
  }
  return graph;
}

// Topological sort. Among systems that are ready, the lowest order runs
// first; ties keep registration order. On a cycle the remaining systems are
// appended in registration order. Returns indices into `systems`.
std::vector<usize> sort_systems(const std::vector<sys_desc> &systems,
                                  const char *label, sys_phase phase) {
  const usize count = systems.size();
  dep_graph graph = build_graph(systems, label, phase);
  std::vector<bool> placed(count, false);
  std::vector<usize> sorted;
  sorted.reserve(count);

  while (sorted.size() < count) {
    usize best = count;
    for (usize i = 0; i < count; ++i) {
      if (placed[i] || graph.indegree[i] != 0) {
        continue;
      }
      if (best == count || systems[i].order < systems[best].order) {
        best = i;
      }
    }

    if (best == count) {
      NJIN_ERROR("%s/%s: cycle in after/before constraints, remaining "
                 "systems keep registration order",
                 label, phase_names[phase]);
      for (usize i = 0; i < count; ++i) {
        if (!placed[i]) {
          sorted.push_back(i);
        }
      }
      break;
    }

    placed[best] = true;
    sorted.push_back(best);
    for (const usize then : graph.next[best]) {
      --graph.indegree[then];
    }
  }
  return sorted;
}

bool is_registered(const ecs_store &ecs, const char *name) {
  return std::find(ecs.modules.begin(), ecs.modules.end(), name) !=
         ecs.modules.end();
}
} // namespace

void ecs_register(context &ctx, sys_phase phase, sys_fnc fnc, const char *name) {
  ecs_register(ctx, phase, sys_desc{.fnc = fnc, .name = name});
}

void ecs_register(context &ctx, sys_phase phase, const sys_desc &desc) {
  if (phase < 0 || phase >= phase_count || desc.fnc == nullptr) {
    NJIN_WARN("ecs_register: invalid phase or null system");
    return;
  }
  if (!ctx.ecs.in_setup) {
    NJIN_WARN("ecs_register: only allowed inside a module setup callback");
    return;
  }
  ctx.ecs.pending[phase].push_back(desc);
}

void mod_register(context &ctx, const mod_desc &desc) {
  ecs_store &ecs = ctx.ecs;
  const char *label = module_label(desc);
  if (ecs.started) {
    NJIN_WARN("module %s: cannot register after run", label);
    return;
  }
  if (ecs.in_setup) {
    NJIN_WARN("module %s: cannot register from another module's setup", label);
    return;
  }
  if (desc.name != nullptr && is_registered(ecs, desc.name)) {
    NJIN_WARN("module %s: already registered", label);
    return;
  }

  ecs.in_setup = true;
  if (desc.setup != nullptr) {
    desc.setup(ctx);
  }
  ecs.in_setup = false;

  for (i32 p = 0; p < phase_count; ++p) {
    const auto phase = static_cast<sys_phase>(p);
    const std::vector<sys_desc> &pending = ecs.pending[p];
    for (const usize i : sort_systems(pending, label, phase)) {
      const std::string name = pending[i].name != nullptr
                                   ? std::string(pending[i].name)
                                   : "#" + std::to_string(ecs.schedule[p].size());
      ecs.schedule[p].push_back(scheduled_system{.fnc = pending[i].fnc,
                                                 .scene = pending[i].scene,
                                                 .label = std::string(label) + "/" + name,
                                                 .stat = {}});
    }
    ecs.pending[p].clear();
  }

  if (desc.name != nullptr) {
    ecs.modules.emplace_back(desc.name);
  }
  NJIN_INFO("module registered: %s", label);
}

void mod_register(context &ctx, std::span<const mod_desc> mods) {
  for (const mod_desc &desc : mods)
    mod_register(ctx, desc);
}

void mod_register(context &ctx, std::initializer_list<mod_desc> mods) {
  mod_register(ctx, std::span<const mod_desc>(mods.begin(), mods.size()));
}

void ecs_run(context &ctx, sys_phase phase) {
  const u32 current = ctx.scene.current.id;
  ecs_store &ecs = ctx.ecs;
  if (!ecs.profile) {
    for (const scheduled_system &sys : ecs.schedule[phase]) {
      if (sys.scene.id == 0 || sys.scene.id == current)
        sys.fnc(ctx);
    }
    return;
  }
  using clock = std::chrono::steady_clock;
  const auto phase_start = clock::now();
  for (scheduled_system &sys : ecs.schedule[phase]) {
    if (sys.scene.id != 0 && sys.scene.id != current)
      continue;
    const auto start = clock::now();
    sys.fnc(ctx);
    sys.stat.accum += std::chrono::duration<f32, std::milli>(clock::now() - start).count();
    sys.stat.calls_accum++;
  }
  ecs.phase_accum[phase] += std::chrono::duration<f32, std::milli>(clock::now() - phase_start).count();
}

void ecs_profile_roll(ecs_store &ecs) {
  for (i32 p = 0; p < phase_count; p++) {
    ecs.phase_last[p] = ecs.phase_accum[p];
    ecs.phase_accum[p] = 0.0f;
    for (scheduled_system &sys : ecs.schedule[p]) {
      sys_stat &s = sys.stat;
      s.last = s.accum;
      s.calls = s.calls_accum;
      s.avg += (s.last - s.avg) * 0.1f;
      s.peak = std::max(s.peak * 0.995f, s.last);
      s.accum = 0.0f;
      s.calls_accum = 0;
    }
  }
}
} // namespace njin
