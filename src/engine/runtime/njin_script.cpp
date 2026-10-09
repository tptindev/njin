#include "njin_script_rt.h"

#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_file.h"
#include "njin_log.h"
#include "njin_path.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_set>

namespace njin {
namespace {
// Tag on every scripted entity, so destroying the entity runs on_destroy.
struct script_tag {};

std::unordered_set<std::string> &reported_errors() {
  static std::unordered_set<std::string> set;
  return set;
}

lua_Integer to_lua(entt::entity e) { return (lua_Integer)entt::to_integral(e); }

void on_entity_destroyed(context &ctx, entt::registry &, entt::entity e);

// Lua's own require searcher reads files with fopen from the working folder;
// this one goes through asset_path() and file_read() like every njin loader,
// and loads text only.
int asset_searcher(lua_State *L) {
  std::string name = luaL_checkstring(L, 1);
  std::replace(name.begin(), name.end(), '.', '/');
  for (const char *pattern : {"%s.lua", "%s/init.lua", "scripts/%s.lua"}) {
    char buf[512];
    std::snprintf(buf, sizeof buf, pattern, name.c_str());
    const std::string path = asset_path(buf);
    std::string code;
    if (!file_exists(path.c_str()) || !file_read(path.c_str(), code))
      continue;
    const std::string chunk = std::string("@") + buf;
    if (luaL_loadbufferx(L, code.data(), code.size(), chunk.c_str(), "t") != LUA_OK)
      return lua_error(L);
    lua_pushstring(L, buf);
    return 2;
  }
  lua_pushfstring(L, "\n\tno script '%s.lua'", name.c_str());
  return 1;
}

int lua_log(lua_State *L, log_level level) {
  lua_Debug ar{};
  const char *src = nullptr;
  i32 line = 0;
  if (lua_getstack(L, 1, &ar) && lua_getinfo(L, "Sl", &ar)) {
    src = ar.source[0] == '@' ? ar.source + 1 : ar.short_src;
    line = ar.currentline;
  }
  std::string msg;
  const int n = lua_gettop(L);
  for (int i = 1; i <= n; i++) {
    if (i > 1)
      msg += '\t';
    size_t len = 0;
    const char *s = luaL_tolstring(L, i, &len);
    msg.append(s, len);
    lua_pop(L, 1);
  }
  log_write(level, src, line, "%s", msg.c_str());
  return 0;
}
int lua_print(lua_State *L) { return lua_log(L, log_info); }
int lua_warn(lua_State *L) { return lua_log(L, log_warn); }
int lua_error_log(lua_State *L) { return lua_log(L, log_error); }

void open_sandbox(script_runtime &rt) {
  sol::state &lua = rt.lua;
  lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::coroutine, sol::lib::string, sol::lib::table,
                     sol::lib::math, sol::lib::utf8, sol::lib::os, sol::lib::debug);
  if (rt.desc.allow_io)
    lua.open_libraries(sol::lib::io);

  rt.traceback = lua["debug"]["traceback"];
  // debug can rewrite any metatable or upvalue; scripts get its traceback only.
  lua["debug"] = lua.create_table_with("traceback", rt.traceback);

  sol::table os = lua["os"];
  sol::table safe_os = lua.create_table_with("clock", os["clock"], "time", os["time"], "date", os["date"],
                                             "difftime", os["difftime"]);
  if (rt.desc.allow_io) {
    safe_os["remove"] = os["remove"];
    safe_os["rename"] = os["rename"];
    safe_os["tmpname"] = os["tmpname"];
    lua["io"]["popen"] = sol::lua_nil;
  } else {
    lua["dofile"] = sol::lua_nil;
    lua["loadfile"] = sol::lua_nil;
  }
  lua["os"] = safe_os;

  sol::table package = lua["package"];
  package["loadlib"] = sol::lua_nil;
  package["cpath"] = "";
  sol::table searchers = package["searchers"];
  sol::table only = lua.create_table();
  only[1] = searchers[1]; // package.preload
  only[2] = &asset_searcher;
  package["searchers"] = only;

