// The 3d feature (njin_features.h): models and materials, the 3D pass, 3D
// particles, post3d, world3d (sky, terrain, water, grass) and 3D animation.
#include "_comps.h"
#include "modules/debug.h"
#include "modules/debug3d.h"
#include "modules/particles3d.h"
#include "modules/post3d.h"
#include "modules/post_fx.h"
#include "modules/render3d.h"
#include "njin_3d.h"
#include "njin_anim3d_impl.h"
#include "njin_audio_impl.h"
#include "njin_ctx_impl.h"
#include "njin_instance.h"
#include "njin_json.h"
#include "njin_log.h"
#include "njin_model.h"
#include "njin_world3d_impl.h"
#include <algorithm>
#include <cstring>

namespace njin {
namespace {
// The depth the last pass left in the world target, for the depth of field.
void world_depth(context &ctx, u32 depth_texture, post_depth &out) {
  render3d_state &r = ctx.render3d;
  if (r.depth_drawn && depth_texture != 0)
    out = {depth_texture, r.depth_near, r.depth_far, r.depth_inv_view_proj};
  r.depth_drawn = false;
}

const debug3d_frame *debug_frame3d(const context &ctx) { return &ctx.render3d->debug; }

json_value vec3_json(vec3 v) { return json_value::make_array().push(v.x).push(v.y).push(v.z); }
json_value color_json(rgba c) { return json_value::make_array().push(c.r).push(c.g).push(c.b).push(c.a); }

void debug_views(context &ctx) {
  debug_state &d = ctx.debug;
  const context *pctx = &ctx;
  debug_builtin<transform3d>(d, "transform3d", [](const transform3d &t) {
    return json_value::make_object()
        .set("position", vec3_json(t.position))
        .set("rotation", vec3_json(t.rotation))
        .set("scale", vec3_json(t.scale));
  });
  debug_builtin<model3d>(d, "model3d", [pctx](const model3d &m) {
    return json_value::make_object()
        .set("model", m.model.id)
        .set("anim", model_anim_name(*pctx, m.model, m.pose.anim))
        .set("time", m.pose.time)
        .set("blend_anim", model_anim_name(*pctx, m.model, m.pose.blend_anim))
        .set("blend", m.pose.blend)
        .set("speed", m.speed)
        .set("tint", color_json(m.tint))
        .set("visible", m.visible);
  });
  debug_builtin<shape3d_render>(d, "shape3d_render", [](const shape3d_render &r) {
    const char *kinds[] = {"sphere", "box", "capsule", "cylinder", "torus"};
    return json_value::make_object()
        .set("kind", kinds[std::clamp((i32)r.shape.kind, 0, 4)])
        .set("size", vec3_json(r.shape.size))
        .set("radius", r.shape.radius)
        .set("height", r.shape.height)
        .set("color", color_json(r.color))
        .set("visible", r.visible);
  });
  debug_builtin<light3d_source>(d, "light3d_source", [](const light3d_source &l) {
    return json_value::make_object()
        .set("kind", l.kind == light3d_spot ? "spot" : "point")
        .set("color", color_json(l.color))
        .set("intensity", l.intensity)
        .set("radius", l.radius)
        .set("shadows", l.shadows);
  });
}
} // namespace

void feature_install_3d(context &ctx) {
  ctx.instances.emplace();
  ctx.model.emplace();
  ctx.render3d.emplace();
  ctx.post3d.emplace();
  ctx.particles3d.emplace();
  ctx.world3d.emplace();
  ctx.anim3d.emplace();
  feature_hooks &h = ctx.hooks;
  h.world_begin = post3d_frame_begin;
  h.world_post_wanted = post3d_wanted;
  h.world_end = render3d_close;
  h.world_depth = world_depth;
  h.audio_update = audio3d_update;
  h.quality_changed = post3d_apply_quality;
  h.debug_frame3d = debug_frame3d;
  h.debug_views.push_back(debug_views);
  for (const mod_desc &m : {particles3d_module(), render3d_module(), world3d_module()})
    h.modules_mid.push_back(m);
}

model_handle model_load(context &ctx, const char *path) {
  return model_store_load(ctx.model, model_load_desc{.path = path});
}

model_handle model_load(context &ctx, const model_load_desc &desc) { return model_store_load(ctx.model, desc); }

void model_unload(context &ctx, model_handle handle) {
  model_store_unload(ctx.model, handle);
}

model_handle model_create(context &ctx, const mesh3d_data &mesh) { return model_store_create(ctx.model, mesh); }

model_handle model_create_skinned(context &ctx, const skinned_mesh3d_data &mesh) {
  return model_store_create_skinned(ctx.model, mesh);
}

i32 model_material_count(const context &ctx, model_handle handle) {
  const model_slot *slot = model_slot_of(ctx.model, handle);
  return slot != nullptr ? (i32)slot->materials.size() : 0;
}

i32 model_material_find(const context &ctx, model_handle handle, const char *name) {
  const model_slot *slot = model_slot_of(ctx.model, handle);
  if (slot == nullptr || name == nullptr || name[0] == '\0')
    return -1;
  const usize n = std::strlen(name);
  for (usize i = 0; i < slot->material_names.size(); i++)
    if (slot->material_names[i].compare(0, n, name) == 0)
      return (i32)i;
  return -1;
}

model_material model_material_get(const context &ctx, model_handle handle, i32 index) {
  const model_slot *slot = model_slot_of(ctx.model, handle);
  if (slot == nullptr || index < 0 || index >= (i32)slot->materials.size())
    return model_material{};
  return slot->materials[(usize)index];
}

void model_material_set(context &ctx, model_handle handle, i32 index, const model_material &material) {
  model_slot *slot = model_slot_of(ctx.model, handle);
  if (slot == nullptr)
    return;
  if (index < 0) {
    for (model_material &m : slot->materials)
      m = material;
  } else if (index < (i32)slot->materials.size()) {
    slot->materials[(usize)index] = material;
  } else {
    NJIN_WARN("model: material %d out of range (model has %zu)", index, slot->materials.size());
  }
}
} // namespace njin
