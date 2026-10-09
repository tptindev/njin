#pragma once
#include "njin_internal_only.h"

#include "njin_script_impl.h"
#include <entt/entity/entity.hpp>
#include <sol/sol.hpp>
#include <string>
#include <unordered_map>

namespace njin {
// One script file attached to entities: the table it returned, and the
// metatable every entity's `self` shares, whose __index is swapped on reload.
struct script_class {
  sol::table module;
  sol::table meta;
  sol::protected_function on_start, on_update, on_fixed_update, on_render, on_destroy, on_reload;
};

struct script_instance {
  entt::entity entity = entt::null;
  std::string path;
  sol::table self;
  bool started = false;
};

struct script_runtime {
  sol::state lua;
  script_desc desc;
  sol::protected_function traceback;
  std::unordered_map<std::string, script_class> classes;
  std::unordered_map<u32, script_instance> instances;
  std::vector<std::string> run_files; // script_run_file() paths, run again on change
};

// The runtime, created with default options if no script_init() came first.
script_runtime &script_rt(context &ctx);

// Lua value <-> script_value.
sol::object script_to_lua(sol::state_view lua, const script_value &v);
script_value script_from_lua(const sol::object &o);

// Logs a Lua error (its message already carries file:line) and returns false.
bool script_report(const sol::protected_function_result &r, const char *what);

// Calls `fn` (if set) with the error handler; logs and returns false on error.
template <class... A> bool script_pcall(const sol::protected_function &fn, const char *what, A &&...args) {
  if (!fn.valid())
    return true;
  const sol::protected_function_result r = fn(std::forward<A>(args)...);
  return r.valid() || script_report(r, what);
}

inline entt::entity script_entity(lua_Integer v) { return v < 0 ? entt::entity(entt::null) : entt::entity((u32)v); }
} // namespace njin