  lua["print"] = &lua_print;
  // load() of precompiled bytecode can crash the VM: text only.
  lua.script(R"(local raw = load
load = function(chunk, name, mode, env) return raw(chunk, name, "t", env) end)",
             "=njin");
}

std::unique_ptr<script_runtime> make_runtime(context &ctx, const script_desc &desc) {
  auto rt = std::make_unique<script_runtime>();
  rt->desc = desc;
  open_sandbox(*rt);
  script_bind_njin(ctx, *rt);
  sol::table njin = rt->lua["njin"];
  script_bind_njin_more(ctx, *rt, njin);
  njin["log"] = &lua_print;
  njin["warn"] = &lua_warn;
  njin["error"] = &lua_error_log;
  world(ctx).on_destroy<script_tag>().connect<&on_entity_destroyed>(ctx);
  return rt;
}

// Runs code; returns its first result as an object (nil if none). Results
// live on the Lua stack, which the load result pops when it goes, so the
// value is taken out here.
sol::object run_code(script_runtime &rt, const std::string &code, const std::string &chunk, bool &ok) {
  ok = false;
  sol::protected_function fn;
  {
    sol::load_result loaded = rt.lua.load(code, chunk, sol::load_mode::text);
    if (!loaded.valid()) {
      const sol::error err = loaded;
      log_write(log_error, nullptr, 0, "script: %s", err.what());
      return sol::make_object(rt.lua, sol::lua_nil);
    }
    fn = loaded;
  }
  fn.set_error_handler(rt.traceback);
  const sol::protected_function_result r = fn();
  if (!r.valid()) {
    script_report(r, chunk.c_str() + 1);
    return sol::make_object(rt.lua, sol::lua_nil);
  }
  ok = true;
  return r.return_count() > 0 ? r.get<sol::object>(0) : sol::make_object(rt.lua, sol::lua_nil);
}

bool read_script(const char *path, std::string &out) {
  const std::string resolved = asset_path(path);
  if (!file_read(resolved.c_str(), out)) {
    log_write(log_error, nullptr, 0, "script: cannot read %s", path);
    return false;
  }
  return true;
}

void set_handlers(script_runtime &rt, script_class &c) {
  const auto get = [&](const char *name) {
    sol::protected_function f;
    sol::object o = c.module[name];
    if (o.get_type() == sol::type::function) {
      f = o.as<sol::protected_function>();
      f.set_error_handler(rt.traceback);
    }
    return f;
  };
  c.on_start = get("on_start");
  c.on_update = get("on_update");
  c.on_fixed_update = get("on_fixed_update");
  c.on_render = get("on_render");
  c.on_destroy = get("on_destroy");
  c.on_reload = get("on_reload");
  c.on_load = get("on_load");
  c.on_ui = get("on_ui");
}

// Runs a class file; it must return a table.
bool load_class(script_runtime &rt, const std::string &path, sol::table &out) {
  std::string code;
  if (!read_script(path.c_str(), code))
    return false;
  bool ok = false;
  const sol::object result = run_code(rt, code, "@" + path, ok);
  if (!ok)
    return false;
  if (result.get_type() != sol::type::table) {
    log_write(log_error, nullptr, 0, "script: %s must return a table (local M = {} ... return M)", path.c_str());
    return false;
  }
  out = result.as<sol::table>();
  return true;
}

script_class *class_of(script_runtime &rt, const std::string &path) {
  const auto it = rt.classes.find(path);
  if (it != rt.classes.end())
    return &it->second;
  sol::table module;
  if (!load_class(rt, path, module))
    return nullptr;
  script_class c;
  c.module = module;
  c.meta = rt.lua.create_table_with("__index", module);
  set_handlers(rt, c);
  return &rt.classes.emplace(path, std::move(c)).first->second;
}

void finish(script_runtime &rt, script_instance &inst) {
  const auto it = rt.classes.find(inst.path);
  if (it != rt.classes.end())
    script_pcall(it->second.on_destroy, "on_destroy", inst.self);
}

