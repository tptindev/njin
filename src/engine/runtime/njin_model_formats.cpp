// Translated from raylib 6.0 rmodels.c (LoadIQM, LoadModelAnimationsIQM, LoadVOX, LoadM3D,
// LoadModelAnimationsM3D) and the raymath.h functions they use.
// raylib: zlib licence, Copyright (c) 2013-2026 Ramon Santamaria (@raysan5).
// Altered: no GPU upload, no raylib types; the data lands in plain CPU structs
// (njin_model_formats.h), textures are returned as a file path or RGBA8 pixels.
#include "njin_model_formats.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

// Both libraries are also compiled inside raylib (rmodels.c), which is still linked
// beside this file: their external symbols are renamed here so the two copies do not
// clash at link time. The headers themselves are unmodified (third_party/).
#define Vox_LoadFromMemory njin_vox_load_from_memory
#define Vox_FreeArrays njin_vox_free_arrays
#define m3d_load njin_m3d_load
#define m3d_free njin_m3d_free
#define m3d_pose njin_m3d_pose
#define m3d_frame njin_m3d_frame
#define m3d_save njin_m3d_save
#define _m3d_safestr njin_m3d_safestr
#define _m3d_gettx njin_m3d_gettx
#define _m3d_getpr njin_m3d_getpr
#define _m3d_mul njin_m3d_mul
#define _m3d_inv njin_m3d_inv
#define _m3d_mat njin_m3d_mat
#define _m3d_addhdr njin_m3d_addhdr
#define _m3dstbi_zlib_decode_malloc_guesssize_headerflag njin_m3d_zlib_decode_malloc_guesssize_headerflag

// raylib's rmodels.c defines these before including the two headers (RL_* are malloc,
// calloc, realloc and free in a default raylib build).
#define VOX_MALLOC(sz) std::malloc(sz)
#define VOX_CALLOC(n, sz) std::calloc(n, sz)
#define VOX_REALLOC(p, sz) std::realloc(p, sz)
#define VOX_FREE(p) std::free(p)
#define VOX_LOADER_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#pragma GCC diagnostic ignored "-Wstringop-truncation"
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include "vox_loader.h"

#define M3D_MALLOC(sz) std::malloc(sz)
#define M3D_REALLOC(p, nsz) std::realloc(p, nsz)
#define M3D_FREE(p) std::free(p)
#define M3D_IMPLEMENTATION
#include "m3d.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace njin {
namespace {

// raymath.h, same formulas and order of operations.
struct vec3 {
  float x, y, z;
};
struct quat {
  float x, y, z, w;
};
struct xform {
  vec3 translation{0, 0, 0};
  quat rotation{0, 0, 0, 0};
  vec3 scale{0, 0, 0};
};

quat quaternion_normalize(quat q) {
  quat result{0, 0, 0, 0};
  float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (length == 0.0f) length = 1.0f;
  float ilength = 1.0f / length;
  result.x = q.x * ilength;
  result.y = q.y * ilength;
  result.z = q.z * ilength;
  result.w = q.w * ilength;
  return result;
}

quat quaternion_multiply(quat q1, quat q2) {
  quat result{0, 0, 0, 0};
  float qax = q1.x, qay = q1.y, qaz = q1.z, qaw = q1.w;
  float qbx = q2.x, qby = q2.y, qbz = q2.z, qbw = q2.w;
  result.x = qax * qbw + qaw * qbx + qay * qbz - qaz * qby;
  result.y = qay * qbw + qaw * qby + qaz * qbx - qax * qbz;
  result.z = qaz * qbw + qaw * qbz + qax * qby - qay * qbx;
  result.w = qaw * qbw - qax * qbx - qay * qby - qaz * qbz;
  return result;
}

vec3 vector3_rotate_by_quaternion(vec3 v, quat q) {
  vec3 result{0, 0, 0};
  result.x = v.x * (q.x * q.x + q.w * q.w - q.y * q.y - q.z * q.z) + v.y * (2 * q.x * q.y - 2 * q.w * q.z) +
             v.z * (2 * q.x * q.z + 2 * q.w * q.y);
  result.y = v.x * (2 * q.w * q.z + 2 * q.x * q.y) + v.y * (q.w * q.w - q.x * q.x + q.y * q.y - q.z * q.z) +
             v.z * (-2 * q.w * q.x + 2 * q.y * q.z);
  result.z = v.x * (-2 * q.w * q.y + 2 * q.x * q.z) + v.y * (2 * q.w * q.x + 2 * q.y * q.z) +
             v.z * (q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z);
  return result;
}

vec3 vector3_add(vec3 a, vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
vec3 vector3_multiply(vec3 a, vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }

imported_bone to_bone(const xform &t) {
  imported_bone b;
  b.translation = {t.translation.x, t.translation.y, t.translation.z};
  b.rotation = {t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w};
  b.scale = {t.scale.x, t.scale.y, t.scale.z};
  return b;
}

// raylib's legacy renderer in njin played every keyframe of a ModelAnimation at 60 per
// second (render3d.cpp anim_fps); raylib itself stores no rate.
constexpr float legacy_keyframe_rate = 60.0f;

bool read_file(const std::string &path, std::vector<unsigned char> &data, std::string &error) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    error = "cannot open " + path;
    return false;
  }
  data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  if (data.empty()) {
    error = "empty file " + path;
    return false;
  }
  return true;
}

