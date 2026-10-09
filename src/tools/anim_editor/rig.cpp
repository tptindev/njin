// glTF in and out through cgltf: the parser compiled into raylib, and the
// vendored writer (third_party/cgltf/cgltf_write.h) of the same cgltf commit.
#include "document.h"
#include "raymath.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>

#define CGLTF_WRITE_IMPLEMENTATION
#include "cgltf_write.h"

namespace anim_editor {
namespace {
struct Loaded {
  cgltf_data *data = nullptr;
  ~Loaded() {
    if (data)
      cgltf_free(data);
  }
};
bool parse(const std::string &path, Loaded &out, std::string &error) {
  cgltf_options options{};
  if (cgltf_parse_file(&options, path.c_str(), &out.data) != cgltf_result_success) {
    error = "Cannot read glTF: " + path;
    return false;
  }
  if (cgltf_load_buffers(&options, out.data, path.c_str()) != cgltf_result_success) {
    error = "Cannot read the glTF's buffers (missing .bin next to it?)";
    return false;
  }
  return true;
}
Matrix to_matrix(const cgltf_float m[16]) {
  return {m[0], m[4], m[8], m[12], m[1], m[5], m[9], m[13], m[2], m[6], m[10], m[14], m[3], m[7], m[11], m[15]};
}
int node_index(const cgltf_data *data, const cgltf_node *node) { return node ? (int)(node - data->nodes) : -1; }

// A channel's value at `time`, glTF interpolation rules; `width` 3 or 4.
void sample_channel(const cgltf_animation_sampler &s, int width, float time, float *out) {
  const cgltf_size n = s.input->count;
  auto in = [&](cgltf_size i) {
    float t = 0;
    cgltf_accessor_read_float(s.input, i, &t, 1);
    return t;
  };
  const bool cubic = s.interpolation == cgltf_interpolation_type_cubic_spline;
  auto value = [&](cgltf_size i, int part, float *v) {
    cgltf_accessor_read_float(s.output, cubic ? i * 3 + part : i, v, (cgltf_size)width);
  };
  if (n == 0)
    return;
  if (time <= in(0) || n == 1) {
    value(0, 1, out);
    return;
  }
  if (time >= in(n - 1)) {
    value(n - 1, 1, out);
    return;
  }
  cgltf_size k = 0;
  while (k + 1 < n && in(k + 1) <= time)
    ++k;
  const float t0 = in(k), t1 = in(k + 1), dt = t1 - t0, u = dt > 0 ? (time - t0) / dt : 0;
  float a[4]{}, b[4]{};
  if (s.interpolation == cgltf_interpolation_type_step) {
    value(k, 1, out);
    return;
  }
  value(k, 1, a);
  value(k + 1, 1, b);
  if (cubic) {
    float ao[4]{}, bi[4]{};
    value(k, 2, ao);
    value(k + 1, 0, bi);
    const float u2 = u * u, u3 = u2 * u;
    for (int c = 0; c < width; ++c)
      out[c] = (2 * u3 - 3 * u2 + 1) * a[c] + (u3 - 2 * u2 + u) * dt * ao[c] + (-2 * u3 + 3 * u2) * b[c] +
               (u3 - u2) * dt * bi[c];
    if (width == 4) {
      Quaternion q = QuaternionNormalize({out[0], out[1], out[2], out[3]});
      out[0] = q.x, out[1] = q.y, out[2] = q.z, out[3] = q.w;
    }
    return;
  }
  if (width == 4) {
    Quaternion q = QuaternionSlerp({a[0], a[1], a[2], a[3]}, {b[0], b[1], b[2], b[3]}, u);
    out[0] = q.x, out[1] = q.y, out[2] = q.z, out[3] = q.w;
  } else
    for (int c = 0; c < width; ++c)
      out[c] = a[c] + (b[c] - a[c]) * u;
}

AnimationClip import_clip(const cgltf_data *data, const cgltf_animation &anim, const Rig &rig, int index) {
  AnimationClip clip;
  clip.name = anim.name && anim.name[0] ? std::string(anim.name).substr(0, 127) : "Animation " + std::to_string(index + 1);
  float duration = 0;
  for (cgltf_size i = 0; i < anim.samplers_count; ++i)
    if (anim.samplers[i].input->has_max)
      duration = std::max(duration, anim.samplers[i].input->max[0]);
  clip.duration = std::clamp(duration, 0.05f, 600.0f);
  for (int bone = 0; bone < (int)rig.bones.size(); ++bone) {
    const cgltf_animation_sampler *t = nullptr, *r = nullptr;
    for (cgltf_size c = 0; c < anim.channels_count; ++c) {
      const auto &ch = anim.channels[c];
      if (node_index(data, ch.target_node) != rig.bones[bone].node)
        continue;
      if (ch.target_path == cgltf_animation_path_type_translation)
        t = ch.sampler;
      if (ch.target_path == cgltf_animation_path_type_rotation)
        r = ch.sampler;
    }
    if (!t && !r)
      continue;
    std::vector<float> times;
    const bool linear = (!t || t->interpolation == cgltf_interpolation_type_linear) &&
                        (!r || r->interpolation == cgltf_interpolation_type_linear);
    if (linear) {
      for (const auto *s : {t, r})
        if (s)
          for (cgltf_size i = 0; i < s->input->count; ++i) {
            float v = 0;
            cgltf_accessor_read_float(s->input, i, &v, 1);
            times.push_back(v);
          }
      std::sort(times.begin(), times.end());
      times.erase(std::unique(times.begin(), times.end(), [](float a, float b) { return std::abs(a - b) < 0.0001f; }),
                  times.end());
    } else {
      // Step and cubic curves become keys on every frame of the clip's rate.
      const int frames = (int)std::ceil(clip.duration * clip.fps);
      for (int f = 0; f <= frames; ++f)
        times.push_back(std::min(clip.duration, (float)f / clip.fps));
    }
    const Bone &b = rig.bones[bone];
    for (float time : times) {
      float tv[4]{b.rest_translation.x, b.rest_translation.y, b.rest_translation.z, 0};
      float rv[4]{b.rest_rotation.x, b.rest_rotation.y, b.rest_rotation.z, b.rest_rotation.w};
      if (t)
        sample_channel(*t, 3, time, tv);
      if (r)
        sample_channel(*r, 4, time, rv);
      Matrix local = MatrixMultiply(MatrixMultiply(MatrixScale(b.rest_scale.x, b.rest_scale.y, b.rest_scale.z),
                                                   QuaternionToMatrix({rv[0], rv[1], rv[2], rv[3]})),
                                    MatrixTranslate(tv[0], tv[1], tv[2]));
      if (!set_key(clip, key_from_local(rig, bone, time, local)))
        break;
    }
  }
  return clip;
}
} // namespace

bool load_rig(const std::string &path, Rig &rig, std::vector<AnimationClip> &clips, std::string &error) {
  Loaded file;
  if (!parse(path, file, error))
    return false;
  const cgltf_data *data = file.data;
  if (data->skins_count == 0) {
    error = "The model has no skin (no rigged mesh): export it from your 3D tool with its armature.";
    return false;
  }
  Rig out;
  out.nodes.resize(data->nodes_count);
  for (cgltf_size i = 0; i < data->nodes_count; ++i) {
    cgltf_float m[16];
    cgltf_node_transform_local(&data->nodes[i], m);
    out.nodes[i].local = to_matrix(m);
    out.nodes[i].parent = node_index(data, data->nodes[i].parent);
  }
  const cgltf_skin &skin = data->skins[0];
  if (skin.joints_count > 128) {
    error = "The skin has more than 128 joints, njin's limit.";
    return false;
  }
  for (cgltf_size j = 0; j < skin.joints_count; ++j) {
    Bone b;
    b.node = node_index(data, skin.joints[j]);
    b.name = skin.joints[j]->name && skin.joints[j]->name[0] ? skin.joints[j]->name : "joint_" + std::to_string(j);
    b.name = b.name.substr(0, 120);
    MatrixDecompose(out.nodes[b.node].local, &b.rest_translation, &b.rest_rotation, &b.rest_scale);
    out.nodes[b.node].joint = (int)j;
    out.bones.push_back(b);
  }
  // Keys and projects name bones, so names must be unique.
  std::map<std::string, int> seen;
  for (auto &b : out.bones)
    if (seen[b.name]++ > 0)
      b.name += "#" + std::to_string(seen[b.name] - 1);
  for (auto &b : out.bones) {
    int n = out.nodes[b.node].parent;
    while (n >= 0 && out.nodes[n].joint < 0)
      n = out.nodes[n].parent;
    b.parent = n >= 0 ? out.nodes[n].joint : -1;
  }
  // Bounds from the meshes' POSITION ranges, placed by their nodes.
  auto world = std::vector<Matrix>(data->nodes_count);
  for (cgltf_size i = 0; i < data->nodes_count; ++i) {
    cgltf_float m[16];
    cgltf_node_transform_world(&data->nodes[i], m);
    world[i] = to_matrix(m);
  }
  Vector3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
  for (cgltf_size i = 0; i < data->nodes_count; ++i) {
    const cgltf_mesh *mesh = data->nodes[i].mesh;
    for (cgltf_size p = 0; mesh && p < mesh->primitives_count; ++p)
      for (cgltf_size a = 0; a < mesh->primitives[p].attributes_count; ++a) {
        const cgltf_accessor *acc = mesh->primitives[p].attributes[a].data;
        if (mesh->primitives[p].attributes[a].type != cgltf_attribute_type_position || !acc->has_min || !acc->has_max)
          continue;
        for (int c = 0; c < 8; ++c) {
          Vector3 v{(c & 1) ? acc->max[0] : acc->min[0], (c & 2) ? acc->max[1] : acc->min[1],
                    (c & 4) ? acc->max[2] : acc->min[2]};
          v = Vector3Transform(v, world[i]);
          lo = Vector3Min(lo, v);
          hi = Vector3Max(hi, v);
        }
      }
  }
  for (const auto &b : bone_world(out, nullptr)) {
    lo = Vector3Min(lo, {b.m12, b.m13, b.m14});
    hi = Vector3Max(hi, {b.m12, b.m13, b.m14});
  }
  out.bounds = {lo, hi};
  std::vector<AnimationClip> imported;
  for (cgltf_size a = 0; a < data->animations_count && imported.size() < max_clips; ++a)
    imported.push_back(import_clip(data, data->animations[a], out, (int)a));
  rig = std::move(out);
  clips = std::move(imported);
  error.clear();
  return true;
}

bool export_glb(const std::string &source, const Rig &rig, const std::vector<AnimationClip> &clips,
                const std::string &out, std::string &error) {
  Loaded file;
  if (!parse(source, file, error))
    return false;
  cgltf_data *data = file.data;
  if (data->skins_count == 0 || data->skins[0].joints_count != rig.bones.size()) {
    error = "The source model changed since it was opened (its skin no longer matches)";
    return false;
  }
  std::vector<uint8_t> blob;
  auto append = [&](const void *bytes, size_t size) {
    while (blob.size() % 4)
      blob.push_back(0);
    const size_t at = blob.size();
    blob.insert(blob.end(), (const uint8_t *)bytes, (const uint8_t *)bytes + size);
    return at;
  };
  // A .glb has one BIN chunk: every buffer of the source goes into it.
  std::vector<size_t> base(data->buffers_count);
  for (cgltf_size b = 0; b < data->buffers_count; ++b)
    base[b] = append(data->buffers[b].data, data->buffers[b].size);
  // Textures stored as files next to a .gltf are embedded.
  struct Picture {
    cgltf_size image;
    size_t offset, size;
    bool png;
  };
  std::vector<Picture> pictures;
  const auto folder = std::filesystem::path(reinterpret_cast<const char8_t *>(source.c_str())).parent_path();
  for (cgltf_size i = 0; i < data->images_count; ++i) {
    const cgltf_image &im = data->images[i];
    if (im.buffer_view || !im.uri || std::strncmp(im.uri, "data:", 5) == 0)
      continue;
    std::string uri = im.uri;
    cgltf_decode_uri(uri.data());
    uri.resize(std::strlen(uri.c_str()));
    std::ifstream in(folder / std::filesystem::path(reinterpret_cast<const char8_t *>(uri.c_str())), std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
    if (bytes.empty()) {
      error = "Cannot read texture '" + uri + "' next to the model";
      return false;
    }
    const bool png = std::filesystem::path(uri).extension() == ".png";
    pictures.push_back({i, append(bytes.data(), bytes.size()), bytes.size(), png});
  }
  // Each animated bone of a clip: key times, translations, rotations.
  struct Track {
    size_t clip, node, count, times, translations, rotations;
    float first, last;
  };
  std::vector<Track> tracks;
  for (size_t c = 0; c < clips.size(); ++c)
    for (int bone = 0; bone < (int)rig.bones.size(); ++bone) {
      std::vector<float> times, t, r;
      const Bone &b = rig.bones[bone];
      for (const auto &k : clips[c].keys) {
        if (k.bone != bone)
          continue;
        const Vector3 tv = Vector3Add(b.rest_translation, k.translation);
        Quaternion q = QuaternionNormalize(QuaternionMultiply(b.rest_rotation, euler_quaternion(k.rotation)));
        // One hemisphere, so LINEAR sampling turns the short way as the editor does.
        if (!r.empty() &&
            r[r.size() - 4] * q.x + r[r.size() - 3] * q.y + r[r.size() - 2] * q.z + r[r.size() - 1] * q.w < 0)
          q = {-q.x, -q.y, -q.z, -q.w};
        times.push_back(k.time);
        t.insert(t.end(), {tv.x, tv.y, tv.z});
        r.insert(r.end(), {q.x, q.y, q.z, q.w});
      }
      if (times.empty())
        continue;
      Track tr{c, (size_t)b.node, times.size(), 0, 0, 0, times.front(), times.back()};
      tr.times = append(times.data(), times.size() * sizeof(float));
      tr.translations = append(t.data(), t.size() * sizeof(float));
      tr.rotations = append(r.data(), r.size() * sizeof(float));
      tracks.push_back(tr);
    }
  while (blob.size() % 4)
    blob.push_back(0);

  cgltf_buffer buffer{};
  buffer.size = blob.size();
  buffer.data = blob.data();
  cgltf_buffer_view *old_views = data->buffer_views;
  cgltf_accessor *old_accessors = data->accessors;
  std::vector<cgltf_buffer_view> views(old_views, old_views + data->buffer_views_count);
  for (auto &v : views) {
    v.offset += base[(size_t)(v.buffer - data->buffers)];
    v.buffer = &buffer;
    if (v.has_meshopt_compression) {
      v.meshopt_compression.offset += base[(size_t)(v.meshopt_compression.buffer - data->buffers)];
      v.meshopt_compression.buffer = &buffer;
    }
  }
  auto add_view = [&](size_t offset, size_t size) {
    cgltf_buffer_view v{};
    v.buffer = &buffer;
    v.offset = offset;
    v.size = size;
    views.push_back(v);
    return views.size() - 1;
  };
  std::vector<size_t> picture_views;
  for (const auto &p : pictures)
    picture_views.push_back(add_view(p.offset, p.size));
  std::vector<std::array<size_t, 3>> track_views;
  for (const auto &t : tracks)
    track_views.push_back(
        {add_view(t.times, t.count * 4), add_view(t.translations, t.count * 12), add_view(t.rotations, t.count * 16)});
  // `views` is final from here, so pointers into it stay valid.
  auto view = [&](cgltf_buffer_view *v) { return v ? &views[(size_t)(v - old_views)] : nullptr; };
  std::vector<cgltf_accessor> accessors(old_accessors, old_accessors + data->accessors_count);
  for (auto &a : accessors) {
    a.buffer_view = view(a.buffer_view);
    if (a.is_sparse) {
      a.sparse.indices_buffer_view = view(a.sparse.indices_buffer_view);
      a.sparse.values_buffer_view = view(a.sparse.values_buffer_view);
    }
  }
  const size_t first_new = accessors.size();
  accessors.reserve(first_new + tracks.size() * 3);
  for (size_t i = 0; i < tracks.size(); ++i)
    for (int part = 0; part < 3; ++part) {
      cgltf_accessor a{};
      a.component_type = cgltf_component_type_r_32f;
      a.type = part == 0 ? cgltf_type_scalar : part == 1 ? cgltf_type_vec3 : cgltf_type_vec4;
      a.count = tracks[i].count;
      a.stride = cgltf_calc_size(a.type, a.component_type);
      a.buffer_view = &views[track_views[i][part]];
      if (part == 0) {
        a.has_min = a.has_max = true;
        a.min[0] = tracks[i].first;
        a.max[0] = tracks[i].last;
      }
      accessors.push_back(a);
    }
  // `accessors` is final from here too.
  auto accessor = [&](cgltf_accessor *a) { return a ? &accessors[(size_t)(a - old_accessors)] : nullptr; };

  // One animation per clip. Channels the editor does not edit (scale, morph
  // weights, nodes outside the skin) are kept from the source animation of the
  // same name, so exporting loses nothing the clip still has.
  struct Built {
    std::string name;
    std::vector<cgltf_animation_sampler> samplers;
    std::vector<cgltf_animation_channel> channels;
  };
  std::vector<Built> built(clips.size());
  for (size_t c = 0; c < clips.size(); ++c) {
    Built &b = built[c];
    b.name = clips[c].name;
    const cgltf_animation *source_anim = nullptr;
    for (cgltf_size a = 0; a < data->animations_count; ++a)
      if (data->animations[a].name && clips[c].name == data->animations[a].name)
        source_anim = &data->animations[a];
    std::vector<const cgltf_animation_channel *> kept;
    for (cgltf_size k = 0; source_anim && k < source_anim->channels_count; ++k) {
      const auto &ch = source_anim->channels[k];
      const int n = node_index(data, ch.target_node);
      const bool edited = n >= 0 && rig.nodes[(size_t)n].joint >= 0 &&
                          (ch.target_path == cgltf_animation_path_type_translation ||
                           ch.target_path == cgltf_animation_path_type_rotation);
      if (!edited)
        kept.push_back(&ch);
    }
    size_t mine = 0;
    for (const auto &t : tracks)
      mine += t.clip == c;
    // Channels point at samplers: no reallocation after the first push.
    b.samplers.reserve(mine * 2 + kept.size());
    b.channels.reserve(mine * 2 + kept.size());
    for (size_t i = 0; i < tracks.size(); ++i) {
      if (tracks[i].clip != c)
        continue;
      for (int part = 0; part < 2; ++part) {
        cgltf_animation_sampler s{};
        s.input = &accessors[first_new + i * 3];
        s.output = &accessors[first_new + i * 3 + 1 + (size_t)part];
        s.interpolation = cgltf_interpolation_type_linear;
        b.samplers.push_back(s);
        cgltf_animation_channel ch{};
        ch.sampler = &b.samplers.back();
        ch.target_node = &data->nodes[tracks[i].node];
        ch.target_path = part == 0 ? cgltf_animation_path_type_translation : cgltf_animation_path_type_rotation;
        b.channels.push_back(ch);
      }
    }
    for (const auto *ch : kept) {
      cgltf_animation_sampler s = *ch->sampler;
      s.input = accessor(s.input);
      s.output = accessor(s.output);
      b.samplers.push_back(s);
      cgltf_animation_channel copy = *ch;
      copy.sampler = &b.samplers.back();
      b.channels.push_back(copy);
    }
  }
  std::vector<cgltf_animation> animations(built.size());
  for (size_t i = 0; i < built.size(); ++i) {
    animations[i].name = built[i].name.data();
    animations[i].samplers = built[i].samplers.data();
    animations[i].samplers_count = built[i].samplers.size();
    animations[i].channels = built[i].channels.data();
    animations[i].channels_count = built[i].channels.size();
  }

  // Swap the new arrays in. cgltf_free owns the source's arrays, so every
  // change is undone before it runs.
  std::vector<std::function<void()>> undo;
  auto patch = [&](auto &field, auto value) {
    undo.push_back([&field, old = field] { field = old; });
    field = value;
  };
  for (cgltf_size m = 0; m < data->meshes_count; ++m)
    for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p) {
      cgltf_primitive &prim = data->meshes[m].primitives[p];
      patch(prim.indices, accessor(prim.indices));
      for (cgltf_size a = 0; a < prim.attributes_count; ++a)
        patch(prim.attributes[a].data, accessor(prim.attributes[a].data));
      for (cgltf_size t = 0; t < prim.targets_count; ++t)
        for (cgltf_size a = 0; a < prim.targets[t].attributes_count; ++a)
          patch(prim.targets[t].attributes[a].data, accessor(prim.targets[t].attributes[a].data));
      if (prim.has_draco_mesh_compression) {
        patch(prim.draco_mesh_compression.buffer_view, view(prim.draco_mesh_compression.buffer_view));
        for (cgltf_size a = 0; a < prim.draco_mesh_compression.attributes_count; ++a)
          patch(prim.draco_mesh_compression.attributes[a].data, accessor(prim.draco_mesh_compression.attributes[a].data));
      }
    }
  for (cgltf_size s = 0; s < data->skins_count; ++s)
    patch(data->skins[s].inverse_bind_matrices, accessor(data->skins[s].inverse_bind_matrices));
  for (cgltf_size n = 0; n < data->nodes_count; ++n)
    if (data->nodes[n].has_mesh_gpu_instancing)
      for (cgltf_size a = 0; a < data->nodes[n].mesh_gpu_instancing.attributes_count; ++a)
        patch(data->nodes[n].mesh_gpu_instancing.attributes[a].data,
              accessor(data->nodes[n].mesh_gpu_instancing.attributes[a].data));
  for (cgltf_size i = 0; i < data->images_count; ++i)
    patch(data->images[i].buffer_view, view(data->images[i].buffer_view));
  static char png_mime[] = "image/png", jpeg_mime[] = "image/jpeg";
  for (size_t i = 0; i < pictures.size(); ++i) {
    cgltf_image &im = data->images[pictures[i].image];
    patch(im.buffer_view, &views[picture_views[i]]);
    patch(im.uri, (char *)nullptr);
    patch(im.mime_type, pictures[i].png ? png_mime : jpeg_mime);
  }
  patch(data->buffers, &buffer);
  patch(data->buffers_count, (cgltf_size)1);
  patch(data->buffer_views, views.data());
  patch(data->buffer_views_count, (cgltf_size)views.size());
  patch(data->accessors, accessors.data());
  patch(data->accessors_count, (cgltf_size)accessors.size());
  patch(data->animations, animations.empty() ? (cgltf_animation *)nullptr : animations.data());
  patch(data->animations_count, (cgltf_size)animations.size());
  patch(data->bin, (const void *)blob.data());
  patch(data->bin_size, (cgltf_size)blob.size());

  cgltf_options options{};
  options.type = cgltf_file_type_glb;
  const cgltf_result written = cgltf_write_file(&options, out.c_str(), data);
  for (auto it = undo.rbegin(); it != undo.rend(); ++it)
    (*it)();
  if (written != cgltf_result_success) {
    error = "Cannot write " + out;
    return false;
  }
  error.clear();
  return true;
}
} // namespace anim_editor