void on_entity_destroyed(context &ctx, entt::registry &, entt::entity e) {
  script_runtime *rt = ctx.script.rt.get();
  if (rt == nullptr)
    return;
  const auto it = rt->instances.find(entt::to_integral(e));
  if (it == rt->instances.end())
    return;
  script_instance inst = std::move(it->second);
  rt->instances.erase(it);
  finish(*rt, inst);
}

std::vector<u32> instance_ids(const script_runtime &rt) {
  std::vector<u32> ids;
  ids.reserve(rt.instances.size());
  for (const auto &[id, inst] : rt.instances)
    ids.push_back(id);
  std::sort(ids.begin(), ids.end()); // a stable order: scripts run oldest entity first
  return ids;
}

// Runs on_start the first time an entity's script is reached, in whichever of
// fixed_update and update comes first. False when on_start detached it.
bool start_once(script_runtime &rt, u32 id, script_class &c) {
  auto it = rt.instances.find(id);
  if (it->second.started)
    return true;
  it->second.started = true;
  const sol::table self = it->second.self;
  script_pcall(c.on_start, "on_start", self);
  return rt.instances.find(id) != rt.instances.end();
}

void update(context &ctx) {
  script_runtime *rt = ctx.script.rt.get();
  if (rt == nullptr || rt->instances.empty())
    return;
  const f32 dt = delta(ctx);
  for (const u32 id : instance_ids(*rt)) {
    auto it = rt->instances.find(id);
    if (it == rt->instances.end())
      continue;
    script_class *c = class_of(*rt, it->second.path);
    if (c == nullptr || !start_once(*rt, id, *c))
      continue;
    const sol::table self = rt->instances.find(id)->second.self;
    script_pcall(c->on_update, "on_update", self, dt);
  }
}

// on_fixed_update(self, dt) at the fixed step, before the 3D physics step, so
// velocities set here move bodies in the same step. Nothing runs (no entity
// walk) while no loaded script has the function.
void fixed_update(context &ctx) {
  script_runtime *rt = ctx.script.rt.get();
  if (rt == nullptr || rt->instances.empty())
    return;
  bool any = false;
  for (const auto &[path, c] : rt->classes)
    any = any || c.on_fixed_update.valid();
  if (!any)
    return;
  const f32 dt = delta(ctx);
  for (const u32 id : instance_ids(*rt)) {
    const auto it = rt->instances.find(id);
    if (it == rt->instances.end())
      continue;
    script_class *c = class_of(*rt, it->second.path);
    if (c == nullptr || !c->on_fixed_update.valid() || !start_once(*rt, id, *c))
      continue;
    const sol::table self = rt->instances.find(id)->second.self;
    script_pcall(c->on_fixed_update, "on_fixed_update", self, dt);
  }
}

void render(context &ctx) {
  script_runtime *rt = ctx.script.rt.get();
  if (rt == nullptr || rt->instances.empty())
    return;
  for (const u32 id : instance_ids(*rt)) {
    const auto it = rt->instances.find(id);
    if (it == rt->instances.end() || !it->second.started)
      continue;
    const script_class *c = class_of(*rt, it->second.path);
    if (c == nullptr || !c->on_render.valid())
      continue;
    const sol::table self = it->second.self;
    script_pcall(c->on_render, "on_render", self);
  }
}

// on_ui(self) in phase_post_render, screen space: where njin's immediate UI
// (ui_begin()...) has to be called.
void ui(context &ctx) {
  script_runtime *rt = ctx.script.rt.get();
  if (rt == nullptr || rt->instances.empty())
    return;
  bool any = false;
  for (const auto &[path, c] : rt->classes)
    any = any || c.on_ui.valid();
  if (!any)
    return;
  for (const u32 id : instance_ids(*rt)) {
    const auto it = rt->instances.find(id);
    if (it == rt->instances.end() || !it->second.started)
      continue;
    const script_class *c = class_of(*rt, it->second.path);
    if (c == nullptr || !c->on_ui.valid())
      continue;
    const sol::table self = it->second.self;
    script_pcall(c->on_ui, "on_ui", self);
  }
}