// raylib's GetDirectoryPath: the folder without a trailing separator, "." for none.
std::string directory_of(const std::string &path) {
  std::string dir = std::filesystem::path(path).parent_path().generic_string();
  return dir.empty() ? std::string(".") : dir;
}

// A string of at most `n` bytes, as raylib copies names into char[n] buffers.
std::string fixed_string(const unsigned char *p, std::size_t n) {
  std::size_t len = 0;
  while (len < n && p[len] != 0) len++;
  return std::string(reinterpret_cast<const char *>(p), len);
}

// ---------------------------------------------------------------------------------
// IQM
// ---------------------------------------------------------------------------------

struct IQMHeader {
  char magic[16];
  unsigned int version;
  unsigned int dataSize;
  unsigned int flags;
  unsigned int num_text, ofs_text;
  unsigned int num_meshes, ofs_meshes;
  unsigned int num_vertexarrays, num_vertexes, ofs_vertexarrays;
  unsigned int num_triangles, ofs_triangles, ofs_adjacency;
  unsigned int num_joints, ofs_joints;
  unsigned int num_poses, ofs_poses;
  unsigned int num_anims, ofs_anims;
  unsigned int num_frames, num_framechannels, ofs_frames, ofs_bounds;
  unsigned int num_comment, ofs_comment;
  unsigned int num_extensions, ofs_extensions;
};

struct IQMMesh {
  unsigned int name;
  unsigned int material;
  unsigned int first_vertex, num_vertexes;
  unsigned int first_triangle, num_triangles;
};

struct IQMTriangle {
  unsigned int vertex[3];
};

struct IQMJoint {
  unsigned int name;
  int parent;
  float translate[3], rotate[4], scale[3];
};

struct IQMVertexArray {
  unsigned int type;
  unsigned int flags;
  unsigned int format;
  unsigned int size;
  unsigned int offset;
};

struct IQMPose {
  int parent;
  unsigned int mask;
  float channeloffset[10];
  float channelscale[10];
};

struct IQMAnim {
  unsigned int name;
  unsigned int first_frame, num_frames;
  float framerate;
  unsigned int flags;
};

enum {
  IQM_POSITION = 0,
  IQM_TEXCOORD = 1,
  IQM_NORMAL = 2,
  IQM_TANGENT = 3,
  IQM_BLENDINDEXES = 4,
  IQM_BLENDWEIGHTS = 5,
  IQM_COLOR = 6,
  IQM_CUSTOM = 0x10
};

constexpr unsigned BONE_NAME_LENGTH = 32;
constexpr unsigned MATERIAL_NAME_LENGTH = 32;

// raylib reads the file without bounds checks; these keep a malformed file from
// reading past the end, and refuse it instead.
struct iqm_reader {
  const std::vector<unsigned char> &data;
  bool fits(std::uint64_t offset, std::uint64_t bytes) const { return offset + bytes <= data.size(); }
  template <class T> bool array(std::uint64_t offset, std::uint64_t count, std::vector<T> &out) const {
    if (!fits(offset, count * sizeof(T))) return false;
    out.resize(count);
    if (count) std::memcpy(out.data(), data.data() + offset, count * sizeof(T));
    return true;
  }
  // A text entry copied as raylib does (a fixed number of bytes from ofs_text + name),
  // clamped to the end of the file.
  std::string text(const IQMHeader &h, unsigned name, unsigned n) const {
    std::uint64_t at = std::uint64_t(h.ofs_text) + name;
    if (at >= data.size()) return {};
    std::uint64_t avail = data.size() - at;
    return fixed_string(data.data() + at, std::size_t(avail < n ? avail : n));
  }
};

bool iqm_header(const std::vector<unsigned char> &data, IQMHeader &h, const std::string &path, std::string &error) {
  static const char IQM_MAGIC[] = "INTERQUAKEMODEL";
  if (data.size() < sizeof(IQMHeader)) {
    error = "IQM file is not a valid model: " + path;
    return false;
  }
  std::memcpy(&h, data.data(), sizeof(IQMHeader));
  if (std::memcmp(h.magic, IQM_MAGIC, sizeof(IQM_MAGIC)) != 0) {
    error = "IQM file is not a valid model: " + path;
    return false;
  }
  if (h.version != 2) {
    error = "IQM file version not supported (" + std::to_string(h.version) + "): " + path;
    return false;
  }
  return true;
}

