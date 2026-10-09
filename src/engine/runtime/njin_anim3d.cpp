#include "njin_anim3d.h"
#include "njin_ctx_impl.h"
#include "njin_log.h"
#include "njin_physics3d.h"
#include <raymath.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <string>

namespace njin {
namespace {
// A rotation as where it sends the three axes (a bone_pose3d's axes), so it
// composes and inverts without any quaternion convention.
struct rot3 {
  vec3 x{1.0f, 0.0f, 0.0f}, y{0.0f, 1.0f, 0.0f}, z{0.0f, 0.0f, 1.0f};
};

vec3 apply(const rot3 &r, vec3 v) { return r.x * v.x + r.y * v.y + r.z * v.z; }
vec3 apply_inv(const rot3 &r, vec3 v) { return {dot(r.x, v), dot(r.y, v), dot(r.z, v)}; }
// `a` after `b`.
rot3 compose(const rot3 &a, const rot3 &b) { return {apply(a, b.x), apply(a, b.y), apply(a, b.z)}; }
rot3 inverse(const rot3 &r) { return {{r.x.x, r.y.x, r.z.x}, {r.x.y, r.y.y, r.z.y}, {r.x.z, r.y.z, r.z.z}}; }
rot3 rot_of(const bone_pose3d &p) { return {p.x_axis, p.y_axis, p.z_axis}; }

// The rotation taking unit `a` to unit `b` the shortest way.
rot3 from_to(vec3 a, vec3 b) {
  const vec3 v = cross(a, b);
  const f32 c = dot(a, b);
  const auto turn = [&](vec3 e) {
    if (c < -0.9999f) { // half a turn about any axis across `a`
      const vec3 helper = std::fabs(a.x) < 0.9f ? vec3{1.0f, 0.0f, 0.0f} : vec3{0.0f, 1.0f, 0.0f};
      const vec3 n = normalize(cross(a, helper));
      return n * (2.0f * dot(n, e)) - e;
    }
    return e + cross(v, e) + cross(v, cross(v, e)) * (1.0f / (1.0f + c));
  };
  return {turn({1.0f, 0.0f, 0.0f}), turn({0.0f, 1.0f, 0.0f}), turn({0.0f, 0.0f, 1.0f})};
}

bool finite3(vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// Where a draw puts the model, as render3d does: scale, then rotation (z, x, y), then position.
struct placement {
  rot3 rot;
  vec3 scale{1.0f, 1.0f, 1.0f};
  vec3 position{};
  f32 uniform = 1.0f;
};

placement placement_of(const transform3d &t) {
  const vec3 r = t.rotation * (PI / 180.0f);
  const Matrix m = MatrixMultiply(MatrixMultiply(MatrixRotateZ(r.z), MatrixRotateX(r.x)), MatrixRotateY(r.y));
  const auto col = [&](f32 x, f32 y, f32 z) {
    const Vector3 v = Vector3Transform({x, y, z}, m);
    return vec3{v.x, v.y, v.z};
  };
  placement p;
  p.rot = {col(1, 0, 0), col(0, 1, 0), col(0, 0, 1)};
  p.scale = t.scale;
  p.position = t.position;
  p.uniform = (std::fabs(t.scale.x) + std::fabs(t.scale.y) + std::fabs(t.scale.z)) / 3.0f;
  return p;
}

vec3 to_world(const placement &w, vec3 p) {
  return w.position + apply(w.rot, {p.x * w.scale.x, p.y * w.scale.y, p.z * w.scale.z});
}

vec3 to_model(const placement &w, vec3 p) {
  const vec3 q = apply_inv(w.rot, p - w.position);
  const auto div = [](f32 a, f32 s) { return std::fabs(s) > 1e-12f ? a / s : 0.0f; };
  return {div(q.x, w.scale.x), div(q.y, w.scale.y), div(q.z, w.scale.z)};
}

// Every bone of `model`, parents before children.
std::vector<i32> parents_first(const context &ctx, model_handle model, i32 bones) {
  std::vector<i32> depth((usize)bones, 0);
  for (i32 b = 0; b < bones; b++)
    for (i32 p = model_bone_parent(ctx, model, b), guard = 0; p >= 0 && guard < bones;
         p = model_bone_parent(ctx, model, p), guard++)
      depth[(usize)b]++;
  std::vector<i32> order((usize)bones);
  for (i32 b = 0; b < bones; b++)
    order[(usize)b] = b;
  std::stable_sort(order.begin(), order.end(), [&](i32 a, i32 b) { return depth[(usize)a] < depth[(usize)b]; });
  return order;
}

spring_slot *spring_of(context &ctx, spring3d_handle h) {
  auto &v = ctx.anim3d.springs;
  if (h.id == 0 || h.id > v.size() || !v[h.id - 1].alive)
    return nullptr;
  return &v[h.id - 1];
}

const retarget_slot *retarget_of(const context &ctx, retarget3d_handle h) {
  const auto &v = ctx.anim3d.retargets;
  if (h.id == 0 || h.id > v.size() || !v[h.id - 1].alive)
    return nullptr;
  return &v[h.id - 1];
}

// The closest point to `p` on the segment `a`..`b`.
vec3 closest_on_segment(vec3 p, vec3 a, vec3 b) {
  const vec3 ab = b - a;
  const f32 len = length_sq(ab);
  if (len < 1e-12f)
    return a;
  return a + ab * std::clamp(dot(p - a, ab) / len, 0.0f, 1.0f);
}

constexpr f32 spring_step = 1.0f / 60.0f;
} // namespace

spring3d_handle spring3d_create(context &ctx, const spring3d_desc &desc) {
  const i32 bones = model_bone_count(ctx, desc.model);
  if (bones <= 0) {
    NJIN_WARN("spring3d_create: the model has no bones");
    return {};
  }
  spring_slot slot;
  slot.model = desc.model;
  slot.order = parents_first(ctx, desc.model, bones);
  slot.joint_of.assign((usize)bones, -1);
  std::vector<bone_pose3d> rest((usize)bones);
  for (i32 b = 0; b < bones; b++)
    rest[(usize)b] = model_bone_pose(ctx, desc.model, model_pose{}, b);
  const auto below = [&](i32 b, i32 root) {
    for (i32 guard = 0; b >= 0 && guard <= bones; b = model_bone_parent(ctx, desc.model, b), guard++)
      if (b == root)
        return true;
    return false;
  };
  for (u32 c = 0; c < desc.chain_count && desc.chains != nullptr; c++) {
    const spring3d_chain &chain = desc.chains[c];
    if (chain.bone < 0 || chain.bone >= bones) {
      NJIN_WARN("spring3d_create: chain %u has bone %d, the model has %d", c, chain.bone, bones);
      continue;
    }
    for (const i32 b : slot.order) {
      if (slot.joint_of[(usize)b] >= 0 || !below(b, chain.bone))
        continue;
      // The tail: the first child, or past a leaf as far as its parent is from it.
      i32 child = -1;
      for (i32 k = 0; k < bones && child < 0; k++)
        if (model_bone_parent(ctx, desc.model, k) == b)
          child = k;
      const vec3 at = rest[(usize)b].position;
      vec3 to{};
      if (child >= 0) {
        to = rest[(usize)child].position - at;
      } else {
        const i32 p = model_bone_parent(ctx, desc.model, b);
        to = p >= 0 ? at - rest[(usize)p].position : rest[(usize)b].y_axis * 0.1f;
      }
      const f32 len = length(to);
      if (len < 1e-5f)
        continue;
      spring_joint j;
      j.bone = b;
      j.axis = normalize(apply_inv(rot_of(rest[(usize)b]), to));
      j.length = len;
      j.stiffness = std::isfinite(chain.stiffness) ? std::max(chain.stiffness, 0.0f) : 1.0f;
      j.drag = std::isfinite(chain.drag) ? std::clamp(chain.drag, 0.0f, 1.0f) : 0.4f;
      j.gravity = std::isfinite(chain.gravity) ? chain.gravity : 0.0f;
      j.gravity_dir = finite3(chain.gravity_dir) && length_sq(chain.gravity_dir) > 1e-12f
                          ? normalize(chain.gravity_dir)
                          : vec3{0.0f, -1.0f, 0.0f};
      j.radius = std::isfinite(chain.radius) ? std::max(chain.radius, 0.0f) : 0.0f;
      slot.joint_of[(usize)b] = (i32)slot.joints.size();
      slot.joints.push_back(j);
    }
  }
  if (slot.joints.empty()) {
    NJIN_WARN("spring3d_create: no chain has a bone with a length");
    return {};
  }
  for (u32 c = 0; c < desc.collider_count && desc.colliders != nullptr; c++) {
    const spring3d_collider &col = desc.colliders[c];
    if (col.bone >= bones || !finite3(col.offset) || !finite3(col.tail) || !std::isfinite(col.radius)) {
      NJIN_WARN("spring3d_create: collider %u is not valid (bone %d, the model has %d), skipped", c, col.bone, bones);
      continue;
    }
    slot.colliders.push_back(col);
  }
  slot.alive = true;
  ctx.anim3d.springs.push_back(std::move(slot));
  return spring3d_handle{.id = (u32)ctx.anim3d.springs.size()};
}

i32 spring3d_update(context &ctx, spring3d_handle handle, const model_pose &pose, const transform3d &transform, f32 dt,
                    bone_pose3d *out, i32 count) {
  spring_slot *s = spring_of(ctx, handle);
  const i32 bones = s != nullptr ? model_bone_count(ctx, s->model) : 0;
  if (s == nullptr || out == nullptr || bones <= 0 || count < bones)
    return 0;
  std::vector<bone_pose3d> in((usize)bones);
  for (i32 b = 0; b < bones; b++)
    in[(usize)b] = model_bone_pose(ctx, s->model, pose, b);
  if (!finite3(transform.position) || !finite3(transform.rotation) || !finite3(transform.scale)) {
    NJIN_WARN("spring3d_update: the transform is not finite: the pose is drawn without springs");
    std::copy(in.begin(), in.end(), out);
    return bones;
  }
  if (!std::isfinite(dt) || dt < 0.0f)
    dt = 0.0f;
  dt = std::min(dt, 0.1f);
  const placement w = placement_of(transform);

  // The pose in each bone's parent: what the springs leave to the children.
  std::vector<i32> parent((usize)bones);
  std::vector<rot3> local_rot((usize)bones);
  std::vector<vec3> local_pos((usize)bones);
  for (i32 b = 0; b < bones; b++) {
    const i32 p = model_bone_parent(ctx, s->model, b);
    parent[(usize)b] = p;
    if (p < 0)
      continue;
    const rot3 pr = rot_of(in[(usize)p]);
    local_rot[(usize)b] = compose(inverse(pr), rot_of(in[(usize)b]));
    local_pos[(usize)b] = apply_inv(pr, in[(usize)b].position - in[(usize)p].position);
  }
  // Colliders where the animation puts them.
  struct sphere_line {
    vec3 a, b;
    f32 r;
  };
  std::vector<sphere_line> hits;
  for (const spring3d_collider &c : s->colliders) {
    const bool on_bone = c.bone >= 0;
    const rot3 r = on_bone ? rot_of(in[(usize)c.bone]) : rot3{};
    const vec3 at = on_bone ? in[(usize)c.bone].position : vec3{};
    hits.push_back({to_world(w, at + apply(r, c.offset)), to_world(w, at + apply(r, c.tail)), c.radius * w.uniform});
  }

  std::vector<vec3> pos((usize)bones);
  std::vector<rot3> rot((usize)bones);
  std::vector<u8> moved((usize)bones, 0);
  const auto pass = [&](bool integrate, f32 h) {
    for (const i32 b : s->order) {
      const i32 p = parent[(usize)b];
      const bool follow = p >= 0 && moved[(usize)p] != 0;
      if (follow) {
        rot[(usize)b] = compose(rot[(usize)p], local_rot[(usize)b]);
        pos[(usize)b] = pos[(usize)p] + apply(rot[(usize)p], local_pos[(usize)b] * w.uniform);
      } else {
        rot[(usize)b] = compose(w.rot, rot_of(in[(usize)b]));
        pos[(usize)b] = to_world(w, in[(usize)b].position);
      }
      moved[(usize)b] = follow ? 1 : 0;
      const i32 ji = s->joint_of[(usize)b];
      if (ji < 0)
        continue;
      spring_joint &j = s->joints[(usize)ji];
      const vec3 center = pos[(usize)b];
      const vec3 rest_dir = normalize(apply(rot[(usize)b], j.axis));
      const f32 len = j.length * w.uniform;
      if (s->fresh || !finite3(j.tail) || !finite3(j.prev))
        j.tail = j.prev = center + rest_dir * len;
      if (integrate) {
        vec3 next = j.tail + (j.tail - j.prev) * (1.0f - j.drag) + rest_dir * (j.stiffness * h) +
                    j.gravity_dir * (j.gravity * h);
        const auto on_sphere = [&](vec3 v) {
          const vec3 d = v - center;
          return length_sq(d) > 1e-12f ? center + normalize(d) * len : center + rest_dir * len;
        };
        next = on_sphere(next);
        for (const sphere_line &c : hits) {
          const vec3 q = closest_on_segment(next, c.a, c.b);
          const f32 r = c.r + j.radius * w.uniform;
          const vec3 d = next - q;
          if (length_sq(d) < r * r) {
            next = q + (length_sq(d) > 1e-12f ? normalize(d) : rest_dir) * r;
            next = on_sphere(next);
          }
        }
        j.prev = j.tail;
        j.tail = next;
      }
      const vec3 to = j.tail - center;
      if (length_sq(to) > 1e-12f)
        rot[(usize)b] = compose(from_to(rest_dir, normalize(to)), rot[(usize)b]);
      moved[(usize)b] = 1;
    }
    s->fresh = false;
  };
  const i32 steps = dt > 0.0f ? std::max(1, (i32)std::ceil(dt / spring_step - 1e-4f)) : 0;
  for (i32 k = 0; k < steps; k++)
    pass(true, dt / (f32)steps);
  if (steps == 0)
    pass(false, 0.0f);

  const rot3 back = inverse(w.rot);
  for (i32 b = 0; b < bones; b++) {
    if (moved[(usize)b] == 0) {
      out[b] = in[(usize)b];
      continue;
    }
    const rot3 r = compose(back, rot[(usize)b]);
    out[b] = bone_pose3d{.position = to_model(w, pos[(usize)b]), .x_axis = r.x, .y_axis = r.y, .z_axis = r.z};
  }
  return bones;
}

void spring3d_reset(context &ctx, spring3d_handle handle) {
  if (spring_slot *s = spring_of(ctx, handle))
    s->fresh = true;
}

void spring3d_destroy(context &ctx, spring3d_handle handle) {
  if (spring_slot *s = spring_of(ctx, handle))
    *s = spring_slot{};
}

namespace {
// `name` without a tool's prefix (`mixamorig:`, `Armature|`), lower case.
std::string plain_name(const char *name) {
  std::string s = name != nullptr ? name : "";
  const usize cut = s.find_last_of(":|");
  if (cut != std::string::npos)
    s = s.substr(cut + 1);
  for (char &c : s)
    c = (char)std::tolower((unsigned char)c);
  return s;
}

bool starts(const std::string &s, const char *p) { return s.rfind(p, 0) == 0; }
bool ends(const std::string &s, const char *p) {
  const usize n = std::strlen(p);
  return s.size() >= n && s.compare(s.size() - n, n, p) == 0;
}

struct humanoid_name {
  const char *core;
  const char *name;
};

// Joined lower-case cores (side and separators removed) and what they mean.
constexpr humanoid_name trunk[] = {
    {"hips", "hips"},          {"hip", "hips"},       {"pelvis", "hips"},        {"spine", "spine"},
    {"chest", "chest"},        {"upperchest", "upper_chest"}, {"neck", "neck"},  {"head", "head"},
};
constexpr humanoid_name limbs[] = {
    {"shoulder", "shoulder"},   {"clavicle", "shoulder"},  {"collar", "shoulder"},
    {"upperarm", "upper_arm"},  {"uparm", "upper_arm"},    {"arm", "upper_arm"},
    {"lowerarm", "lower_arm"},  {"forearm", "lower_arm"},  {"hand", "hand"},
    {"upperleg", "upper_leg"},  {"upleg", "upper_leg"},    {"thigh", "upper_leg"},
    {"lowerleg", "lower_leg"},  {"leg", "lower_leg"},      {"calf", "lower_leg"},
    {"shin", "lower_leg"},      {"foot", "foot"},          {"toes", "toes"},
    {"toebase", "toes"},        {"toe", "toes"},           {"ball", "toes"},
};
constexpr humanoid_name fingers[] = {
    {"thumb", "thumb"}, {"index", "index"}, {"middle", "middle"}, {"ring", "ring"}, {"pinky", "little"},
    {"little", "little"},
};
} // namespace

const char *bone_humanoid_name(const char *name) {
  static thread_local std::string result;
  std::string s = plain_name(name);
  if (s.empty())
    return "";
  // The side: a word at the start or the end, or "left"/"right" anywhere.
  i32 side = 0; // 1 left, 2 right
  const auto take = [&](const char *word, bool at_start, i32 which) {
    if (side != 0)
      return;
    const usize n = std::strlen(word);
    if (at_start && starts(s, word) && s.size() > n) {
      s = s.substr(n);
      side = which;
    } else if (!at_start && ends(s, word) && s.size() > n) {
      s = s.substr(0, s.size() - n);
      side = which;
    }
  };
  for (const char *sep : {".", "_", " ", "-"}) {
    take((std::string(sep) + "left").c_str(), false, 1);
    take((std::string(sep) + "right").c_str(), false, 2);
    take((std::string(sep) + "l").c_str(), false, 1);
    take((std::string(sep) + "r").c_str(), false, 2);
    take((std::string("l") + sep).c_str(), true, 1);
    take((std::string("r") + sep).c_str(), true, 2);
  }
  take("left", true, 1);
  take("right", true, 2);
  if (side == 0) {
    const usize l = s.find("left"), r = s.find("right");
    if (l != std::string::npos)
      side = 1, s.erase(l, 4);
    else if (r != std::string::npos)
      side = 2, s.erase(r, 5);
  }
  // Joined, and the number at its end apart ("spine_02" -> "spine", "02").
  std::string core;
  for (const char c : s)
    if (c != '.' && c != '_' && c != ' ' && c != '-')
      core += c;
  std::string digits;
  while (!core.empty() && std::isdigit((unsigned char)core.back())) {
    digits.insert(digits.begin(), core.back());
    core.pop_back();
  }
  const i32 number = digits.empty() ? -1 : std::atoi(digits.c_str());
  const bool padded = digits.size() >= 2 && digits[0] == '0'; // Unreal's 01, 02...

  if (side == 0) {
    if (core == "spine" && number >= 0) {
      // Unreal: spine_01..03; Mixamo: Spine, Spine1, Spine2.
      const i32 k = padded ? number - 1 : number;
      return k <= 0 ? "spine" : k == 1 ? "chest" : k == 2 ? "upper_chest" : "";
    }
    if (core == "neck" && number <= 1)
      return "neck";
    for (const humanoid_name &h : trunk)
      if (core == h.core && number < 0)
        return h.name;
    return "";
  }
  const char *prefix = side == 1 ? "left_" : "right_";
  // Fingers: Mixamo "HandThumb1", Unreal "thumb_01", VRM "ThumbProximal", Blender "f_index.01".
  std::string f = core;
  if (starts(f, "hand"))
    f = f.substr(4);
  if (starts(f, "f") && f.size() > 1 && !starts(f, "foot") && !starts(f, "fore"))
    for (const humanoid_name &h : fingers)
      if (starts(f.substr(1), h.core))
        f = f.substr(1);
  for (const humanoid_name &h : fingers) {
    if (!starts(f, h.core))
      continue;
    const std::string rest = f.substr(std::strlen(h.core));
    i32 joint = number;
    if (rest == "proximal" || rest == "metacarpal")
      joint = 1;
    else if (rest == "intermediate")
      joint = 2;
    else if (rest == "distal")
      joint = 3;
    else if (!rest.empty())
      continue;
    if (joint < 1 || joint > 3)
      return "";
    result = std::string(prefix) + h.name + "_" + std::to_string(joint);
    return result.c_str();
  }
  for (const humanoid_name &h : limbs)
    if (core == h.core && number < 0) {
      result = std::string(prefix) + h.name;
      return result.c_str();
    }
  return "";
}

retarget3d_handle retarget3d_create(context &ctx, const retarget3d_desc &desc) {
  const i32 sb = model_bone_count(ctx, desc.source), tb = model_bone_count(ctx, desc.target);
  if (sb <= 0 || tb <= 0) {
    NJIN_WARN("retarget3d_create: the %s model has no bones", sb <= 0 ? "source" : "target");
    return {};
  }
  retarget_slot slot;
  slot.source = desc.source;
  slot.target = desc.target;
  slot.source_of.assign((usize)tb, -1);
  for (u32 i = 0; i < desc.pair_count && desc.pairs != nullptr; i++) {
    const i32 s = model_bone_find(ctx, desc.source, desc.pairs[2 * i]);
    const i32 t = model_bone_find(ctx, desc.target, desc.pairs[2 * i + 1]);
    if (s < 0 || t < 0) {
      NJIN_WARN("retarget3d_create: pair %u ('%s', '%s') names a bone the model does not have", i,
                desc.pairs[2 * i] != nullptr ? desc.pairs[2 * i] : "", desc.pairs[2 * i + 1] != nullptr ? desc.pairs[2 * i + 1] : "");
      continue;
    }
    slot.source_of[(usize)t] = s;
  }
  std::vector<std::string> source_humanoid((usize)sb), source_plain((usize)sb);
  for (i32 b = 0; b < sb; b++) {
    source_humanoid[(usize)b] = bone_humanoid_name(model_bone_name(ctx, desc.source, b));
    source_plain[(usize)b] = plain_name(model_bone_name(ctx, desc.source, b));
  }
  if (desc.humanoid)
    for (i32 t = 0; t < tb; t++) {
      if (slot.source_of[(usize)t] >= 0)
        continue;
      const std::string h = bone_humanoid_name(model_bone_name(ctx, desc.target, t));
      const std::string plain = plain_name(model_bone_name(ctx, desc.target, t));
      for (i32 s = 0; s < sb && slot.source_of[(usize)t] < 0; s++)
        if (!h.empty() && source_humanoid[(usize)s] == h)
          slot.source_of[(usize)t] = s;
      for (i32 s = 0; s < sb && slot.source_of[(usize)t] < 0; s++)
        if (!plain.empty() && source_plain[(usize)s] == plain)
          slot.source_of[(usize)t] = s;
    }
  slot.order = parents_first(ctx, desc.target, tb);
  // The hips: the target's humanoid hips if mapped, else the mapped bone nearest the root.
  for (i32 t = 0; t < tb && slot.hips < 0; t++)
    if (slot.source_of[(usize)t] >= 0 && std::strcmp(bone_humanoid_name(model_bone_name(ctx, desc.target, t)), "hips") == 0)
      slot.hips = t;
  for (const i32 t : slot.order)
    if (slot.hips < 0 && slot.source_of[(usize)t] >= 0)
      slot.hips = t;
  if (slot.hips < 0) {
    NJIN_WARN("retarget3d_create: no bone of the target matches one of the source");
    return {};
  }
  // Leg length: how high the hips stand over the lowest bone, at rest.
  const auto hips_height = [&](model_handle m, i32 bones, i32 hips) {
    f32 low = 1e30f;
    for (i32 b = 0; b < bones; b++)
      low = std::min(low, model_bone_pose(ctx, m, model_pose{}, b).position.y);
    return model_bone_pose(ctx, m, model_pose{}, hips).position.y - low;
  };
  const f32 hs = hips_height(desc.source, sb, slot.source_of[(usize)slot.hips]);
  const f32 ht = hips_height(desc.target, tb, slot.hips);
  slot.scale = hs > 1e-5f && ht > 1e-5f ? ht / hs : 1.0f;
  slot.alive = true;
  ctx.anim3d.retargets.push_back(std::move(slot));
  return retarget3d_handle{.id = (u32)ctx.anim3d.retargets.size()};
}

i32 retarget3d_pose(const context &ctx, retarget3d_handle handle, const model_pose &pose, bone_pose3d *out,
                    i32 count) {
  const retarget_slot *r = retarget_of(ctx, handle);
  const i32 tb = r != nullptr ? model_bone_count(ctx, r->target) : 0;
  const i32 sb = r != nullptr ? model_bone_count(ctx, r->source) : 0;
  if (r == nullptr || out == nullptr || tb <= 0 || sb <= 0 || count < tb || (i32)r->source_of.size() != tb)
    return 0;
  std::vector<rot3> rot((usize)tb);
  for (const i32 b : r->order) {
    const bone_pose3d rest = model_bone_pose(ctx, r->target, model_pose{}, b);
    const i32 p = model_bone_parent(ctx, r->target, b);
    const i32 s = r->source_of[(usize)b];
    // How the parent turned from its rest pose: the unmatched child goes along.
    rot3 parent_turn{};
    vec3 parent_at{};
    bone_pose3d parent_rest{};
    if (p >= 0) {
      parent_rest = model_bone_pose(ctx, r->target, model_pose{}, p);
      parent_turn = compose(rot[(usize)p], inverse(rot_of(parent_rest)));
      parent_at = out[p].position;
    }
    if (s >= 0) {
      const bone_pose3d now = model_bone_pose(ctx, r->source, pose, s);
      const bone_pose3d was = model_bone_pose(ctx, r->source, model_pose{}, s);
      rot[(usize)b] = compose(compose(rot_of(now), inverse(rot_of(was))), rot_of(rest));
      if (b == r->hips) {
        out[b].position = rest.position + (now.position - was.position) * r->scale;
      }
    } else {
      rot[(usize)b] = p >= 0 ? compose(parent_turn, rot_of(rest)) : rot_of(rest);
    }
    if (b != r->hips || s < 0)
      out[b].position = p >= 0 ? parent_at + apply(parent_turn, rest.position - parent_rest.position) : rest.position;
    out[b].x_axis = rot[(usize)b].x;
    out[b].y_axis = rot[(usize)b].y;
    out[b].z_axis = rot[(usize)b].z;
  }
  return tb;
}

i32 retarget3d_source_bone(const context &ctx, retarget3d_handle handle, i32 bone) {
  const retarget_slot *r = retarget_of(ctx, handle);
  if (r == nullptr || bone < 0 || bone >= (i32)r->source_of.size())
    return -1;
  return r->source_of[(usize)bone];
}

void retarget3d_destroy(context &ctx, retarget3d_handle handle) {
  auto &v = ctx.anim3d.retargets;
  if (handle.id != 0 && handle.id <= v.size())
    v[handle.id - 1] = retarget_slot{};
}

namespace {
foot_slot *foot_of(context &ctx, foot3d_handle h) {
  auto &v = ctx.anim3d.feet;
  if (h.id == 0 || h.id > v.size() || !v[h.id - 1].alive)
    return nullptr;
  return &v[h.id - 1];
}

const foot_slot *foot_of(const context &ctx, foot3d_handle h) {
  const auto &v = ctx.anim3d.feet;
  if (h.id == 0 || h.id > v.size() || !v[h.id - 1].alive)
    return nullptr;
  return &v[h.id - 1];
}

// A world-space vector in the model's space (a direction, no position).
vec3 vector_to_model(const placement &w, vec3 v) {
  const vec3 q = apply_inv(w.rot, v);
  const auto div = [](f32 a, f32 s) { return std::fabs(s) > 1e-12f ? a / s : 0.0f; };
  return {div(q.x, w.scale.x), div(q.y, w.scale.y), div(q.z, w.scale.z)};
}

bool below(const std::vector<i32> &parent, i32 b, i32 root) {
  for (i32 guard = 0; b >= 0 && guard < (i32)parent.size(); guard++, b = parent[(usize)b])
    if (b == root)
      return true;
  return false;
}

// Turns bone `root` and every bone under it by `r` about `pivot`.
void turn_subtree(std::vector<bone_pose3d> &bones, const std::vector<i32> &parent, i32 root, const rot3 &r,
                  vec3 pivot) {
  for (i32 b = 0; b < (i32)bones.size(); b++) {
    if (!below(parent, b, root))
      continue;
    bone_pose3d &p = bones[(usize)b];
    p.position = pivot + apply(r, p.position - pivot);
    p.x_axis = apply(r, p.x_axis);
    p.y_axis = apply(r, p.y_axis);
    p.z_axis = apply(r, p.z_axis);
  }
}

// The shortest turn from direction `a` to direction `b` (any length).
rot3 turn_between(vec3 a, vec3 b) {
  if (length_sq(a) < 1e-12f || length_sq(b) < 1e-12f)
    return {};
  return from_to(normalize(a), normalize(b));
}

// Thigh, shin and foot of `leg` bend so the ankle reaches `target`: the knee on
// the side the pose bent it to, or towards `pole` when the pose has the leg straight.
void reach(std::vector<bone_pose3d> &bones, const std::vector<i32> &parent, const foot3d_leg &leg, vec3 target,
           vec3 pole) {
  const vec3 root = bones[(usize)leg.upper].position, mid = bones[(usize)leg.lower].position,
             end = bones[(usize)leg.foot].position;
  const f32 l1 = length(mid - root), l2 = length(end - mid);
  if (l1 < 1e-6f || l2 < 1e-6f)
    return;
  const vec3 to = target - root;
  if (length_sq(to) < 1e-12f)
    return;
  const f32 d = std::clamp(length(to), std::fabs(l1 - l2) + 1e-4f, l1 + l2 - 1e-4f);
  const vec3 u = normalize(to);
  vec3 side = (mid - root) - u * dot(mid - root, u);
  if (length(side) < 0.1f * l1)
    side = pole - u * dot(pole, u);
  if (length_sq(side) < 1e-12f)
    side = cross(bones[(usize)leg.upper].x_axis, u);
  if (length_sq(side) < 1e-12f)
    return;
  const vec3 v = normalize(side);
  const f32 a = std::acos(std::clamp((l1 * l1 + d * d - l2 * l2) / (2.0f * l1 * d), -1.0f, 1.0f));
  turn_subtree(bones, parent, leg.upper, turn_between(mid - root, (u * std::cos(a) + v * std::sin(a)) * l1), root);
  const vec3 m = bones[(usize)leg.lower].position;
  turn_subtree(bones, parent, leg.lower, turn_between(bones[(usize)leg.foot].position - m, root + u * d - m), m);
}

bone_pose3d blend_pose(const bone_pose3d &a, const bone_pose3d &b, f32 k) {
  bone_pose3d p;
  p.position = a.position + (b.position - a.position) * k;
  const vec3 x = a.x_axis + (b.x_axis - a.x_axis) * k, y = a.y_axis + (b.y_axis - a.y_axis) * k;
  if (length_sq(x) < 1e-12f || length_sq(y) < 1e-12f)
    return k < 0.5f ? a : b;
  p.y_axis = normalize(y);
  p.z_axis = normalize(cross(normalize(x), p.y_axis));
  p.x_axis = cross(p.y_axis, p.z_axis);
  return p;
}

bool physics_ground(const context &ctx, vec3 from, f32 distance, vec3 &point, vec3 &normal) {
  const ray3d_hit hit = physics3d_raycast(ctx, ray3d{.origin = from, .direction = {0.0f, -1.0f, 0.0f}}, distance);
  if (!hit.hit)
    return false;
  point = hit.point;
  normal = hit.normal;
  return true;
}

i32 place_feet(context &ctx, foot3d_handle handle, const model_pose &pose, const transform3d &transform, f32 dt,
               bone_pose3d *out, i32 count, const foot3d_ground *ground, f32 weight) {
  foot_slot *s = foot_of(ctx, handle);
  const i32 bones = s != nullptr ? model_bone_count(ctx, s->model) : 0;
  if (s == nullptr || out == nullptr || bones <= 0 || count < bones)
    return 0;
  std::vector<bone_pose3d> in((usize)bones);
  for (i32 b = 0; b < bones; b++)
    in[(usize)b] = model_bone_pose(ctx, s->model, pose, b);
  if (!std::isfinite(weight))
    weight = 0.0f;
  weight = std::clamp(weight, 0.0f, 1.0f);
  if (!finite3(transform.position) || !finite3(transform.rotation) || !finite3(transform.scale)) {
    NJIN_WARN("foot3d_update: the transform is not finite: the pose is drawn without foot placement");
    weight = 0.0f;
  }
  if (weight <= 0.0f) {
    std::copy(in.begin(), in.end(), out);
    return bones;
  }
  if (!std::isfinite(dt) || dt < 0.0f)
    dt = 0.0f;
  const placement w = placement_of(transform);
  std::vector<i32> parent((usize)bones);
  for (i32 b = 0; b < bones; b++)
    parent[(usize)b] = model_bone_parent(ctx, s->model, b);

  // Easing: at once after a reset or with dt 0, up twice as fast as down.
  const bool snap = s->fresh || dt <= 0.0f;
  const f32 down = snap ? 1.0f : 1.0f - std::exp(-dt * s->smoothing);
  const f32 up = snap ? 1.0f : 1.0f - std::exp(-dt * 2.0f * s->smoothing);
  const auto ease = [&](f32 &v, f32 want) { v += (want - v) * (want > v ? up : down); };
  const vec3 up_m = normalize(vector_to_model(w, {0.0f, 1.0f, 0.0f}));
  const f32 tilt_min = std::cos(s->max_tilt * (PI / 180.0f));
  const f32 base = transform.position.y;

  f32 hip_want = 1e9f;
  for (foot_leg_state &leg : s->legs) {
    const vec3 ankle = to_world(w, in[(usize)leg.bones.foot].position);
    const vec3 from{ankle.x, base + s->max_step + 0.05f, ankle.z};
    const f32 reach_down = 2.0f * s->max_step + 0.05f;
    vec3 point{}, normal{0.0f, 1.0f, 0.0f};
    const bool hit = ground != nullptr ? (*ground)(from, reach_down, point, normal)
                                       : physics_ground(ctx, from, reach_down, point, normal);
    f32 lift = 0.0f;
    vec3 surface{0.0f, 1.0f, 0.0f};
    if (hit && std::isfinite(point.y)) {
      lift = std::clamp(point.y - base, -s->max_step, s->max_step);
      if (finite3(normal) && length_sq(normal) > 1e-12f && normalize(normal).y >= tilt_min)
        surface = normalize(normal);
    }
    ease(leg.lift, lift);
    const vec3 n = normalize(vector_to_model(w, surface));
    const vec3 eased = snap ? n : leg.normal + (n - leg.normal) * down;
    leg.normal = length_sq(eased) > 1e-12f ? normalize(eased) : up_m;
    // The hips ease once, toward where the lower foot is going (not where its
    // eased height is), so they do not trail the feet down a step.
    hip_want = std::min(hip_want, lift);
  }
  if (hip_want > 1e8f)
    hip_want = 0.0f;
  ease(s->hip, hip_want);
  s->fresh = false;

  std::vector<bone_pose3d> res = in;
  const vec3 drop = vector_to_model(w, {0.0f, s->hip, 0.0f});
  for (bone_pose3d &b : res)
    b.position = b.position + drop;
  const vec3 pole = length_sq(s->knee_forward) > 1e-12f ? normalize(s->knee_forward) : vec3{0.0f, 0.0f, 1.0f};
  for (const foot_leg_state &leg : s->legs) {
    const bone_pose3d foot = res[(usize)leg.bones.foot];
    // On a slope the ankle stands its height above the ground along the normal,
    // so higher over the point under it by 1 / cos of the slope.
    const f32 ny = std::max(apply(w.rot, leg.normal).y, 0.2f);
    const f32 height = std::max(to_world(w, in[(usize)leg.bones.foot].position).y - base, 0.0f);
    const f32 slope_lift = height * (1.0f / ny - 1.0f);
    reach(res, parent, leg.bones,
          foot.position + vector_to_model(w, {0.0f, leg.lift + slope_lift - s->hip, 0.0f}), pole);
    // The foot keeps the pose's turn, then tips onto the ground it stands on.
    const vec3 ankle = res[(usize)leg.bones.foot].position;
    turn_subtree(res, parent, leg.bones.foot, compose(rot_of(foot), inverse(rot_of(res[(usize)leg.bones.foot]))),
                 ankle);
    turn_subtree(res, parent, leg.bones.foot, turn_between(up_m, leg.normal), ankle);
  }
  for (i32 b = 0; b < bones; b++)
    out[b] = weight >= 1.0f ? res[(usize)b] : blend_pose(in[(usize)b], res[(usize)b], weight);
  return bones;
}
} // namespace

foot3d_handle foot3d_create(context &ctx, const foot3d_desc &desc) {
  const i32 bones = model_bone_count(ctx, desc.model);
  if (bones <= 0) {
    NJIN_WARN("foot3d_create: the model has no bones");
    return {};
  }
  std::vector<foot3d_leg> legs;
  if (desc.legs != nullptr) {
    legs.assign(desc.legs, desc.legs + desc.leg_count);
  } else {
    // The two human legs, by their standard names.
    for (const char *side : {"left_", "right_"}) {
      foot3d_leg leg;
      for (i32 b = 0; b < bones; b++) {
        const std::string n = bone_humanoid_name(model_bone_name(ctx, desc.model, b));
        if (n.rfind(side, 0) != 0)
          continue;
        const std::string part = n.substr(std::strlen(side));
        if (part == "upper_leg" && leg.upper < 0)
          leg.upper = b;
        else if (part == "lower_leg" && leg.lower < 0)
          leg.lower = b;
        else if (part == "foot" && leg.foot < 0)
          leg.foot = b;
      }
      legs.push_back(leg);
    }
  }
  foot_slot slot;
  for (const foot3d_leg &l : legs) {
    const auto ok = [&](i32 b) { return b >= 0 && b < bones; };
    if (!ok(l.upper) || !ok(l.lower) || !ok(l.foot)) {
      NJIN_WARN("foot3d_create: a leg is missing its thigh, shin or foot bone: skipped");
      continue;
    }
    slot.legs.push_back({.bones = l});
  }
  if (slot.legs.empty()) {
    NJIN_WARN("foot3d_create: no leg has a thigh, a shin and a foot bone");
    return {};
  }
  const auto finite = [](f32 v, f32 fallback) { return std::isfinite(v) ? v : fallback; };
  slot.alive = true;
  slot.model = desc.model;
  slot.max_step = std::max(finite(desc.max_step, 0.5f), 0.0f);
  slot.max_tilt = std::clamp(finite(desc.max_tilt, 35.0f), 0.0f, 89.0f);
  slot.smoothing = std::max(finite(desc.smoothing, 15.0f), 0.0f);
  slot.knee_forward = finite3(desc.knee_forward) ? desc.knee_forward : vec3{0.0f, 0.0f, 1.0f};
  ctx.anim3d.feet.push_back(std::move(slot));
  return foot3d_handle{.id = (u32)ctx.anim3d.feet.size()};
}

i32 foot3d_update(context &ctx, foot3d_handle handle, const model_pose &pose, const transform3d &transform, f32 dt,
                  bone_pose3d *out, i32 count, f32 weight) {
  return place_feet(ctx, handle, pose, transform, dt, out, count, nullptr, weight);
}

i32 foot3d_update(context &ctx, foot3d_handle handle, const model_pose &pose, const transform3d &transform, f32 dt,
                  bone_pose3d *out, i32 count, const foot3d_ground &ground, f32 weight) {
  return place_feet(ctx, handle, pose, transform, dt, out, count, ground ? &ground : nullptr, weight);
}

f32 foot3d_hip_offset(const context &ctx, foot3d_handle handle) {
  const foot_slot *s = foot_of(ctx, handle);
  return s != nullptr ? s->hip : 0.0f;
}

void foot3d_reset(context &ctx, foot3d_handle handle) {
  if (foot_slot *s = foot_of(ctx, handle))
    s->fresh = true;
}

void foot3d_destroy(context &ctx, foot3d_handle handle) {
  if (foot_slot *s = foot_of(ctx, handle))
    *s = foot_slot{};
}
} // namespace njin
