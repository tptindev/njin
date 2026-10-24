#pragma once
#include "njin_internal_only.h"

#include "_math.h"
#include "_mod.h"
#include "_types.h"
#include <string>
#include <utility>
#include <vector>

// The engine is split into a core and optional features, each its own library
// (src/engine/runtime/CMakeLists.txt): 3d, physics3d, nav3d, video, net,
// script, and the glue between two of them (script with 3d, physics3d,
// nav3d). A game names the features it uses (njin_use() in cmake/njin.cmake)
// and links only those; njin::rt is every feature.
//
// The core never calls a feature directly, or a game without it would still
// link it. A feature reaches the core three ways, all set by its
// feature_install_<name>() (feature_<name>.cpp):
// - its state, held by the context in a feature_ref (empty without the
//   feature), at the place the member always had, so the order the context
//   destroys things in is the same with or without it;
// - the hooks below, which the core calls when they are set;
// - its modules, appended to `modules_mid` or `modules_last`.
// features_install() calls them, in the order of features_order in
// cmake/njin.cmake; it is generated for each game (njin_use) and for njin::rt.

namespace njin {
struct context;
struct post_depth;
struct ray3d;
struct ray3d_hit;
struct debug3d_frame;

// A feature's state in the context: empty, or one T made by the feature
// (emplace). Only the feature sees T complete; the core keeps a pointer and
// how to free it, so the core links without the feature's code.
template <class T> struct feature_ref {
  feature_ref() = default;
  feature_ref(const feature_ref &) = delete;
  feature_ref &operator=(const feature_ref &) = delete;
  ~feature_ref() { reset(); }

  template <class... A> T &emplace(A &&...args) {
    reset();
    ptr = new T(std::forward<A>(args)...);
    free = [](void *p) { delete static_cast<T *>(p); };
    return *ptr;
  }
  void reset() {
    if (ptr != nullptr)
      free(ptr);
    ptr = nullptr;
    free = nullptr;
  }
  explicit operator bool() const { return ptr != nullptr; }
  T *operator->() { return ptr; }
  const T *operator->() const { return ptr; }
  T &operator*() { return *ptr; }
  const T &operator*() const { return *ptr; }
  operator T &() { return *ptr; }
  operator const T &() const { return *ptr; }

private:
  T *ptr = nullptr;
  void (*free)(void *) = nullptr;
};

// What the core calls of a feature. Unset (nullptr) without it.
struct feature_hooks {
  // 3d: the world pass of the camera module opens (post3d's frame), may need
  // the post target (post3d), and closes (a 3D pass the game left open).
  void (*world_begin)(context &ctx) = nullptr;
  bool (*world_post_wanted)(const context &ctx) = nullptr;
  void (*world_end)(context &ctx) = nullptr;
  // 3d: the depth the last 3D pass left in the world target, for the depth of
  // field; fills `out` when a pass drew since the last call.
  void (*world_depth)(context &ctx, u32 depth_texture, post_depth &out) = nullptr;
  // 3d: positional voices, before the audio store updates.
  void (*audio_update)(context &ctx) = nullptr;
  // 3d: quality_set() changed the settings.
  void (*quality_changed)(context &ctx) = nullptr;
  // 3d: the last 3D pass for the inspector's world view, or nullptr.
  const debug3d_frame *(*debug_frame3d)(const context &ctx) = nullptr;

  // physics3d: one fixed step, right after the game's fixed systems.
  void (*fixed_step)(context &ctx, f32 dt) = nullptr;
  // physics3d, for the 3d feature (ground and sound occlusion rays, terrain
  // collision, floating bodies): see physics3d_bridge.h.
  ray3d_hit (*raycast)(const context &ctx, const ray3d &ray, f32 max_distance, body3d_handle *body) = nullptr;
  body3d_handle (*heightfield_create)(context &ctx, const f32 *heights, i32 count, vec3 origin, f32 spacing, f32 lo,
                                      f32 hi, f32 friction, u64 user) = nullptr;
  void (*heightfield_set)(context &ctx, body3d_handle body, const f32 *heights, i32 count, i32 x0, i32 z0, i32 x1,
                          i32 z1) = nullptr;
  void (*body_destroy)(context &ctx, body3d_handle body) = nullptr;
  void (*body_float)(context &ctx, body3d_handle body, u32 water, f32 buoyancy, f32 linear_drag, f32 angular_drag,
                     vec3 flow, bool (*surface)(const context &, u32, vec3, vec3 &, vec3 &)) = nullptr;
  void (*body_unfloat)(context &ctx, body3d_handle body, u32 water) = nullptr;

  // script: the files hot reload watches, and reloading one of them.
  std::vector<std::string> (*watched_files)(const context &ctx) = nullptr;
  bool (*reload_file)(context &ctx, const std::string &path) = nullptr;

  // Any feature: views of its components for the inspector (debug.h,
  // debug_register_builtin), registered with the core's own.
  std::vector<void (*)(context &ctx)> debug_views;

  // Modules: `modules_mid` run after the core's particles and before its
  // sprites, `modules_last` after every core module (core_modules.cpp).
  std::vector<mod_desc> modules_mid;
  std::vector<mod_desc> modules_last;
};

// Installs the game's features. Defined in the source njin_use() generates for
// each game (and in features_all.cpp for njin::rt); called by create() before
// the core modules are registered.
void features_install(context &ctx);
} // namespace njin
