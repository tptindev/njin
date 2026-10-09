#pragma once
#include "_math.h"
#include "_types.h"
#include "njin_json.h"
#include <entt/entity/fwd.hpp>
#include <functional>
#include <initializer_list>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace njin {
struct context;

/// @addtogroup grp_script
/// @{

/// A value passed back and forth between C++ and Lua: nil, bool, number, string,
/// vec2, vec3 or entity. Every Lua number (integer or float) becomes an `f64`; an
/// entity is an integer on the Lua side.
using script_value = std::variant<std::monostate, bool, f64, std::string, vec2, vec3, entt::entity>;

/// Options of the Lua machine, for script_init().
struct script_desc {
  /// Lets scripts read and write files and run other files (`io`, `dofile`,
  /// `loadfile`, `os.remove`...). Off by default: scripts only act inside the engine
  /// and never touch the player's machine. Only turn it on for trusted tools (builds,
  /// level editors). `os.execute`, `io.popen` and loading C libraries
  /// (`package.loadlib`) are never available.
  bool allow_io = false;
};

/// Creates the Lua machine with options `desc`. Optional: the first use of any
/// script_* function creates the machine with the default options. A second call
/// is ignored (the machine already exists).
/// @param ctx The engine context.
/// @param desc Options.
void script_init(context &ctx, const script_desc &desc = {});

/// Runs a Lua file (path resolved like every asset: next to the executable or in
/// the working folder). The functions and globals it creates can be used with
/// script_call() and script_get_global(). Errors (syntax or at run time) are logged
/// with the file and line and never stop the game.
/// @param ctx The engine context.
/// @param path A `.lua` file.
/// @return `true` if it ran to the end without an error.
bool script_run_file(context &ctx, const char *path);

/// Runs a piece of Lua code. Like script_run_file().
/// @param ctx The engine context.
/// @param code Lua code.
/// @param name Name shown in error messages.
/// @return `true` if it ran to the end without an error.
bool script_run_string(context &ctx, const char *code, const char *name = "string");

/// Result of script_call().
struct script_result {
  bool ok = false;      ///< The function exists and ran without an error.
  script_value value{}; ///< The first value returned (nil if nothing).
};

/// Calls a global Lua function by name. A dotted name (`"enemy.spawn"`) is looked
/// up in tables. A missing function or an error is logged and `ok` is `false`.
/// @param ctx The engine context.
/// @param function Function name.
/// @param args Arguments.
/// @return The result.
script_result script_call(context &ctx, const char *function, std::initializer_list<script_value> args = {});

/// Like the one above, with the arguments in an array.
/// @param ctx The engine context.
/// @param function Function name.
/// @param args Arguments.
/// @return The result.
script_result script_call(context &ctx, const char *function, std::span<const script_value> args);

/// A game function callable from Lua, general form: takes every argument, returns one value.
using script_fn = std::function<script_value(context &, std::span<const script_value>)>;

/// Registers a C++ function under the name `name` (global on the Lua side; a dotted
/// name puts it in a table, creating tables as needed). A C++ exception thrown in
/// the function becomes a Lua error with the file and line of the call.
/// @param ctx The engine context.
/// @param name Name on the Lua side.
/// @param fn The function.
void script_register(context &ctx, const char *name, script_fn fn);

namespace script_detail {
template <class> inline constexpr bool always_false = false;

/// Converts a script_value to the type of a game function's parameter. A wrong type gives the default value.
template <class T> std::remove_cvref_t<T> to(const script_value &v) {
  using U = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<U, bool>) {
    if (const bool *b = std::get_if<bool>(&v))
      return *b;
    if (const f64 *d = std::get_if<f64>(&v))
      return *d != 0.0;
    return false;
  } else if constexpr (std::is_same_v<U, entt::entity>) {
    if (const entt::entity *e = std::get_if<entt::entity>(&v))
      return *e;
    if (const f64 *d = std::get_if<f64>(&v))
      return entt::entity((u32)*d);
    return entt::entity(~u32{0});
  } else if constexpr (std::is_arithmetic_v<U>) {
    if (const f64 *d = std::get_if<f64>(&v))
      return (U)*d;
    if (const bool *b = std::get_if<bool>(&v))
      return (U)(*b ? 1 : 0);
    return U{};
  } else if constexpr (std::is_same_v<U, std::string> || std::is_same_v<U, vec2> || std::is_same_v<U, vec3>) {
    if (const U *p = std::get_if<U>(&v))
      return *p;
    return U{};
  } else if constexpr (std::is_same_v<U, script_value>) {
    return v;
  } else {
    static_assert(always_false<U>, "script_register: unsupported parameter type");
  }
}