bool iqm_model(const std::vector<unsigned char> &data, const IQMHeader &h, const std::string &path,
               imported_model &out, std::string &error) {
  const iqm_reader r{data};
  const auto malformed = [&](const char *what) {
    error = std::string("IQM file is malformed (") + what + "): " + path;
    return false;
  };
  const std::string basePath = directory_of(path);

  std::vector<IQMMesh> imesh;
  if (!r.array(h.ofs_meshes, h.num_meshes, imesh)) return malformed("meshes");

  out.meshes.resize(h.num_meshes);
  out.materials.resize(h.num_meshes);
  for (unsigned i = 0; i < h.num_meshes; i++) {
    const std::string material = r.text(h, imesh[i].material, MATERIAL_NAME_LENGTH);
    // LoadMaterialDefault() then LoadTexture("<basePath>/<material>").
    if (!material.empty()) out.materials[i].albedo_path = basePath + "/" + material;

    imported_mesh &m = out.meshes[i];
    m.material = int(i);
    const std::size_t vc = imesh[i].num_vertexes;
    if (std::uint64_t(imesh[i].first_vertex) + vc > h.num_vertexes) return malformed("mesh vertices");
    if (std::uint64_t(imesh[i].first_triangle) + imesh[i].num_triangles > h.num_triangles)
      return malformed("mesh triangles");
    m.positions.assign(vc * 3, 0.0f);
    m.normals.assign(vc * 3, 0.0f);
    m.texcoords.assign(vc * 2, 0.0f);
    m.bone_ids.assign(vc * 4, 0);
    m.bone_weights.assign(vc * 4, 0.0f);
    m.indices.assign(std::size_t(imesh[i].num_triangles) * 3, 0);
  }

  // Triangles: IQM winding is reversed to raylib's, indices kept as unsigned short.
  std::vector<IQMTriangle> tri;
  if (!r.array(h.ofs_triangles, h.num_triangles, tri)) return malformed("triangles");
  for (unsigned m = 0; m < h.num_meshes; m++) {
    std::size_t tcounter = 0;
    for (unsigned i = imesh[m].first_triangle; i < (imesh[m].first_triangle + imesh[m].num_triangles); i++) {
      auto &ind = out.meshes[m].indices;
      ind[tcounter + 2] = std::uint16_t(tri[i].vertex[0] - imesh[m].first_vertex);
      ind[tcounter + 1] = std::uint16_t(tri[i].vertex[1] - imesh[m].first_vertex);
      ind[tcounter] = std::uint16_t(tri[i].vertex[2] - imesh[m].first_vertex);
      tcounter += 3;
    }
  }

  std::vector<IQMVertexArray> va;
  if (!r.array(h.ofs_vertexarrays, h.num_vertexarrays, va)) return malformed("vertex arrays");
  const std::uint64_t nv = h.num_vertexes;
  for (unsigned a = 0; a < h.num_vertexarrays; a++) {
    switch (va[a].type) {
    case IQM_POSITION:
    case IQM_NORMAL: {
      std::vector<float> src;
      if (!r.array(va[a].offset, nv * 3, src)) return malformed("vertex array");
      for (unsigned m = 0; m < h.num_meshes; m++) {
        auto &dst = va[a].type == IQM_POSITION ? out.meshes[m].positions : out.meshes[m].normals;
        std::size_t vCounter = 0;
        for (std::size_t i = std::size_t(imesh[m].first_vertex) * 3;
             i < (std::size_t(imesh[m].first_vertex) + imesh[m].num_vertexes) * 3; i++)
          dst[vCounter++] = src[i];
      }
    } break;
    case IQM_TEXCOORD: {
      std::vector<float> src;
      if (!r.array(va[a].offset, nv * 2, src)) return malformed("vertex array");
      for (unsigned m = 0; m < h.num_meshes; m++) {
        std::size_t vCounter = 0;
        for (std::size_t i = std::size_t(imesh[m].first_vertex) * 2;
             i < (std::size_t(imesh[m].first_vertex) + imesh[m].num_vertexes) * 2; i++)
          out.meshes[m].texcoords[vCounter++] = src[i];
      }
    } break;
    case IQM_BLENDINDEXES:
    case IQM_BLENDWEIGHTS:
    case IQM_COLOR: {
      std::vector<unsigned char> src;
      if (!r.array(va[a].offset, nv * 4, src)) return malformed("vertex array");
      for (unsigned m = 0; m < h.num_meshes; m++) {
        imported_mesh &mesh = out.meshes[m];
        if (va[a].type == IQM_COLOR) mesh.colors.assign(std::size_t(imesh[m].num_vertexes) * 4, 0);
        std::size_t counter = 0;
        for (std::size_t i = std::size_t(imesh[m].first_vertex) * 4;
             i < (std::size_t(imesh[m].first_vertex) + imesh[m].num_vertexes) * 4; i++, counter++) {
          if (va[a].type == IQM_BLENDINDEXES)
            mesh.bone_ids[counter] = src[i];
          else if (va[a].type == IQM_BLENDWEIGHTS)
            mesh.bone_weights[counter] = src[i] / 255.0f;
          else
            mesh.colors[counter] = src[i];
        }
      }
    } break;
    default:
      break;
    }
  }

  // Bones and their bind pose, then BuildPoseFromParentJoints.
  std::vector<IQMJoint> ijoint;
  if (!r.array(h.ofs_joints, h.num_joints, ijoint)) return malformed("joints");
  std::vector<xform> bind(h.num_joints);
  out.bones.resize(h.num_joints);
  for (unsigned i = 0; i < h.num_joints; i++) {
    out.bones[i].parent = ijoint[i].parent;
    out.bones[i].name = r.text(h, ijoint[i].name, BONE_NAME_LENGTH);
    bind[i].translation = {ijoint[i].translate[0], ijoint[i].translate[1], ijoint[i].translate[2]};
    bind[i].rotation = {ijoint[i].rotate[0], ijoint[i].rotate[1], ijoint[i].rotate[2], ijoint[i].rotate[3]};
    bind[i].scale = {ijoint[i].scale[0], ijoint[i].scale[1], ijoint[i].scale[2]};
  }
  for (unsigned i = 0; i < h.num_joints; i++) {
    const int p = out.bones[i].parent;
    if (p >= 0) {
      if (p > int(i)) continue;  // not topologically sorted: raylib skips it
      bind[i].rotation = quaternion_multiply(bind[p].rotation, bind[i].rotation);
      bind[i].scale = vector3_multiply(bind[i].scale, bind[p].scale);
      bind[i].translation = vector3_multiply(bind[i].translation, bind[p].scale);
      bind[i].translation = vector3_rotate_by_quaternion(bind[i].translation, bind[p].rotation);
      bind[i].translation = vector3_add(bind[i].translation, bind[p].translation);
    }
  }
  for (unsigned i = 0; i < h.num_joints; i++) {
    imported_bone b = to_bone(bind[i]);
    out.bones[i].translation = b.translation;
    out.bones[i].rotation = b.rotation;
    out.bones[i].scale = b.scale;
  }
  return true;
}