void setup(context &ctx) {
  ecs_register(ctx, phase_fixed_update, fixed_update, "fixed_update");
  ecs_register(ctx, phase_update, update, "update");
  ecs_register(ctx, phase_render, render, "render");
  ecs_register(ctx, phase_post_render, ui, "ui");
}

// Walks "a.b.c": the table holding the last name (created when `create`), and
// that name.
bool resolve(sol::state &lua, const char *dotted, bool create, sol::table &table, std::string &key) {
  std::string name = dotted == nullptr ? "" : dotted;
  table = lua.globals();
  usize start = 0;
  for (usize dot = name.find('.'); dot != std::string::npos; dot = name.find('.', start)) {
    const std::string part = name.substr(start, dot - start);
    sol::object next = table[part];
    if (next.get_type() != sol::type::table) {
      if (!create)
        return false;
      next = lua.create_table();
      table[part] = next;
    }
    table = next.as<sol::table>();
    start = dot + 1;
  }
  key = name.substr(start);
  return !key.empty();
}
} // namespace

script_state::script_state() = default;
script_state::~script_state() = default;

mod_desc script_module() { return mod_desc{.name = "njin.script", .setup = setup}; }

script_runtime &script_rt(context &ctx) {
  if (!ctx.script.rt)
    ctx.script.rt = make_runtime(ctx, {});
  return *ctx.script.rt;
}

bool script_report(const sol::protected_function_result &r, const char *what) {
  const sol::error err = r;
  // An on_update that fails fails every frame: each message is logged once,
  // until a hot reload clears the list.
  std::string msg = std::string(what) + ": " + err.what();
  if (reported_errors().insert(msg).second)
    log_write(log_error, nullptr, 0, "script (%s)", msg.c_str());
  return false;
}

sol::object script_to_lua(sol::state_view lua, const script_value &v) {
  return std::visit(
      [&](const auto &x) -> sol::object {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, std::monostate>)
          return sol::make_object(lua, sol::lua_nil);
        else if constexpr (std::is_same_v<T, f64>) {
          if (std::isfinite(x) && x == std::floor(x) && std::fabs(x) < 9.0e15)
            return sol::make_object(lua, (lua_Integer)x);
          return sol::make_object(lua, x);
        } else if constexpr (std::is_same_v<T, entt::entity>) {
          if (x == entt::null)
            return sol::make_object(lua, sol::lua_nil);
          return sol::make_object(lua, to_lua(x));
        } else
          return sol::make_object(lua, x);
      },
      v);
}

script_value script_from_lua(const sol::object &o) {
  switch (o.get_type()) {
  case sol::type::boolean:
    return o.as<bool>();
  case sol::type::number:
    return o.as<f64>();
  case sol::type::string:
    return o.as<std::string>();
  case sol::type::userdata:
    if (o.is<vec2>())
      return o.as<vec2>();
    if (o.is<vec3>())
      return o.as<vec3>();
    return {};
  default:
    return {};
  }
}

void script_init(context &ctx, const script_desc &desc) {
  if (!ctx.script.rt)
    ctx.script.rt = make_runtime(ctx, desc);
}

bool script_run_file(context &ctx, const char *path) {
  if (path == nullptr)
    return false;
  script_runtime &rt = script_rt(ctx);
  std::string code;
  if (!read_script(path, code))
    return false;
  if (std::find(rt.run_files.begin(), rt.run_files.end(), path) == rt.run_files.end())
    rt.run_files.emplace_back(path);
  bool ok = false;
  run_code(rt, code, std::string("@") + path, ok);
  return ok;
}

bool script_run_string(context &ctx, const char *code, const char *name) {
  if (code == nullptr)
    return false;
  bool ok = false;
  run_code(script_rt(ctx), code, std::string("=") + (name == nullptr ? "string" : name), ok);
  return ok;
}

script_result script_call(context &ctx, const char *function, std::initializer_list<script_value> args) {
  return script_call(ctx, function, std::span<const script_value>(args.begin(), args.size()));
}