/// Converts a game function's return value to a script_value.
template <class T> script_value from(T &&value) {
  using U = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<U, script_value>)
    return std::forward<T>(value);
  else if constexpr (std::is_same_v<U, bool> || std::is_same_v<U, vec2> || std::is_same_v<U, vec3> ||
                     std::is_same_v<U, entt::entity> || std::is_same_v<U, std::string>)
    return script_value{std::forward<T>(value)};
  else if constexpr (std::is_arithmetic_v<U>)
    return script_value{(f64)value};
  else if constexpr (std::is_convertible_v<U, const char *>)
    return script_value{std::string(value)};
  else
    static_assert(always_false<U>, "script_register: unsupported return type");
}

inline const script_value &arg(std::span<const script_value> args, usize i) {
  static const script_value none{};
  return i < args.size() ? args[i] : none;
}

template <class R, class... A, usize... I>
script_value call(const std::function<R(context &, A...)> &f, context &ctx, std::span<const script_value> args,
                  std::index_sequence<I...>) {
  if constexpr (std::is_void_v<R>) {
    f(ctx, to<A>(arg(args, I))...);
    return {};
  } else {
    return from(f(ctx, to<A>(arg(args, I))...));
  }
}

template <class R, class... A, usize... I>
script_value call(const std::function<R(A...)> &f, std::span<const script_value> args, std::index_sequence<I...>) {
  if constexpr (std::is_void_v<R>) {
    f(to<A>(arg(args, I))...);
    return {};
  } else {
    return from(f(to<A>(arg(args, I))...));
  }
}

template <class R, class... A> script_fn wrap(std::function<R(context &, A...)> f) {
  return [f = std::move(f)](context &ctx, std::span<const script_value> args) {
    return call(f, ctx, args, std::index_sequence_for<A...>{});
  };
}

template <class R, class... A> script_fn wrap(std::function<R(A...)> f) {
  return [f = std::move(f)](context &, std::span<const script_value> args) {
    return call(f, args, std::index_sequence_for<A...>{});
  };
}
} // namespace script_detail

/// Registers a C++ function with ordinary parameter and return types: `bool`,
/// numbers, `std::string`, vec2, vec3, `entt::entity` or script_value, optionally
/// taking `context &` first. A missing or mistyped Lua argument becomes the default
/// value of that type.
/// @code
/// njin::script_register(ctx, "add_score", [](njin::context &c, int points) { score += points; });
/// njin::script_register(ctx, "distance", [](njin::vec2 a, njin::vec2 b) { return njin::length(b - a); });
/// @endcode
/// @param ctx The engine context.
/// @param name Name on the Lua side.
/// @param fn A function, lambda or function pointer (not a generic lambda).
template <class F>
  requires(!std::is_convertible_v<F, script_fn>)
void script_register(context &ctx, const char *name, F &&fn) {
  script_register(ctx, name, script_detail::wrap(std::function{std::forward<F>(fn)}));
}

/// Sets a global variable on the Lua side (a dotted name sets it in a table).
/// @param ctx The engine context.
/// @param name Name.
/// @param value Value.
void script_set_global(context &ctx, const char *name, const script_value &value);

/// Reads a global variable on the Lua side. Tables, functions and values that
/// cannot be converted read as nil.
/// @param ctx The engine context.
/// @param name Name (a dotted name is looked up in tables).
/// @return The value.
script_value script_get_global(context &ctx, const char *name);

