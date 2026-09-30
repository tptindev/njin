#pragma once
#include "_math.h"
#include "_types.h"
#include "njin_json.h"
#include <entt/core/type_info.hpp>
#include <entt/entity/registry.hpp>
#include <functional>
#include <type_traits>

namespace njin {
struct context;

/// @addtogroup grp_debug
/// @{

/// How to open the debug port, used with debug_server_start().
struct debug_server_desc {
  u16 port = 7779;           ///< TCP port, listens only on 127.0.0.1.
  f32 snapshot_hz = 10.0f;   ///< Times per second the entity list and watched values are sent.
  i32 max_entities = 4000;   ///< Maximum entities per send. Any excess is cut, the inspector reports it.
};

/// Opens the debug port for the **njin_inspector** program (a separate process) to
/// connect to: FPS, a frame-time graph, the list of entities and components, collider
/// outlines drawn on the map, logs, watched values, and time controls
/// (pause, step frame by frame, slow motion) and recording the game window to a GIF file (see @ref debug_recording).
///
/// The game **draws nothing extra**: all debug UI lives in the inspector window. It only
/// listens on 127.0.0.1, so other machines cannot connect; with no inspector
/// connected the cost is almost zero. It never blocks the game: if the inspector is
/// slow, data is dropped rather than the game stalling.
///
/// Usually enabled only in debug builds:
/// @code
/// #ifndef NDEBUG
///   njin::debug_server_start(*ctx);
/// #endif
/// @endcode
///
/// **Log:** while an inspector is connected, logs go to the inspector and are **no longer printed
/// to the game's console**; with no inspector (or after it disconnects) logs still go to the
/// console as usual. Lines already emitted before, including while the window was opening (at most 2000
/// lines, counted only from create()), are sent to the inspector as soon as it connects.
/// @param ctx Engine context.
/// @param desc Port and send rate.
/// @return `false` if the port is taken (for example another copy of the game is running).
bool debug_server_start(context &ctx, const debug_server_desc &desc = {});

/// Closes the debug port and disconnects the connected inspector. @param ctx Engine context.
void debug_server_stop(context &ctx);

/// Whether any inspector is connected. @param ctx Engine context.
/// @return `true` if there is one.
bool debug_server_connected(const context &ctx);

/// Sets a value to watch live in the inspector's "Watches" panel: character
/// velocity, AI state, number of monsters alive. Call it every frame or whenever it
/// changes; the last value is sent at the `snapshot_hz` rate. With no inspector
/// it does nothing.
/// @code
/// njin::debug_watch(ctx, "player.velocity", vel);
/// njin::debug_watch(ctx, "enemies alive", (njin::i32)enemies.size());
/// @endcode
/// @param ctx Engine context.
/// @param name Display name.
/// @param value Value: a number, bool, string, or any njin::json_value.
void debug_watch(context &ctx, const char *name, json_value value);

/// Like debug_watch() for a vec2. @param ctx Engine context.
/// @param name Display name. @param value Value.
void debug_watch(context &ctx, const char *name, vec2 value);

/// Function that turns a component into JSON so the inspector can show its value.
using debug_component_fn = std::function<json_value(const entt::registry &, entt::entity)>;

/// Registers how to show a kind of component. The engine already provides this for every
/// njin component. A game component that is not registered still shows its name in the inspector,
/// just without values.
/// @param ctx Engine context.
/// @param type EnTT type identifier, `entt::type_hash<T>::value()`.
/// @param name Display name.
/// @param fn Function that converts to JSON.
/// @param bytes Size of one component (`sizeof`), so the inspector's Memory panel
/// can compute memory. 0 means unknown.
void debug_component(context &ctx, entt::id_type type, const char *name, debug_component_fn fn,
                     std::size_t bytes = 0);

/// Registers how to show a game component `T` with a function taking `const T &`.
/// @code
/// struct health { njin::i32 hp = 3, max = 3; };
/// njin::debug_component<health>(ctx, "health", [](const health &h) {
///   return njin::json_value::make_object().set("hp", h.hp).set("max", h.max);
/// });
/// @endcode
/// @tparam T Component type.
/// @tparam Fn Function or lambda `json_value(const T &)`.
/// @param ctx Engine context.
/// @param name Display name.
/// @param fn Function that converts to JSON.
template <class T, class Fn>
void debug_component(context &ctx, const char *name, Fn fn) {
  debug_component(ctx, entt::type_hash<T>::value(), name,
                  [fn](const entt::registry &reg, entt::entity e) -> json_value {
                    if constexpr (std::is_empty_v<T>) {
                      return json_value::make_object(); // a tag has no fields
                    } else {
                      return fn(reg.get<T>(e));
                    }
                  },
                  std::is_empty_v<T> ? 0 : sizeof(T));
}

/// What the last frame drew, for the game's own debug screen. njin_inspector
/// shows exactly these numbers in the Performance window.
struct render_info {
  u32 sprites = 0;         ///< Sprites drawn.
  u32 sprites_culled = 0;  ///< Sprites skipped because they were outside the camera.
  u32 tile_chunks = 0;     ///< Tilemap chunks drawn.
  u32 emitters = 0;        ///< Particle emitters drawn.
  u32 emitters_culled = 0; ///< Emitters skipped because they were outside the camera.
  u32 particles = 0;       ///< Particles drawn, both CPU and GPU.
  u32 particles_gpu = 0;   ///< Of which drawn on the GPU.
  u32 instanced_calls = 0; ///< Instanced draw commands (one per GPU emitter).
  /// **Estimated** number of draw calls. Raylib does not report the real count, so this counts
  /// texture or blend mode changes, plus each instanced call. UI and text are not counted.
  u32 draw_calls = 0;
  u32 post_passes = 0;     ///< Number of full-screen passes from the built-in post-processing and from lighting.
  u32 lights = 0;          ///< Lights drawn into the light map (njin_light.h).
  u32 models3d = 0;        ///< 3D models drawn (draw_model(), njin::model3d).
  u32 models3d_culled = 0; ///< 3D models left out as outside the camera's view (they still cast shadows).
};

/// Draw statistics of the last frame. Read in `phase_post_render` (or the next frame)
/// to get the numbers for the whole frame; read earlier, the parts not yet drawn are missing.
/// @param ctx Engine context.
/// @return The statistics.
render_info render_info_get(const context &ctx);
/// @}
} // namespace njin