script_result script_call(context &ctx, const char *function, std::span<const script_value> args) {
  script_runtime &rt = script_rt(ctx);
  sol::table table;
  std::string key;
  sol::object fn_obj;
  if (resolve(rt.lua, function, false, table, key))
    fn_obj = table[key];
  if (fn_obj.get_type() != sol::type::function) {
    log_write(log_error, nullptr, 0, "script: no function '%s'", function == nullptr ? "" : function);
    return {};
  }
  sol::protected_function fn = fn_obj.as<sol::protected_function>();
  fn.set_error_handler(rt.traceback);
  std::vector<sol::object> lua_args;
  lua_args.reserve(args.size());
  for (const script_value &a : args)
    lua_args.push_back(script_to_lua(rt.lua, a));
  const sol::protected_function_result r = fn(sol::as_args(lua_args));
  if (!r.valid()) {
    script_report(r, function);
    return {};
  }
  script_result out{.ok = true};
  if (r.return_count() > 0)
    out.value = script_from_lua(r.get<sol::object>(0));
  return out;
}

void script_register(context &ctx, const char *name, script_fn fn) {
  script_runtime &rt = script_rt(ctx);
  sol::table table;
  std::string key;
  if (!fn || !resolve(rt.lua, name, true, table, key))
    return;
  context *c = &ctx;
  table[key] = [c, fn = std::move(fn)](sol::variadic_args va, sol::this_state ts) -> sol::object {
    std::vector<script_value> args;
    args.reserve(va.size());
    for (const sol::stack_proxy &p : va)
      args.push_back(script_from_lua(p.get<sol::object>()));
    return script_to_lua(ts, fn(*c, args));
  };
}

void script_set_global(context &ctx, const char *name, const script_value &value) {
  script_runtime &rt = script_rt(ctx);
  sol::table table;
  std::string key;
  if (resolve(rt.lua, name, true, table, key))
    table[key] = script_to_lua(rt.lua, value);
}

script_value script_get_global(context &ctx, const char *name) {
  script_runtime &rt = script_rt(ctx);
  sol::table table;
  std::string key;
  if (!resolve(rt.lua, name, false, table, key))
    return {};
  return script_from_lua(table[key]);
}

bool script_attach(context &ctx, entt::entity entity, const char *path) {
  entt::registry &reg = world(ctx);
  if (path == nullptr || !reg.valid(entity))
    return false;
  script_runtime &rt = script_rt(ctx);
  script_detach(ctx, entity);
  script_class *c = class_of(rt, path);
  if (c == nullptr)
    return false;
  script_instance inst;
  inst.entity = entity;
  inst.path = path;
  inst.self = rt.lua.create_table();
  inst.self["entity"] = to_lua(entity);
  inst.self[sol::metatable_key] = c->meta;
  rt.instances[entt::to_integral(entity)] = std::move(inst);
  reg.emplace_or_replace<script_tag>(entity);
  return true;
}

void script_detach(context &ctx, entt::entity entity) {
  script_runtime *rt = ctx.script.rt.get();
  if (rt == nullptr)
    return;
  const auto it = rt->instances.find(entt::to_integral(entity));
  if (it == rt->instances.end())
    return;
  script_instance inst = std::move(it->second);
  rt->instances.erase(it);
  entt::registry &reg = world(ctx);
  if (reg.valid(entity))
    reg.remove<script_tag>(entity); // no instance left, so its on_destroy does nothing
  finish(*rt, inst);
}

bool script_attached(const context &ctx, entt::entity entity) {
  const script_runtime *rt = ctx.script.rt.get();
  return rt != nullptr && rt->instances.contains(entt::to_integral(entity));
}

script_value script_field(context &ctx, entt::entity entity, const char *key) {
  script_runtime *rt = ctx.script.rt.get();
  if (rt == nullptr || key == nullptr)
    return {};
  const auto it = rt->instances.find(entt::to_integral(entity));
  if (it == rt->instances.end())
    return {};
  return script_from_lua(it->second.self.raw_get<sol::object>(key));
}

void script_set_field(context &ctx, entt::entity entity, const char *key, const script_value &value) {
  script_runtime *rt = ctx.script.rt.get();
  if (rt == nullptr || key == nullptr)
    return;
  const auto it = rt->instances.find(entt::to_integral(entity));
  if (it != rt->instances.end())
    it->second.self.raw_set(key, script_to_lua(rt->lua, value));
}

