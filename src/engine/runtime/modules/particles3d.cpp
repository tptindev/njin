#include "particles3d.h"
#include "njin2rl.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_draw.h"
#include "njin_render.h"
#include "njin_texture.h"
#include <algorithm>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>

namespace njin {
namespace {
// Bounds what one careless spawn call can cost.
constexpr usize max_particles = 16384;

f32 pick(rng &random, f32_range range) {
  return range.max > range.min ? random.range(range.min, range.max) : range.min;
}

// A unit vector inside the cone of half angle `half` (radians) around `axis`,
// uniform over the cap of the sphere it cuts.
vec3 in_cone(rng &random, vec3 axis, f32 half) {
  const f32 z = random.range(std::cos(half), 1.0f);
  const f32 a = random.range(0.0f, 2.0f * pi);
  const f32 r = std::sqrt(std::max(0.0f, 1.0f - z * z));
  const vec3 local{r * std::cos(a), r * std::sin(a), z};
  // Any basis with `axis` as its third vector.
  const vec3 helper = std::fabs(axis.y) < 0.99f ? vec3{0.0f, 1.0f, 0.0f} : vec3{1.0f, 0.0f, 0.0f};
  const vec3 u = normalize(cross(helper, axis));
  const vec3 v = cross(axis, u);
  return u * local.x + v * local.y + axis * local.z;
}

usize live_count(const particles3d_state &s) {
  usize n = 0;
  for (const particle3d_burst &b : s.bursts)
    n += b.particles.size();
  return n;
}

void update(context &ctx) {
  const f32 dt = delta(ctx);
  if (dt <= 0.0f)
    return;
  for (particle3d_burst &b : ctx.particles3d.bursts) {
    const particle_emitter &em = b.look;
    const vec3 gravity{em.gravity.x * b.scale, -em.gravity.y * b.scale, 0.0f};
    // Same exponential drag as the 2D particles.
    const f32 keep = em.drag > 0.0f ? std::exp(-em.drag * dt) : 1.0f;
    for (particle3d &p : b.particles) {
      p.age += dt;
      p.velocity += gravity * dt;
      p.velocity *= keep;
      p.pos += p.velocity * dt;
      p.rotation += p.spin * dt;
    }
    std::erase_if(b.particles, [](const particle3d &p) { return p.age >= p.life; });
  }
  std::erase_if(ctx.particles3d.bursts, [](const particle3d_burst &b) { return b.particles.empty(); });
}

void setup(context &ctx) { ecs_register(ctx, phase_post_update, update, "update"); }

// A white disc with a one-pixel soft edge, so small particles stay round.
Texture2D make_disc() {
  constexpr i32 size = 64;
  Image image = GenImageColor(size, size, BLANK);
  const f32 c = (f32)size * 0.5f;
  for (i32 y = 0; y < size; y++) {
    for (i32 x = 0; x < size; x++) {
      const f32 d = std::hypot((f32)x + 0.5f - c, (f32)y + 0.5f - c);
      const f32 a = clamp(c - d, 0.0f, 1.0f);
      ImageDrawPixel(&image, x, y, Color{255, 255, 255, (unsigned char)(a * 255.0f)});
    }
  }
  Texture2D texture = LoadTextureFromImage(image);
  UnloadImage(image);
  SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
  return texture;
}
} // namespace

mod_desc particles3d_module() { return mod_desc{.name = "njin.particles3d", .setup = setup}; }

particles3d_state::~particles3d_state() {
  if (IsTextureValid(disc))
    UnloadTexture(disc);
}

void particles3d_spawn(context &ctx, const particle_emitter &emitter, vec3 pos, i32 count,
                       const particles3d_desc &desc) {
  particles3d_state &s = ctx.particles3d;
  const usize room = max_particles - std::min(max_particles, live_count(s));
  const usize n = std::min(room, (usize)std::max(count, 0));
  if (n == 0)
    return;
  particle3d_burst burst;
  burst.look = emitter;
  burst.look.particles.clear();
  burst.scale = desc.scale;
  burst.stretch = std::max(desc.stretch, 0.0f);
  burst.shader = desc.shader;
  burst.style = desc.style;
  burst.particles.reserve(n);
  rng &random = njin::random(ctx);
  const vec3 axis = length_sq(desc.direction) > 0.0f ? normalize(desc.direction) : vec3{0.0f, 1.0f, 0.0f};
  const f32 half = clamp(emitter.spread, 0.0f, 360.0f) * 0.5f * (pi / 180.0f);
  const vec3 area{emitter.area.x * desc.scale, emitter.area.y * desc.scale, emitter.area.x * desc.scale};
  for (usize i = 0; i < n; i++) {
    particle3d p;
    p.pos = pos + vec3{random.range(-0.5f, 0.5f) * area.x, random.range(-0.5f, 0.5f) * area.y,
                       random.range(-0.5f, 0.5f) * area.z};
    p.velocity = in_cone(random, axis, half) * (pick(random, emitter.speed) * desc.scale);
    p.life = std::max(pick(random, emitter.life), 0.001f);
    p.spin = pick(random, emitter.spin);
    p.rotation = p.spin != 0.0f ? random.range(0.0f, 360.0f) : 0.0f;
    p.size = pick(random, emitter.size_jitter);
    burst.particles.push_back(p);
  }
  s.bursts.push_back(std::move(burst));
}

void particles3d_clear(context &ctx) { ctx.particles3d.bursts.clear(); }

i32 particles3d_count(const context &ctx) { return (i32)live_count(ctx.particles3d); }

void particles3d_draw(context &ctx, const camera3d &view) {
  particles3d_state &s = ctx.particles3d;
  if (s.bursts.empty())
    return;
  if (!IsTextureValid(s.disc))
    s.disc = make_disc();
  Camera3D camera{};
  to_raylib(view.position, camera.position);
  to_raylib(view.target, camera.target);
  to_raylib(view.up, camera.up);
  camera.fovy = view.fovy;
  camera.projection = CAMERA_PERSPECTIVE;
  const Texture2D white{.id = rlGetTextureIdDefault(), .width = 1, .height = 1, .mipmaps = 1,
                        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};

  // Tested against the depth of the solid shapes, but not written: particles
  // do not hide each other, so their order does not matter.
  rlDrawRenderBatchActive();
  rlDisableDepthMask();
  render_stats &stats = ctx.stats;
  for (const particle3d_burst &b : s.bursts) {
    const particle_emitter &em = b.look;
    const texture_slot *slot = texture_slot_of(ctx.texture, em.texture);
    Texture2D texture = em.shape == particle_square ? white : s.disc;
    Rectangle source{0.0f, 0.0f, (f32)texture.width, (f32)texture.height};
    if (slot != nullptr) {
      texture = slot->texture;
      const Rectangle area = texture_area(*slot);
      const bool whole = em.source.size.x == 0.0f || em.source.size.y == 0.0f;
      source = whole ? area
                     : Rectangle{area.x + em.source.pos.x, area.y + em.source.pos.y, em.source.size.x,
                                 em.source.size.y};
    }
    const f32 longest = std::max(source.width, source.height);
    const f32 u0 = source.x / (f32)texture.width, u1 = (source.x + source.width) / (f32)texture.width;
    const f32 v0 = source.y / (f32)texture.height, v1 = (source.y + source.height) / (f32)texture.height;
    blend_begin(ctx, em.blend);
    // The game's shader, or the look of the burst's style.
    bool look = false;
    if (b.shader.id != 0) {
      rlDrawRenderBatchActive();
      shader_begin(ctx, b.shader);
    } else if (b.style != particle3d_flat) {
      rlDrawRenderBatchActive();
      look = particle3d_look_begin(ctx, s.looks, b.style);
    }
    stats.emitters++;
    for (const particle3d &p : b.particles) {
      const f32 t = p.age / p.life;
      const f32 size = lerp(em.size_start, em.size_end, t) * p.size * b.scale;
      const rgba tint = lerp(em.color_start, em.color_end, t);
      if (size <= 0.0f || tint.a <= 0.0f)
        continue;
      Color color{};
      to_raylib(tint, color);
      // The longer side of the image is the particle's size.
      const Vector2 extent{size * source.width / longest, size * source.height / longest};
      stats.particles++;
      if (b.stretch > 0.0f && length_sq(p.velocity) > 1e-8f) {
        // A streak: along the flight as the camera sees it, from where the
        // particle was `stretch` seconds ago to a little past where it is.
        const vec3 eye = normalize(p.pos - view.position);
        vec3 along = p.velocity - eye * dot(p.velocity, eye);
        const f32 run = length(along);
        if (run > 1e-6f) {
          along = along * (1.0f / run);
          const vec3 side = normalize(cross(along, eye)) * (extent.x * 0.5f);
          const vec3 head = p.pos + along * (extent.y * 0.5f);
          const vec3 tail = p.pos - along * (extent.y * 0.5f + run * b.stretch);
          // Seen from either side: which way round its corners go depends on the flight.
          rlDisableBackfaceCulling();
          rlSetTexture(texture.id);
          rlBegin(RL_QUADS);
          rlColor4ub(color.r, color.g, color.b, color.a);
          rlTexCoord2f(u0, v1);
          rlVertex3f(tail.x - side.x, tail.y - side.y, tail.z - side.z);
          rlTexCoord2f(u1, v1);
          rlVertex3f(tail.x + side.x, tail.y + side.y, tail.z + side.z);
          rlTexCoord2f(u1, v0);
          rlVertex3f(head.x + side.x, head.y + side.y, head.z + side.z);
          rlTexCoord2f(u0, v0);
          rlVertex3f(head.x - side.x, head.y - side.y, head.z - side.z);
          rlEnd();
          rlSetTexture(0);
          rlDrawRenderBatchActive();
          rlEnableBackfaceCulling();
          continue;
        }
      }
      Vector3 pos{};
      to_raylib(p.pos, pos);
      DrawBillboardPro(camera, texture, source, pos, camera.up, extent,
                       Vector2{extent.x * 0.5f, extent.y * 0.5f}, p.rotation, color);
    }
    if (b.shader.id != 0) {
      rlDrawRenderBatchActive();
      shader_end(ctx);
    } else if (look) {
      rlDrawRenderBatchActive();
      EndShaderMode();
    }
    blend_end(ctx);
  }
  rlDrawRenderBatchActive();
  rlEnableDepthMask();
}
} // namespace njin
