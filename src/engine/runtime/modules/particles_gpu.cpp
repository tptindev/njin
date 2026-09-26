#include "particles_gpu.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_gpu_caps.h"
#include "njin_log.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <utility>
#include <rlgl.h>
#include <raymath.h>

namespace njin {
namespace {
// How often the dead particles of a GPU emitter are removed, seconds, and when
// its clock is moved back to zero.
constexpr f32 compact_interval = 0.25f;
constexpr f32 clock_rebase = 600.0f;

// Where a particle is after `t` seconds, for velocity v0, constant gravity g and
// drag k (velocity decays as exp(-k t)):
//   p = v0 (1 - e^-kt) / k + g (t - (1 - e^-kt) / k) / k
// Below k t = 0.1 that subtracts two nearly equal numbers, so it switches to the
// Taylor series, which is also the k = 0 case: v0 t + g t^2 / 2.
constexpr const char *vertex_src = R"(#version 330
layout(location = 0) in vec2 corner;
layout(location = 1) in vec4 iPosVel;
layout(location = 2) in vec4 iTime;
layout(location = 3) in float iSize;
uniform mat4 mvp;
uniform vec2 gravity;
uniform float drag;
uniform float clock;
uniform vec2 sizes;
uniform vec4 colorStart;
uniform vec4 colorEnd;
uniform vec2 emitterPos;
uniform vec2 emitterX;
uniform vec2 emitterY;
uniform vec2 aspect;
out vec2 vUV;
out vec4 vColor;

vec2 displacement(vec2 v0, float t) {
  float u = drag * t;
  if (u < 0.1) {
    float tail = 1.0 - u * 0.25 * (1.0 - u * 0.2);
    float a = t * (1.0 - u * 0.5 * (1.0 - u / 3.0 * tail));
    float b = 0.5 * t * t * (1.0 - u / 3.0 * tail);
    return v0 * a + gravity * b;
  }
  float e = 1.0 - exp(-u);
  return v0 * (e / drag) + gravity * ((t - e / drag) / drag);
}

void main() {
  float age = clock - iTime.x;
  float life = iTime.y;
  float t01 = age / life;
  float size = mix(sizes.x, sizes.y, t01) * iSize;
  vec4 color = mix(colorStart, colorEnd, t01);
  if (age >= life || size <= 0.0 || color.a <= 0.0) {
    // Dead, or invisible: the CPU path skips these, so park them off screen.
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    vUV = vec2(0.0);
    vColor = vec4(0.0);
    return;
  }
  vec2 local = iPosVel.xy + displacement(iPosVel.zw, age);
  vec2 center = emitterPos + emitterX * local.x + emitterY * local.y;
  float angle = radians(iTime.z + iTime.w * age);
  float c = cos(angle);
  float s = sin(angle);
  vec2 q = corner * aspect * size;
  gl_Position = mvp * vec4(center + vec2(q.x * c - q.y * s, q.x * s + q.y * c), 0.0, 1.0);
  vUV = corner + 0.5;
  vColor = color;
}
)";

constexpr const char *fragment_src = R"(#version 330
in vec2 vUV;
in vec4 vColor;
uniform sampler2D texture0;
uniform int mode;
uniform vec4 source;
out vec4 finalColor;
void main() {
  if (mode == 2) {
    finalColor = texture(texture0, mix(source.xy, source.zw, vUV)) * vColor;
  } else if (mode == 0) {
    float d = length(vUV * 2.0 - 1.0);
    float edge = fwidth(d);
    finalColor = vec4(vColor.rgb, vColor.a * (1.0 - smoothstep(1.0 - edge, 1.0, d)));
  } else {
    finalColor = vColor;
  }
}
)";

// Six corners of a unit square centred on the origin: the instance offsets and
// scales it. The texture coordinate is the corner plus one half.
constexpr f32 quad_corners[12] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.5f,  0.5f,
                                  -0.5f, -0.5f, 0.5f, 0.5f,  -0.5f, 0.5f};

// Layout of njin::particle as the shader reads it.
static_assert(sizeof(particle) == 9 * sizeof(f32), "particle is uploaded as is");
static_assert(offsetof(particle, pos) == 0 && offsetof(particle, velocity) == 8 &&
                  offsetof(particle, age) == 16 && offsetof(particle, size) == 32,
              "particle is uploaded as is");

enum : int { mode_circle = 0, mode_square = 1, mode_texture = 2 };