i32 script_count(const context &ctx) {
  const script_runtime *rt = ctx.script.rt.get();
  return rt == nullptr ? 0 : (i32)rt->instances.size();
}

std::vector<std::string> script_watched_files(const context &ctx) {
  std::vector<std::string> out;
  const script_runtime *rt = ctx.script.rt.get();
  if (rt == nullptr)
    return out;
  out = rt->run_files;
  for (const auto &[path, c] : rt->classes)
    if (std::find(out.begin(), out.end(), path) == out.end())
      out.push_back(path);
  return out;
}

bool script_reload_file(context &ctx, const std::string &path) {
  script_runtime *rt = ctx.script.rt.get();
  if (rt == nullptr)
    return false;
  reported_errors().clear();
  bool ok = true;
  const auto cit = rt->classes.find(path);
  if (cit != rt->classes.end()) {
    sol::table module;
    if (!load_class(*rt, path, module))
      return false; // the old functions stay
    script_class &c = cit->second;
    c.module = module;
    c.meta["__index"] = module;
    set_handlers(*rt, c);
    for (const u32 id : instance_ids(*rt)) {
      const auto it = rt->instances.find(id);
      if (it != rt->instances.end() && it->second.path == path) {
        const sol::table self = it->second.self;
        ok = script_pcall(c.on_reload, "on_reload", self) && ok;
      }
    }
  }
  if (std::find(rt->run_files.begin(), rt->run_files.end(), path) != rt->run_files.end()) {
    std::string code;
    if (!read_script(path.c_str(), code))
      return false;
    bool run_ok = false;
    run_code(*rt, code, "@" + path, run_ok);
    ok = ok && run_ok;
  }
  return ok;
}

namespace {
// A script's `self` as JSON. Tables whose keys are exactly 1..n become arrays,
// other tables objects; an integer key is written "#n", and a string key that
// starts with '#' or '$' gets one more '$' in front, so `{"$vec3": [...]}` and
// `{"$vec2": [...]}` (the vectors) cannot be mistaken for a game's table.
constexpr i32 save_depth = 64;

struct save_writer {
  std::vector<const void *> open; // tables being written: one seen again is a cycle
  i32 skipped = 0;

  void skip(const std::string &where, const char *why) {
    skipped++;
    NJIN_WARN("script_save_state: %s %s: not saved", where.c_str(), why);
  }

  static std::string key_name(const sol::object &k, bool &ok) {
    ok = true;
    if (k.get_type() == sol::type::string) {
      std::string s = k.as<std::string>();
      return !s.empty() && (s[0] == '#' || s[0] == '$') ? "$" + s : s;
    }
    if (k.get_type() == sol::type::number && k.is<lua_Integer>())
      return "#" + std::to_string(k.as<lua_Integer>());
    ok = false;
    return {};
  }

  static std::string shown(const sol::object &k) {
    if (k.get_type() == sol::type::string)
      return k.as<std::string>();
    if (k.get_type() == sol::type::number)
      return std::to_string(k.as<f64>());
    return std::string("<") + sol::type_name(k.lua_state(), k.get_type()) + ">";
  }

  bool value(const sol::object &o, const std::string &where, i32 depth, json_value &out) {
    switch (o.get_type()) {
    case sol::type::boolean:
      out = json_value(o.as<bool>());
      return true;
    case sol::type::number: {
      const f64 v = o.as<f64>();
      if (!std::isfinite(v)) {
        skip(where, "is not a finite number");
        return false;
      }
      out = json_value(v);
      return true;
    }
    case sol::type::string:
      out = json_value(o.as<std::string>());
      return true;
    case sol::type::userdata:
      if (o.is<vec2>()) {
        const vec2 v = o.as<vec2>();
        json_value a = json_value::make_array();
        a.push(v.x).push(v.y);
        out = json_value::make_object();
        out.set("$vec2", std::move(a));
        return true;
      }
      if (o.is<vec3>()) {
        const vec3 v = o.as<vec3>();
        json_value a = json_value::make_array();
        a.push(v.x).push(v.y).push(v.z);
        out = json_value::make_object();
        out.set("$vec3", std::move(a));
        return true;
      }
      skip(where, "is a userdata other than vec2 or vec3");
      return false;
    case sol::type::table:
      return table(o.as<sol::table>(), where, depth, false, out);
    default:
      skip(where, (std::string("is a ") + sol::type_name(o.lua_state(), o.get_type())).c_str());
      return false;
    }
  }