bool iqm_animations(const std::vector<unsigned char> &data, const IQMHeader &h, const std::string &path,
                    imported_model &out, std::string &error) {
  const iqm_reader r{data};
  const auto malformed = [&](const char *what) {
    error = std::string("IQM file is malformed (") + what + "): " + path;
    return false;
  };
  std::vector<IQMPose> poses;
  if (!r.array(h.ofs_poses, h.num_poses, poses)) return malformed("poses");
  std::vector<IQMAnim> anim;
  if (!r.array(h.ofs_anims, h.num_anims, anim)) return malformed("animations");
  std::vector<unsigned short> framedata;
  if (!r.array(h.ofs_frames, std::uint64_t(h.num_frames) * h.num_framechannels, framedata))
    return malformed("frames");

  for (unsigned a = 0; a < h.num_anims; a++) {
    imported_clip clip;
    clip.name = r.text(h, anim[a].name, 32);
    // raylib ignores anim[a].framerate ("TODO: Use animation framerate data?").
    clip.fps = legacy_keyframe_rate;
    std::vector<std::vector<xform>> frames(anim[a].num_frames, std::vector<xform>(h.num_poses));

    std::uint64_t dcounter = std::uint64_t(anim[a].first_frame) * h.num_framechannels;
    for (unsigned frame = 0; frame < anim[a].num_frames; frame++) {
      for (unsigned i = 0; i < h.num_poses; i++) {
        float ch[10];
        for (unsigned c = 0; c < 10; c++) {
          ch[c] = poses[i].channeloffset[c];
          if (poses[i].mask & (1u << c)) {
            if (dcounter >= framedata.size()) return malformed("frame channels");
            ch[c] += framedata[dcounter] * poses[i].channelscale[c];
            dcounter++;
          }
        }
        xform &t = frames[frame][i];
        t.translation = {ch[0], ch[1], ch[2]};
        t.rotation = {ch[3], ch[4], ch[5], ch[6]};
        t.scale = {ch[7], ch[8], ch[9]};
        t.rotation = quaternion_normalize(t.rotation);
      }
    }

    // Build frameposes (model space).
    for (unsigned frame = 0; frame < anim[a].num_frames; frame++) {
      for (unsigned i = 0; i < h.num_poses; i++) {
        const int p = poses[i].parent;
        if (p >= 0) {
          if (unsigned(p) >= h.num_poses) return malformed("pose parent");
          xform &t = frames[frame][i];
          const xform &pt = frames[frame][p];
          t.rotation = quaternion_multiply(pt.rotation, t.rotation);
          t.translation = vector3_rotate_by_quaternion(t.translation, pt.rotation);
          t.translation = vector3_add(t.translation, pt.translation);
          t.scale = vector3_multiply(t.scale, pt.scale);
        }
      }
    }

    clip.frames.resize(frames.size());
    for (std::size_t f = 0; f < frames.size(); f++) {
      clip.frames[f].reserve(frames[f].size());
      for (const xform &t : frames[f]) clip.frames[f].push_back(to_bone(t));
    }
    out.clips.push_back(std::move(clip));
  }
  return true;
}