// Points attributes 1..3 of the bound vertex array at `vbo`, one instance per
// particle.
void bind_instance_attributes(unsigned int vbo) {
  const int stride = (int)sizeof(particle);
  rlEnableVertexBuffer(vbo);
  rlSetVertexAttribute(1, 4, RL_FLOAT, false, stride, (int)offsetof(particle, pos));
  rlSetVertexAttribute(2, 4, RL_FLOAT, false, stride, (int)offsetof(particle, age));
  rlSetVertexAttribute(3, 1, RL_FLOAT, false, stride, (int)offsetof(particle, size));
  for (unsigned int index = 1; index <= 3; index++) {
    rlEnableVertexAttribute(index);
    rlSetVertexAttributeDivisor(index, 1);
  }
}

// Makes the vertex array and buffer of `buffer` exist and hold at least `bytes`.
// A larger buffer is a new one and starts empty: the caller uploads all again.
void ensure_capacity(const particle_gpu_state &gpu, particle_gpu_buffer &buffer, int bytes) {
  if (buffer.vao == 0) {
    buffer.vao = rlLoadVertexArray();
    rlEnableVertexArray(buffer.vao);
    rlEnableVertexBuffer(gpu.quad_vbo);
    rlSetVertexAttribute(0, 2, RL_FLOAT, false, 0, 0);
    rlEnableVertexAttribute(0);
    rlDisableVertexArray();
  }
  if (buffer.vbo != 0 && bytes <= buffer.capacity)
    return;
  const int wanted = std::max({bytes, buffer.capacity + buffer.capacity / 2, 4 * 1024});
  rlEnableVertexArray(buffer.vao);
  if (buffer.vbo != 0)
    rlUnloadVertexBuffer(buffer.vbo);
  buffer.vbo = rlLoadVertexBuffer(nullptr, wanted, true);
  buffer.capacity = wanted;
  buffer.uploaded = 0;
  bind_instance_attributes(buffer.vbo);
  rlDisableVertexArray();
}

void probe(particle_gpu_state &gpu) {
  if (gpu.probed)
    return;
  gpu.probed = true;

  const int version = rlGetVersion();
  if (version != RL_OPENGL_33 && version != RL_OPENGL_43 && version != RL_OPENGL_ES_30) {
    NJIN_INFO("particles: OpenGL has no instancing here, particles run on the CPU");
    return;
  }

  if (gpu_is_software()) {
    NJIN_INFO("particles: software renderer (%s), particles run on the CPU",
              gpu_renderer_name().c_str());
    return;
  }

  gpu.shader = LoadShaderFromMemory(vertex_src, fragment_src);
  if (!IsShaderValid(gpu.shader)) {
    NJIN_WARN("particles: GPU shader failed to compile, particles run on the CPU");
    return;
  }
  const unsigned int id = gpu.shader.id;
  gpu.loc_mvp = rlGetLocationUniform(id, "mvp");
  gpu.loc_gravity = rlGetLocationUniform(id, "gravity");
  gpu.loc_drag = rlGetLocationUniform(id, "drag");
  gpu.loc_clock = rlGetLocationUniform(id, "clock");
  gpu.loc_sizes = rlGetLocationUniform(id, "sizes");
  gpu.loc_color_start = rlGetLocationUniform(id, "colorStart");
  gpu.loc_color_end = rlGetLocationUniform(id, "colorEnd");
  gpu.loc_emitter_pos = rlGetLocationUniform(id, "emitterPos");
  gpu.loc_emitter_x = rlGetLocationUniform(id, "emitterX");
  gpu.loc_emitter_y = rlGetLocationUniform(id, "emitterY");
  gpu.loc_aspect = rlGetLocationUniform(id, "aspect");
  gpu.loc_mode = rlGetLocationUniform(id, "mode");
  gpu.loc_source = rlGetLocationUniform(id, "source");
  gpu.loc_texture = rlGetLocationUniform(id, "texture0");

  gpu.quad_vbo = rlLoadVertexBuffer(quad_corners, (int)sizeof quad_corners, false);

  gpu.available = true;
  NJIN_INFO("particles: GPU backend on (%s)", gpu_renderer_name().c_str());
}

void set_vec2(int loc, vec2 v) {
  const f32 value[2] = {v.x, v.y};
  rlSetUniform(loc, value, RL_SHADER_UNIFORM_VEC2, 1);
}