  bool table(const sol::table &t, const std::string &where, i32 depth, bool top, json_value &out) {
    const void *p = t.pointer();
    if (std::find(open.begin(), open.end(), p) != open.end()) {
      skip(where, "refers back to a table that contains it (a cycle)");
      return false;
    }
    if (depth >= save_depth) {
      skip(where, "is nested too deep");
      return false;
    }
    open.push_back(p);
    // An array when the keys are exactly 1..n.
    usize n = 0;
    bool seq = true;
    for (const auto &kv : t) {
      n++;
      if (!(kv.first.get_type() == sol::type::number && kv.first.is<lua_Integer>()))
        seq = false;
    }
    if (seq && n > 0) {
      for (usize i = 1; i <= n && seq; i++)
        seq = t.raw_get<sol::object>((lua_Integer)i).get_type() != sol::type::lua_nil;
    }
    if (seq && n > 0 && !top) {
      out = json_value::make_array();
      for (usize i = 1; i <= n; i++) {
        json_value v;
        if (!value(t.raw_get<sol::object>((lua_Integer)i), where + "[" + std::to_string(i) + "]", depth + 1, v))
          v = json_value{}; // keeps the positions of the items after it
        out.push(std::move(v));
      }
    } else {
      out = json_value::make_object();
      for (const auto &kv : t) {
        bool ok = false;
        const std::string name = key_name(kv.first, ok);
        const std::string at = where + "." + shown(kv.first);
        if (!ok) {
          skip(at, "has a key that is not a string or an integer");
          continue;
        }
        if (top && name == "entity")
          continue; // set again on attach: entities get new numbers when a game loads
        json_value v;
        if (value(kv.second, at, depth + 1, v))
          out.set(name, std::move(v));
      }
    }
    open.pop_back();
    return true;
  }
};

sol::object load_value(sol::state_view lua, const json_value &v);

sol::table load_table(sol::state_view lua, const json_value &v) {
  sol::table t = lua.create_table();
  if (v.kind == json_value::array) {
    for (usize i = 0; i < v.items.size(); i++)
      t.raw_set((lua_Integer)(i + 1), load_value(lua, v.items[i]));
    return t;
  }
  for (const auto &[name, item] : v.members) {
    if (!name.empty() && name[0] == '#') {
      char *end = nullptr;
      const long long k = std::strtoll(name.c_str() + 1, &end, 10);
      if (end != nullptr && *end == '\0' && end != name.c_str() + 1) {
        t.raw_set((lua_Integer)k, load_value(lua, item));
        continue;
      }
    }
    const std::string key = !name.empty() && name[0] == '$' ? name.substr(1) : name;
    t.raw_set(key, load_value(lua, item));
  }
  return t;
}

sol::object load_value(sol::state_view lua, const json_value &v) {
  switch (v.kind) {
  case json_value::boolean:
    return sol::make_object(lua, v.b);
  case json_value::number:
    return script_to_lua(lua, script_value{v.num});
  case json_value::string:
    return sol::make_object(lua, v.str);
  case json_value::array:
    return load_table(lua, v);
  case json_value::object: {
    if (v.members.size() == 1 && v.members[0].first == "$vec2" && v.members[0].second.size() == 2) {
      const json_value &a = v.members[0].second;
      return sol::make_object(lua, vec2{a[(usize)0].f32_or(0.0f), a[(usize)1].f32_or(0.0f)});
    }
    if (v.members.size() == 1 && v.members[0].first == "$vec3" && v.members[0].second.size() == 3) {
      const json_value &a = v.members[0].second;
      return sol::make_object(lua, vec3{a[(usize)0].f32_or(0.0f), a[(usize)1].f32_or(0.0f), a[(usize)2].f32_or(0.0f)});
    }
    return load_table(lua, v);
  }
  default:
    return sol::make_object(lua, sol::lua_nil);
  }
}

std::string save_id_of(const script_instance &inst) {
  const sol::object id = inst.self.raw_get<sol::object>("save_id");
  if (id.get_type() == sol::type::string)
    return id.as<std::string>();
  if (id.get_type() == sol::type::number)
    return std::to_string(id.as<lua_Integer>());
  return {};
}
} // namespace