// ---------------------------------------------------------------------------------
// M3D
// ---------------------------------------------------------------------------------

// m3d_load asks for external files (textures, material libraries) by name; raylib
// reads them relative to the working directory, njin against the model's folder first.
thread_local std::string m3d_base_dir;

unsigned char *m3d_read_hook(char *fn, unsigned int *len) {
  *len = 0;
  if (fn == nullptr) return nullptr;
  std::filesystem::path p(fn);
  std::ifstream f;
  if (p.is_relative() && !m3d_base_dir.empty()) f.open(std::filesystem::path(m3d_base_dir) / p, std::ios::binary);
  if (!f) f.open(p, std::ios::binary);
  if (!f) return nullptr;
  std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (bytes.empty()) return nullptr;
  auto *buf = static_cast<unsigned char *>(std::malloc(bytes.size()));
  if (buf == nullptr) return nullptr;
  std::memcpy(buf, bytes.data(), bytes.size());
  *len = static_cast<unsigned int>(bytes.size());
  return buf;
}

void m3d_free_hook(void *data) { std::free(data); }

struct m3d_handle {
  std::vector<unsigned char> data;  // must outlive the m3d_t (it may point into it)
  m3d_t *m3d = nullptr;
  ~m3d_handle() {
    if (m3d) m3d_free(m3d);
  }
};

bool m3d_open(const std::string &path, m3d_handle &h, std::string &error) {
  if (!read_file(path, h.data, error)) return false;
  // m3d_load trusts the length in the file header; refuse a file shorter than it says.
  if (h.data.size() < 8) {
    error = "M3D file is not valid: " + path;
    return false;
  }
  std::uint32_t declared = 0;
  std::memcpy(&declared, h.data.data() + 4, 4);
  if (declared > h.data.size()) {
    error = "M3D file is truncated: " + path;
    return false;
  }
  m3d_base_dir = directory_of(path);
  h.m3d = m3d_load(h.data.data(), m3d_read_hook, m3d_free_hook, nullptr);
  m3d_base_dir.clear();
  if (!h.m3d || M3D_ERR_ISFATAL(h.m3d->errcode)) {
    error = "failed to load M3D data, error code " + std::to_string(h.m3d ? int(h.m3d->errcode) : -2) + ": " + path;
    return false;
  }
  return true;
}

std::array<float, 4> color_from_bytes(std::uint32_t c) {
  unsigned char b[4];
  std::memcpy(b, &c, 4);  // as raylib's memcpy into a Color (r, g, b, a)
  return {b[0] / 255.0f, b[1] / 255.0f, b[2] / 255.0f, b[3] / 255.0f};
}

// An m3d texture (1 grey, 2 grey+alpha, 3 RGB, 4 RGBA) as the RGBA8 the GPU sampled.
void texture_to_rgba(const m3dtx_t &tx, imported_material &mat) {
  if (tx.d == nullptr || tx.w == 0 || tx.h == 0) return;
  const unsigned f = tx.f;
  const std::size_t n = std::size_t(tx.w) * tx.h;
  mat.albedo_width = tx.w;
  mat.albedo_height = tx.h;
  mat.albedo_rgba.resize(n * 4);
  for (std::size_t i = 0; i < n; i++) {
    unsigned char *o = &mat.albedo_rgba[i * 4];
    if (f == 4) {
      std::memcpy(o, tx.d + i * 4, 4);
    } else if (f == 3) {
      o[0] = tx.d[i * 3 + 0], o[1] = tx.d[i * 3 + 1], o[2] = tx.d[i * 3 + 2], o[3] = 255;
    } else if (f == 2) {
      o[0] = o[1] = o[2] = tx.d[i * 2 + 0], o[3] = tx.d[i * 2 + 1];
    } else {
      o[0] = o[1] = o[2] = tx.d[i], o[3] = 255;
    }
  }
}

