#include "njin_ecs.h"
#include "_comps.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include <algorithm>
#include <raylib.h>

namespace njin {
namespace {
constexpr const char *phase_names[phase_count] = {
    "startup", "pre_update", "update", "post_update", "render", "shutdown"};

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
        TraceLog(LOG_WARNING,
                 "%s/%s: system #%zu: 'after' target is not in this module "
                 "and phase, ignored",
                 label, phase_names[phase], i);
      }
    }
    for (const sys_fnc fnc : systems[i].before) {
      if (find_system(systems, fnc, other)) {
        graph.add(i, other);
      } else {
        TraceLog(LOG_WARNING,
                 "%s/%s: system #%zu: 'before' target is not in this module "
                 "and phase, ignored",
                 label, phase_names[phase], i);
      }
    }
  }
  return graph;
}

// Topological sort. Among systems that are ready, the lowest order runs
// first; ties keep registration order. On a cycle the remaining systems are
// appended in registration order.
std::vector<sys_fnc> sort_systems(const std::vector<sys_desc> &systems,
                                  const char *label, sys_phase phase) {
  const usize count = systems.size();
  dep_graph graph = build_graph(systems, label, phase);
  std::vector<bool> placed(count, false);
  std::vector<sys_fnc> sorted;
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
      TraceLog(LOG_ERROR,
               "%s/%s: cycle in after/before constraints, remaining systems "
               "keep registration order",
               label, phase_names[phase]);
      for (usize i = 0; i < count; ++i) {
        if (!placed[i]) {
          sorted.push_back(systems[i].fnc);
        }
      }
      break;
    }

    placed[best] = true;
    sorted.push_back(systems[best].fnc);
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

// Moves velocity into transform. Runs after gameplay so this frame's
// velocity changes apply immediately.
void integrate_velocity(njin_ctx &ctx) {
  for (auto [entity, tr, vel] :
       ctx.ecs.registry.view<transform, velocity>().each()) {
    (void)entity;
    tr.pos.x += vel.value.x * ctx.dt;
    tr.pos.y += vel.value.y * ctx.dt;
  }
}

void core_setup(njin_ctx &ctx) {
  ecs_register(ctx, phase_post_update, integrate_velocity);
}
} // namespace

void ecs_register(njin_ctx &ctx, sys_phase phase, sys_fnc fnc) {
  ecs_register(ctx, phase, sys_desc{.fnc = fnc});
}

void ecs_register(njin_ctx &ctx, sys_phase phase, const sys_desc &desc) {
  if (phase < 0 || phase >= phase_count || desc.fnc == nullptr) {
    TraceLog(LOG_WARNING, "ecs_register: invalid phase or null system");
    return;
  }
  if (!ctx.ecs.in_setup) {
    TraceLog(LOG_WARNING,
             "ecs_register: only allowed inside a module setup callback");
    return;
  }
  ctx.ecs.pending[phase].push_back(desc);
}

void njin_mod_register(njin_ctx &ctx, const mod_desc &desc) {
  ecs_store &ecs = ctx.ecs;
  const char *label = module_label(desc);
  if (ecs.started) {
    TraceLog(LOG_WARNING, "module %s: cannot register after njin_run", label);
    return;
  }
  if (ecs.in_setup) {
    TraceLog(LOG_WARNING,
             "module %s: cannot register from another module's setup", label);
    return;
  }
  if (desc.name != nullptr && is_registered(ecs, desc.name)) {
    TraceLog(LOG_WARNING, "module %s: already registered", label);
    return;
  }

  ecs.in_setup = true;
  if (desc.setup != nullptr) {
    desc.setup(ctx);
  }
  ecs.in_setup = false;

  for (i32 p = 0; p < phase_count; ++p) {
    const auto phase = static_cast<sys_phase>(p);
    for (const sys_fnc fnc : sort_systems(ecs.pending[p], label, phase)) {
      ecs.schedule[p].push_back(fnc);
    }
    ecs.pending[p].clear();
  }

  if (desc.name != nullptr) {
    ecs.modules.emplace_back(desc.name);
  }
  TraceLog(LOG_INFO, "module registered: %s", label);
}

mod_desc core_module() {
  return mod_desc{.name = "njin.core", .setup = core_setup};
}

void ecs_run(njin_ctx &ctx, sys_phase phase) {
  for (const sys_fnc fnc : ctx.ecs.schedule[phase]) {
    fnc(ctx);
  }
}
} // namespace njin