void set_vec4(int loc, f32 a, f32 b, f32 c, f32 d) {
  const f32 value[4] = {a, b, c, d};
  rlSetUniform(loc, value, RL_SHADER_UNIFORM_VEC4, 1);
}
} // namespace

particle_gpu_state::~particle_gpu_state() {
  if (!available)
    return;
  UnloadShader(shader);
  rlUnloadVertexBuffer(quad_vbo);
}

particle_gpu_buffer::~particle_gpu_buffer() {
  if (vbo != 0)
    rlUnloadVertexBuffer(vbo);
  if (vao != 0)
    rlUnloadVertexArray(vao);
}

particle_gpu_buffer::particle_gpu_buffer(particle_gpu_buffer &&other) noexcept
    : clock(other.clock), next_compact(other.next_compact),
      vao(std::exchange(other.vao, 0)), vbo(std::exchange(other.vbo, 0)),
      capacity(std::exchange(other.capacity, 0)), uploaded(std::exchange(other.uploaded, 0)) {}

particle_gpu_buffer &particle_gpu_buffer::operator=(particle_gpu_buffer &&other) noexcept {
  if (this != &other) {
    this->~particle_gpu_buffer();
    new (this) particle_gpu_buffer(std::move(other));
  }
  return *this;
}

void particles_gpu_step(particle_emitter &em, particle_gpu_buffer &buffer, f32 dt) {
  buffer.clock += dt;
  if (em.particles.empty()) {
    // Nothing is aging: start the clock over, which keeps it small and precise.
    buffer.clock = 0.0f;
    buffer.next_compact = compact_interval;
    buffer.uploaded = 0;
    return;
  }
  // Float seconds lose their milliseconds after a few hours; move the zero.
  if (buffer.clock > clock_rebase) {
    for (particle &p : em.particles)
      p.age -= buffer.clock;
    buffer.next_compact -= buffer.clock;
    buffer.clock = 0.0f;
    buffer.uploaded = 0;
  }
  // A full emitter refuses new particles, so it looks for room sooner (but not
  // every frame: the dead may be few).
  const bool full = (i32)em.particles.size() >= em.max_particles;
  const bool due = buffer.clock >= buffer.next_compact ||
                   (full && buffer.clock >= buffer.next_compact - 0.8f * compact_interval);
  if (!due)
    return;
  buffer.next_compact = buffer.clock + compact_interval;
  const f32 clock = buffer.clock;
  const usize before = em.particles.size();
  std::erase_if(em.particles, [clock](const particle &p) { return clock - p.age >= p.life; });
  if (em.particles.size() != before)
    buffer.uploaded = 0;
}

usize particles_gpu_alive(const particle_emitter &em, const particle_gpu_buffer &buffer) {
  usize alive = 0;
  for (const particle &p : em.particles)
    alive += buffer.clock - p.age < p.life ? 1 : 0;
  return alive;
}

void particles_set_backend(njin_ctx &ctx, particle_backend backend) {
  ctx.particles_gpu.backend = backend;
}

particle_backend particles_backend(const njin_ctx &ctx) { return ctx.particles_gpu.backend; }

bool particles_gpu_available(njin_ctx &ctx) {
  probe(ctx.particles_gpu);
  return ctx.particles_gpu.available;
}

bool particles_gpu_wanted(njin_ctx &ctx) {
  return ctx.particles_gpu.backend != particle_backend_cpu && particles_gpu_available(ctx);
}