void script_set_save_id(context &ctx, entt::entity entity, const char *id) {
  script_runtime *rt = ctx.script.rt.get();
  if (rt == nullptr)
    return;
  const auto it = rt->instances.find(entt::to_integral(entity));
  if (it == rt->instances.end())
    return;
  if (id == nullptr || id[0] == '\0')
    it->second.self.raw_set("save_id", sol::lua_nil);
  else
    it->second.self.raw_set("save_id", std::string(id));
}

std::string script_save_id(context &ctx, entt::entity entity) {
  script_runtime *rt = ctx.script.rt.get();
  if (rt == nullptr)
    return {};
  const auto it = rt->instances.find(entt::to_integral(entity));
  return it == rt->instances.end() ? std::string{} : save_id_of(it->second);
}

json_value script_save_state(context &ctx) {
  json_value entities = json_value::make_object();
  script_runtime *rt = ctx.script.rt.get();
  if (rt != nullptr) {
    save_writer w;
    i32 unnamed = 0;
    for (const u32 id : instance_ids(*rt)) {
      const script_instance &inst = rt->instances.at(id);
      const std::string key = save_id_of(inst);
      if (key.empty()) {
        unnamed++;
        continue;
      }
      if (entities.has(key)) {
        NJIN_WARN("script_save_state: two entities have save_id '%s': only the first is saved", key.c_str());
        continue;
      }
      json_value self;
      w.table(inst.self, key, 0, true, self);
      json_value e = json_value::make_object();
      e.set("script", inst.path).set("self", std::move(self));
      entities.set(key, std::move(e));
    }
    if (unnamed > 0)
      log_write(log_debug, nullptr, 0, "script_save_state: %d scripted entities have no save_id and are not saved",
                unnamed);
  }
  json_value out = json_value::make_object();
  out.set("version", 1).set("entities", std::move(entities));
  return out;
}

i32 script_load_state(context &ctx, const json_value &state) {
  const json_value &entities = state["entities"];
  if (!entities.is(json_value::object))
    return 0;
  script_runtime &rt = script_rt(ctx);
  i32 restored = 0;
  for (const u32 id : instance_ids(rt)) {
    const auto it = rt.instances.find(id);
    if (it == rt.instances.end())
      continue; // an on_load detached it
    const std::string key = save_id_of(it->second);
    if (key.empty() || !entities.has(key))
      continue;
    const json_value &saved = entities[key];
    if (const char *path = saved["script"].string_or(nullptr); path != nullptr && it->second.path != path)
      NJIN_WARN("script_load_state: '%s' was saved from %s and is restored into %s", key.c_str(), path,
                it->second.path.c_str());
    sol::table self = it->second.self;
    for (const auto &[name, item] : saved["self"].members) {
      if (!name.empty() && name[0] == '#') {
        char *end = nullptr;
        const long long k = std::strtoll(name.c_str() + 1, &end, 10);
        if (end != nullptr && *end == '\0' && end != name.c_str() + 1) {
          self.raw_set((lua_Integer)k, load_value(rt.lua, item));
          continue;
        }
      }
      self.raw_set(!name.empty() && name[0] == '$' ? name.substr(1) : name, load_value(rt.lua, item));
    }
    restored++;
    const auto cit = rt.classes.find(it->second.path);
    if (cit != rt.classes.end())
      script_pcall(cit->second.on_load, "on_load", self);
  }
  return restored;
}
} // namespace njin
