#include "game.h"

#include <cmath>
#include <cstdio>

namespace moteswarm {
namespace {
constexpr rgba body_color{0.918f, 0.463f, 0.0f, 1.0f};
constexpr rgba dot_color{0.24f, 0.25f, 0.30f, 1.0f};
constexpr rgba landmark_color{0.36f, 0.37f, 0.44f, 1.0f};
constexpr f32 grid = 64.0f;

// Uniform arrays are set one element at a time, by name.
void set_element(njin_ctx &ctx, shader_handle sh, const char *name, i32 i, vec4 value) {
  char key[32];
  std::snprintf(key, sizeof key, "%s[%d]", name, i);
  shader_set_vec4(ctx, sh, key, value);
}

void set_element(njin_ctx &ctx, shader_handle sh, const char *name, i32 i, vec2 value) {
  char key[32];
  std::snprintf(key, sizeof key, "%s[%d]", name, i);
  shader_set_vec2(ctx, sh, key, value);
}

void set_uniforms(njin_ctx &ctx, shader_handle sh, const creature_draw &d) {
  for (i32 i = 0; i < blob_count; i++)
    set_element(ctx, sh, "lump", i, d.lump[i]);
  shader_set_f32(ctx, sh, "blend", d.blend);

  for (i32 i = 0; i < d.tail_count; i++) {
    set_element(ctx, sh, "tailPts", i, d.tail_pts[i]);
    set_element(ctx, sh, "tailR", i, d.tail_r[i]);
  }
  shader_set_i32(ctx, sh, "tailCount", d.tail_count);
  shader_set_f32(ctx, sh, "tailBlend", d.tail_blend);
  shader_set_f32(ctx, sh, "tailPairBlend", d.tail_pair_blend);

  shader_set_vec2(ctx, sh, "shadowOffset", d.shadow_offset);
  shader_set_f32(ctx, sh, "shadowBlur", d.shadow_blur);
  shader_set_f32(ctx, sh, "shadowAlpha", d.shadow_alpha);

  shader_set_vec2(ctx, sh, "eyePos", d.eye_pos);
  shader_set_vec2(ctx, sh, "pupilPos", d.pupil_pos);
  shader_set_f32(ctx, sh, "eyeRadius", d.eye_radius);
  shader_set_f32(ctx, sh, "pupilRadius", d.pupil_radius);
  shader_set_f32(ctx, sh, "eyeOpen", d.eye_open);
  shader_set_f32(ctx, sh, "eyeStretch", d.eye_stretch);
  shader_set_vec2(ctx, sh, "dir", d.dir);
  shader_set_vec4(ctx, sh, "bodyColor", {body_color.r, body_color.g, body_color.b, body_color.a});
}
} // namespace

// A field of dots is enough for the floor: it has nothing to say, but a mote
// gliding over nothing does not read as moving at all.
void draw_floor(njin_ctx &ctx) {
  const rect view = camera_bounds(ctx);
  const i32 x0 = static_cast<i32>(std::floor(view.pos.x / grid));
  const i32 x1 = static_cast<i32>(std::ceil((view.pos.x + view.size.x) / grid));
  const i32 y0 = static_cast<i32>(std::floor(view.pos.y / grid));
  const i32 y1 = static_cast<i32>(std::ceil((view.pos.y + view.size.y) / grid));
  for (i32 y = y0; y <= y1; y++) {
    for (i32 x = x0; x <= x1; x++) {
      const bool landmark = x % 4 == 0 && y % 4 == 0;
      draw_circle(ctx, {static_cast<f32>(x) * grid, static_cast<f32>(y) * grid}, landmark ? 3.0f : 1.5f,
                  landmark ? landmark_color : dot_color);
    }
  }
}

// One quad per mote, over just that creature's bounds; the shader shapes
// everything from the uniforms and composites through its own alpha. Every
// creature in the world gets a pass here, the player's own mote and every
// autonomous one alike.
void draw_mote(njin_ctx &ctx) {
  if (!g.shader.id)
    return;
  for (auto [e, c] : world(ctx).view<const creature>().each()) {
    creature_draw d;
    creature_build_draw(c, d);
    set_uniforms(ctx, g.shader, d);

    shader_begin(ctx, g.shader);
    draw_rect(ctx, {d.bounds_min, d.bounds_max - d.bounds_min}, {1.0f, 1.0f, 1.0f, 1.0f});
    shader_end(ctx);
  }
}

void draw_hint(njin_ctx &ctx) {
  draw_text(ctx, "WASD / arrows / left stick: move", {16.0f, 16.0f}, 16.0f, {0.75f, 0.76f, 0.82f, 1.0f});
}
} // namespace moteswarm