void particles_gpu_draw(njin_ctx &ctx, entt::entity entity, const transform &tr,
                        const particle_emitter &em) {
  particle_gpu_state &gpu = ctx.particles_gpu;
  particle_gpu_buffer *buffer = ctx.ecs.registry.try_get<particle_gpu_buffer>(entity);
  if (!gpu.available || buffer == nullptr || em.particles.empty())
    return;

  const texture_slot *slot = texture_slot_of(ctx.texture, em.texture);
  int mode = em.shape == particle_square ? mode_square : mode_circle;
  vec2 aspect{1.0f, 1.0f};
  f32 source[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  if (slot != nullptr) {
    // Coordinates inside the texture (the whole atlas page for a packed image).
    const f32 width = (f32)slot->texture.width;
    const f32 height = (f32)slot->texture.height;
    const Rectangle area = texture_area(*slot);
    const bool whole = em.source.size.x == 0.0f || em.source.size.y == 0.0f;
    const vec2 origin = vec2{area.x, area.y} + (whole ? vec2{0.0f, 0.0f} : em.source.pos);
    const vec2 extent = whole ? vec2{area.width, area.height} : em.source.size;
    const f32 longest = std::max(extent.x, extent.y);
    if (longest > 0.0f && width > 0.0f && height > 0.0f) {
      mode = mode_texture;
      // The longest side of the image is the particle size, the other keeps
      // the image's proportions.
      aspect = {extent.x / longest, extent.y / longest};
      source[0] = origin.x / width;
      source[1] = origin.y / height;
      source[2] = (origin.x + extent.x) / width;
      source[3] = (origin.y + extent.y) / height;
    }
  }

  // Local-space particles ride the emitter: their position is turned into world
  // space with the same rotation and scale transform_combine() applies.
  vec2 emitter_pos{0.0f, 0.0f};
  vec2 emitter_x{1.0f, 0.0f};
  vec2 emitter_y{0.0f, 1.0f};
  if (em.local_space) {
    emitter_pos = tr.pos;
    emitter_x = rotate(vec2{tr.scale, 0.0f}, tr.rot);
    emitter_y = rotate(vec2{0.0f, tr.scale}, tr.rot);
  }

  const int bytes = (int)(em.particles.size() * sizeof(particle));
  const int count = (int)em.particles.size();

  render_stats &stats = ctx.stats;
  stats.emitters++;
  stats.particles += (u32)count;
  stats.particles_gpu += (u32)count;
  stats.instanced_calls++;
  stats.batches++; // the instanced call itself, and it ends the batch before it
  stats.note_flush();

  blend_begin(ctx, em.blend);
  // blend_begin only flushes when the blend mode changes. Sprites queued
  // before this emitter must reach the screen first, or they would draw on top.
  rlDrawRenderBatchActive();

  rlEnableShader(gpu.shader.id);
  const Matrix mvp = MatrixMultiply(
      MatrixMultiply(rlGetMatrixTransform(), rlGetMatrixModelview()), rlGetMatrixProjection());
  rlSetUniformMatrix(gpu.loc_mvp, mvp);
  set_vec2(gpu.loc_gravity, em.gravity);
  rlSetUniform(gpu.loc_drag, &em.drag, RL_SHADER_UNIFORM_FLOAT, 1);
  rlSetUniform(gpu.loc_clock, &buffer->clock, RL_SHADER_UNIFORM_FLOAT, 1);
  set_vec2(gpu.loc_sizes, {em.size_start, em.size_end});
  set_vec4(gpu.loc_color_start, em.color_start.r, em.color_start.g, em.color_start.b,
           em.color_start.a);
  set_vec4(gpu.loc_color_end, em.color_end.r, em.color_end.g, em.color_end.b, em.color_end.a);
  set_vec2(gpu.loc_emitter_pos, emitter_pos);
  set_vec2(gpu.loc_emitter_x, emitter_x);
  set_vec2(gpu.loc_emitter_y, emitter_y);
  set_vec2(gpu.loc_aspect, aspect);
  rlSetUniform(gpu.loc_mode, &mode, RL_SHADER_UNIFORM_INT, 1);
  set_vec4(gpu.loc_source, source[0], source[1], source[2], source[3]);
  if (mode == mode_texture) {
    const int unit = 0;
    rlActiveTextureSlot(unit);
    rlEnableTexture(slot->texture.id);
    rlSetUniform(gpu.loc_texture, &unit, RL_SHADER_UNIFORM_INT, 1);
  }

  // Only what is new since the last draw goes to the GPU, unless particles were
  // removed, which moves the rest and starts the upload over.
  ensure_capacity(gpu, *buffer, bytes);
  if (buffer->uploaded > (usize)count)
    buffer->uploaded = 0;
  if (buffer->uploaded < (usize)count) {
    const usize from = buffer->uploaded;
    rlUpdateVertexBuffer(buffer->vbo, em.particles.data() + from,
                         (int)((usize)count - from) * (int)sizeof(particle),
                         (int)from * (int)sizeof(particle));
    buffer->uploaded = (usize)count;
  }

  // The camera's projection may mirror the triangles; they are not culled.
  rlDisableBackfaceCulling();
  rlEnableVertexArray(buffer->vao);
  rlDrawVertexArrayInstanced(0, 6, count);
  rlDisableVertexArray();
  rlEnableBackfaceCulling();
  if (mode == mode_texture)
    rlDisableTexture();
  rlDisableShader();

  blend_end(ctx);
}
} // namespace njin