/// Attaches a script to an entity. The file must return a table (like a class);
/// each entity gets its own `self` table, which the table's functions take as
/// their first parameter:
///
/// - `on_start(self)`: once, at the first update after attaching (before
///   `on_fixed_update` or `on_update`, whichever runs first).
/// - `on_fixed_update(self, dt)`: at the fixed rate, in `phase_fixed_update`,
///   0 or more times a frame (dt is the fixed step, as config::fixed_hz sets).
///   It runs right before the 3D physics step: a velocity set here takes effect
///   in the same step. For movement and physics that must run the same at any
///   frame rate. Costs nothing while no script has this function.
/// - `on_update(self, dt)`: every frame, in `phase_update` (dt is delta()).
/// - `on_render(self)`: every frame, in `phase_render`, to draw (njin.draw_*).
/// - `on_destroy(self)`: when the entity is destroyed or the script detached.
///   Do not destroy other entities here.
/// - `on_reload(self)`: after the file was loaded again (hot reload).
/// - `on_load(self)`: after script_load_state() put saved data into `self`.
///
/// `self.entity` is the entity. Every field the game writes into `self` is kept
/// across hot reloads: a reloaded file only replaces the functions. When several
/// entities use one file, the file is loaded once. Attaching a second time detaches
/// the old script first (with `on_destroy`).
/// @param ctx The engine context.
/// @param entity The entity.
/// @param path A `.lua` file.
/// @return `true` if the file loads and returns a table.
bool script_attach(context &ctx, entt::entity entity, const char *path);

/// Detaches the script from an entity (calls `on_destroy`). Ignored without a script.
/// @param ctx The engine context.
/// @param entity The entity.
void script_detach(context &ctx, entt::entity entity);

/// Whether the entity has a script. @param ctx The engine context. @param entity The entity.
/// @return `true` if it has one.
bool script_attached(const context &ctx, entt::entity entity);

/// Reads field `key` of the `self` table of the script attached to the entity.
/// @param ctx The engine context.
/// @param entity The entity.
/// @param key Field name.
/// @return The value, nil without a script or field.
script_value script_field(context &ctx, entt::entity entity, const char *key);

/// Writes field `key` of the `self` table. Ignored without a script.
/// @param ctx The engine context.
/// @param entity The entity.
/// @param key Field name.
/// @param value Value.
void script_set_field(context &ctx, entt::entity entity, const char *key, const script_value &value);

/// Number of entities that have a script. @param ctx The engine context. @return The number of entities.
i32 script_count(const context &ctx);

/// Sets the save name (`self.save_id`) of the script on an entity: the key
/// script_load_state() finds the right entity by when a game is loaded, since a
/// recreated entity has another number. A script can set it itself with
/// `self.save_id = "store_door"`. Names must differ between saved entities;
/// `nullptr` or an empty string removes the name (not saved).
/// @param ctx The engine context.
/// @param entity An entity with a script.
/// @param id The save name.
void script_set_save_id(context &ctx, entt::entity entity, const char *id);

/// The save name of the script on an entity. @param ctx The engine context.
/// @param entity The entity. @return The name, or an empty string if there is none.
std::string script_save_id(context &ctx, entt::entity entity);

/// The state of every script with a save name, to write into a save game: the
/// fields of `self` (numbers, booleans, strings, vec2, vec3 and nested tables of
/// them), by save name, with the script file. `self.entity` is not saved
/// (attaching sets it again). Functions, other userdata, reference cycles,
/// non-finite numbers and keys that are not strings or integers are left out, each
/// with a warning that gives its path (`store_door.self.inventory[3]`). Two fields
/// pointing at one table become two copies. Entity numbers in `self` are saved as
/// plain numbers and do not follow the new entities.
///
/// @code
/// njin::json_value save = njin::json_value::make_object();
/// save.set("level", level).set("scripts", njin::script_save_state(ctx));
/// njin::json_save(njin::save_path(ctx, "save.json").c_str(), save);
/// @endcode
/// @param ctx The engine context.
/// @return The object `{"version": 1, "entities": {name: {"script": file, "self": {...}}}}`.
json_value script_save_state(context &ctx);

/// Puts a state saved by script_save_state() into the attached scripts with the
/// same save names, then calls their `on_load(self)`. Call it after recreating the
/// entities, attaching the scripts and setting the save names. Saved fields
/// overwrite fields of the same name; fields not in the save stay as they are. The
/// `on_start` of a freshly attached script still runs on the first update (after
/// `on_load`), so set defaults in the file's table (`M.hp = 10`) or with
/// `self.hp = self.hp or 10`, so they do not overwrite the loaded data.
///
/// @code
/// njin::json_value save;
/// if (njin::json_load(njin::save_path(ctx, "save.json").c_str(), save))
///   njin::script_load_state(ctx, save["scripts"]);
/// @endcode
/// @param ctx The engine context.
/// @param state The value script_save_state() returned (after json_save()/json_load()).
/// @return The number of entities that got their data back.
i32 script_load_state(context &ctx, const json_value &state);
/// @}
} // namespace njin
