#pragma once
#include "njin_internal_only.h"

#include "njin_3d.h"
#include "njin_ctx_get.h"
#include "njin_log.h"

// What the 3d feature asks of physics3d: ground and sound occlusion rays,
// terrain collision, floating bodies. physics3d sets these hooks
// (feature_physics3d.cpp); a game that leaves it out gets no hit, and what
// cannot work without it says so once.
namespace njin {
inline bool physics3d_present(const context &ctx) { return ctx_ref<feature_hooks>(ctx).raycast != nullptr; }

// Says once per `what` that it needs physics3d, which this game does not use;
// true when it is missing. `also` is appended to the message.
inline bool physics3d_missing(const context &ctx, bool &said, const char *what, log_level level = log_error,
                              const char *also = "") {
  if (physics3d_present(ctx))
    return false;
  if (!said)
    NJIN_LOG(level,
             "%s needs the physics3d feature, which this game does not use: add it with "
             "njin_use(<game> FEATURES physics3d)%s",
             what, also);
  said = true;
  return true;
}

inline ray3d_hit physics3d_ray(const context &ctx, const ray3d &ray, f32 max_distance) {
  return ctx_ref<feature_hooks>(ctx).raycast != nullptr ? ctx_ref<feature_hooks>(ctx).raycast(ctx, ray, max_distance, nullptr) : ray3d_hit{};
}
} // namespace njin
