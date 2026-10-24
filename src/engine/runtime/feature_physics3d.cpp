// The physics3d feature (njin_features.h): bodies, characters, ragdolls, soft
// bodies and vehicles on Jolt Physics, stepped after the game's fixed systems.
#include "modules/debug.h"
#include "njin_ctx_impl.h"
#include "njin_json.h"
#include "njin_physics3d_impl.h"

namespace njin {
namespace {
json_value vec3_json(vec3 v) { return json_value::make_array().push(v.x).push(v.y).push(v.z); }

void debug_views(context &ctx) {
  debug_state &d = ctx.debug;
  const context *pctx = &ctx;
  debug_builtin<body3d>(d, "body3d", [pctx](const body3d &b) {
    return json_value::make_object()
        .set("body", b.handle.id)
        .set("velocity", vec3_json(body3d_velocity(*pctx, b.handle)))
        .set("user", (i64)body3d_user(*pctx, b.handle));
  });
  debug_builtin<character3d>(d, "character3d", [pctx](const character3d &c) {
    return json_value::make_object()
        .set("character", c.handle.id)
        .set("velocity", vec3_json(character3d_velocity(*pctx, c.handle)))
        .set("grounded", character3d_grounded(*pctx, c.handle));
  });
}
} // namespace

void feature_install_physics3d(context &ctx) {
  ctx.physics3d.emplace();
  feature_hooks &h = ctx.hooks;
  h.fixed_step = physics3d_step;
  h.raycast = static_cast<ray3d_hit (*)(const context &, const ray3d &, f32, body3d_handle *)>(physics3d_raycast);
  h.heightfield_create = physics3d_heightfield_create;
  h.heightfield_set = physics3d_heightfield_set;
  h.body_destroy = body3d_destroy;
  h.body_float = physics3d_float;
  h.body_unfloat = physics3d_unfloat;
  h.debug_views.push_back(debug_views);
}
} // namespace njin