xform m3d_bone_xform(const m3d_t *m3d, M3D_INDEX pos, M3D_INDEX ori) {
  xform t;
  t.translation.x = m3d->vertex[pos].x * m3d->scale;
  t.translation.y = m3d->vertex[pos].y * m3d->scale;
  t.translation.z = m3d->vertex[pos].z * m3d->scale;
  t.rotation.x = m3d->vertex[ori].x;
  t.rotation.y = m3d->vertex[ori].y;
  t.rotation.z = m3d->vertex[ori].z;
  t.rotation.w = m3d->vertex[ori].w;
  t.rotation = quaternion_normalize(t.rotation);
  t.scale.x = t.scale.y = t.scale.z = 1.0f;
  return t;
}

// Child bones are stored in parent bone relative space: converted into model space.
void m3d_to_model_space(xform &t, const xform &parent) {
  t.rotation = quaternion_multiply(parent.rotation, t.rotation);
  t.translation = vector3_rotate_by_quaternion(t.translation, parent.rotation);
  t.translation = vector3_add(t.translation, parent.translation);
  t.scale = vector3_multiply(t.scale, parent.scale);
}

std::string m3d_name(const char *s) {
  if (s == nullptr) return {};
  return std::string(s, strnlen(s, 31));  // strncpy into char[32], 31 characters kept
}

void m3d_model(const m3d_t *m3d, imported_model &out) {
  out.materials.assign(1, imported_material{});  // index 0: LoadMaterialDefault()
  // Check if face is found, if not, probably just a material library.
  if (!m3d->numface) return;

  const bool skinned = m3d->numbone && m3d->numskin;
  std::size_t meshCount = m3d->nummaterial > 0 ? m3d->nummaterial : 1;
  std::vector<imported_mesh> meshes(meshCount);

  M3D_INDEX mi = M3D_INDEX(-2);
  int k = -1;
  std::size_t l = 0;
  for (std::size_t i = 0; i < m3d->numface; i++, l++) {
    const m3df_t &face = m3d->face[i];
    if (mi != face.materialid) {
      if (std::size_t(k + 1) >= meshes.size()) meshes.emplace_back();
      k++;
      mi = face.materialid;

      // raylib allocates vertex colours when a vertex of this material batch has a
      // zero colour (sic), or the batch has no material.
      bool vcolor = false;
      l = 0;
      for (std::size_t j = i; (j < m3d->numface) && (mi == m3d->face[j].materialid); j++, l++) {
        if (!m3d->vertex[m3d->face[j].vertex[0]].color || !m3d->vertex[m3d->face[j].vertex[1]].color ||
            !m3d->vertex[m3d->face[j].vertex[2]].color)
          vcolor = true;
      }

      imported_mesh &m = meshes[k];
      const std::size_t vertexCount = l * 3;
      m.positions.assign(vertexCount * 3, 0.0f);
      m.texcoords.assign(vertexCount * 2, 0.0f);
      m.normals.assign(vertexCount * 3, 0.0f);
      if ((mi == M3D_UNDEF) || vcolor) m.colors.assign(vertexCount * 4, 0);
      // If no map is provided and vertex colors are allocated, set them to white.
      if ((mi == M3D_UNDEF) && !m.colors.empty()) std::fill(m.colors.begin(), m.colors.end(), 255);
      if (skinned) {
        m.bone_ids.assign(vertexCount * 4, 0);
        m.bone_weights.assign(vertexCount * 4, 0.0f);
      }
      m.material = int(mi + 1);  // M3D_UNDEF wraps to 0, the default material
      l = 0;
    }

    imported_mesh &m = meshes[k];
    for (int n = 0; n < 3; n++) {
      const m3dv_t &v = m3d->vertex[face.vertex[n]];
      m.positions[l * 9 + n * 3 + 0] = v.x * m3d->scale;
      m.positions[l * 9 + n * 3 + 1] = v.y * m3d->scale;
      m.positions[l * 9 + n * 3 + 2] = v.z * m3d->scale;
    }

    // Without vertex color (full transparency), the default (zero) colour stays.
    if (!m.colors.empty()) {
      for (int n = 0; n < 3; n++) {
        const std::uint32_t c = m3d->vertex[face.vertex[n]].color;
        if (c & 0xff000000) std::memcpy(&m.colors[l * 12 + n * 4], &c, 4);
      }
    }

    if (face.texcoord[0] != M3D_UNDEF) {
      for (int n = 0; n < 3; n++) {
        m.texcoords[l * 6 + n * 2 + 0] = m3d->tmap[face.texcoord[n]].u;
        m.texcoords[l * 6 + n * 2 + 1] = 1.0f - m3d->tmap[face.texcoord[n]].v;
      }
    }

    if (face.normal[0] != M3D_UNDEF) {
      for (int n = 0; n < 3; n++) {
        m.normals[l * 9 + n * 3 + 0] = m3d->vertex[face.normal[n]].x;
        m.normals[l * 9 + n * 3 + 1] = m3d->vertex[face.normal[n]].y;
        m.normals[l * 9 + n * 3 + 2] = m3d->vertex[face.normal[n]].z;
      }
    }

    if (skinned) {
      for (int n = 0; n < 3; n++) {
        const int skinid = int(m3d->vertex[face.vertex[n]].skinid);
        if ((M3D_INDEX(skinid) != M3D_UNDEF) && (skinid < int(m3d->numskin))) {
          for (int j = 0; j < 4; j++) {
            m.bone_ids[l * 12 + n * 4 + j] = std::uint8_t(m3d->skin[skinid].boneid[j]);
            m.bone_weights[l * 12 + n * 4 + j] = m3d->skin[skinid].weight[j];
          }
        } else {
          // Vertices without a bone go to the special "no bone" bone.
          m.bone_ids[l * 12 + n * 4] = std::uint8_t(m3d->numbone);
          m.bone_weights[l * 12 + n * 4] = 1.0f;
        }
      }
    }
  }
  // raylib keeps nummaterial meshes even when fewer material batches exist; the
  // unused ones (always at the end) have no vertices and are left out here.
  meshes.resize(std::size_t(k + 1));
  out.meshes = std::move(meshes);

  // Materials: index 0 default, material i at i + 1.
  out.materials.resize(std::size_t(m3d->nummaterial) + 1);
  for (std::size_t i = 0; i < m3d->nummaterial; i++) {
    imported_material &mat = out.materials[i + 1];
    for (std::size_t j = 0; j < m3d->material[i].numprop; j++) {
      const m3dp_t &prop = m3d->material[i].prop[j];
      if (prop.type == m3dp_Kd)
        mat.color = color_from_bytes(prop.value.color);
      else if (prop.type == m3dp_map_Kd && prop.value.textureid < m3d->numtexture)
        texture_to_rgba(m3d->texture[prop.value.textureid], mat);
      // Ks, Ns, Ke, Pm, Pr, Ps and the other maps have no place in imported_material.
    }
  }

  // Bones, plus the special "NO BONE" bone at the end.
  if (m3d->numbone) {
    std::vector<xform> bind(std::size_t(m3d->numbone) + 1);
    out.bones.resize(std::size_t(m3d->numbone) + 1);
    std::size_t i = 0;
    for (; i < m3d->numbone; i++) {
      out.bones[i].parent = int(m3d->bone[i].parent);
      out.bones[i].name = m3d_name(m3d->bone[i].name);
      bind[i] = m3d_bone_xform(m3d, m3d->bone[i].pos, m3d->bone[i].ori);
      if (out.bones[i].parent >= 0 && out.bones[i].parent < int(bind.size()))
        m3d_to_model_space(bind[i], bind[out.bones[i].parent]);
    }
    out.bones[i].parent = -1;
    out.bones[i].name = "NO BONE";
    bind[i].translation = {0.0f, 0.0f, 0.0f};
    bind[i].rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    bind[i].scale = {1.0f, 1.0f, 1.0f};
    for (std::size_t b = 0; b < bind.size(); b++) {
      imported_bone t = to_bone(bind[b]);
      out.bones[b].translation = t.translation;
      out.bones[b].rotation = t.rotation;
      out.bones[b].scale = t.scale;
    }
  }
}

constexpr unsigned M3D_ANIMDELAY = 17;  // raylib's frame step (~1000 ms / 60 FPS)

void m3d_animations(m3d_t *m3d, imported_model &out) {
  // No animation or bones: none. Skins are not required.
  if (!m3d->numaction || !m3d->numbone) return;
  const std::size_t boneCount = std::size_t(m3d->numbone) + 1;
  std::vector<int> parents(boneCount, -1);
  for (std::size_t i = 0; i < m3d->numbone; i++) parents[i] = int(m3d->bone[i].parent);

  for (unsigned a = 0; a < m3d->numaction; a++) {
    imported_clip clip;
    clip.name = m3d_name(m3d->action[a].name);
    clip.fps = legacy_keyframe_rate;
    const unsigned keyframeCount = m3d->action[a].durationmsec / M3D_ANIMDELAY;
    // A pose m3d_pose cannot give stays zeroed, as raylib's RL_CALLOC left it.
    std::vector<std::vector<xform>> frames(keyframeCount, std::vector<xform>(boneCount));
    for (unsigned i = 0; i < keyframeCount; i++) {
      m3db_t *pose = m3d_pose(m3d, a, i * M3D_ANIMDELAY);
      if (pose == nullptr) continue;
      std::size_t j = 0;
      for (; j < m3d->numbone; j++) {
        frames[i][j] = m3d_bone_xform(m3d, pose[j].pos, pose[j].ori);
        if (parents[j] >= 0 && parents[j] < int(boneCount)) m3d_to_model_space(frames[i][j], frames[i][parents[j]]);
      }
      frames[i][j].translation = {0.0f, 0.0f, 0.0f};
      frames[i][j].rotation = {0.0f, 0.0f, 0.0f, 1.0f};
      frames[i][j].scale = {1.0f, 1.0f, 1.0f};
      std::free(pose);
    }
    clip.frames.resize(frames.size());
    for (std::size_t f = 0; f < frames.size(); f++) {
      clip.frames[f].reserve(boneCount);
      for (const xform &t : frames[f]) clip.frames[f].push_back(to_bone(t));
    }
    out.clips.push_back(std::move(clip));
  }
}

}  // namespace

bool import_iqm(const std::string &path, imported_model &out, std::string &error) {
  out = imported_model{};
  std::vector<unsigned char> data;
  if (!read_file(path, data, error)) return false;
  IQMHeader h{};
  if (!iqm_header(data, h, path, error)) return false;
  if (!iqm_model(data, h, path, out, error) || !iqm_animations(data, h, path, out, error)) {
    out = imported_model{};
    return false;
  }
  if (out.materials.empty()) out.materials.emplace_back();  // LoadModel's default material
  return true;
}

bool import_vox(const std::string &path, imported_model &out, std::string &error) {
  out = imported_model{};
  std::vector<unsigned char> data;
  if (!read_file(path, data, error)) return false;

  VoxArray3D voxarray;
  std::memset(&voxarray, 0, sizeof(voxarray));
  if (Vox_LoadFromMemory(data.data(), static_cast<unsigned int>(data.size()), &voxarray) != VOX_SUCCESS) {
    Vox_FreeArrays(&voxarray);
    error = "failed to load VOX data: " + path;
    return false;
  }

  const int nbvertices = voxarray.vertices.used;
  const int meshescount = 1 + (nbvertices / 65536);
  out.materials.assign(1, imported_material{});
  out.meshes.resize(std::size_t(meshescount));

  // 5461 voxels x 12 vertices per voxel -> 65532 (must be below 65536). The same
  // global index list is reused by every mesh (only its first triangleCount*3 used).
  int verticesRemain = nbvertices;
  const int verticesMax = 65532;
  const unsigned short *pindices = voxarray.indices.array;
  std::size_t first = 0;  // raylib advances its vertex, normal and colour pointers by verticesMax

  for (int i = 0; i < meshescount; i++) {
    imported_mesh &m = out.meshes[std::size_t(i)];
    const int vertexCount = int(std::fmin(verticesMax, verticesRemain));
    const int triangleCount = (vertexCount / 4) * 2;
    if (vertexCount > 0) {
      m.positions.resize(std::size_t(vertexCount) * 3);
      m.normals.resize(std::size_t(vertexCount) * 3);
      m.colors.resize(std::size_t(vertexCount) * 4);
      const VoxVector3 *pvertices = voxarray.vertices.array + first;
      const VoxVector3 *pnormals = voxarray.normals.array + first;
      const VoxColor *pcolors = voxarray.colors.array + first;
      for (int v = 0; v < vertexCount; v++) {
        m.positions[std::size_t(v) * 3 + 0] = pvertices[v].x;
        m.positions[std::size_t(v) * 3 + 1] = pvertices[v].y;
        m.positions[std::size_t(v) * 3 + 2] = pvertices[v].z;
        m.normals[std::size_t(v) * 3 + 0] = pnormals[v].x;
        m.normals[std::size_t(v) * 3 + 1] = pnormals[v].y;
        m.normals[std::size_t(v) * 3 + 2] = pnormals[v].z;
        m.colors[std::size_t(v) * 4 + 0] = pcolors[v].r;
        m.colors[std::size_t(v) * 4 + 1] = pcolors[v].g;
        m.colors[std::size_t(v) * 4 + 2] = pcolors[v].b;
        m.colors[std::size_t(v) * 4 + 3] = pcolors[v].a;
      }
      const int indexCount = std::min(triangleCount * 3, voxarray.indices.used);
      m.indices.assign(pindices, pindices + indexCount);
    }
    m.material = 0;
    verticesRemain -= verticesMax;
    first += verticesMax;
  }
  Vox_FreeArrays(&voxarray);
  return true;
}

bool import_m3d(const std::string &path, imported_model &out, std::string &error) {
  out = imported_model{};
  m3d_handle h;
  if (!m3d_open(path, h, error)) return false;
  m3d_model(h.m3d, out);
  m3d_animations(h.m3d, out);
  return true;
}

}  // namespace njin
