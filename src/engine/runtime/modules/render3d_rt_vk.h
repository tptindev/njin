#pragma once
#include "../njin_internal_only.h"
#include <cstddef>
#include <cstdint>

// The Vulkan side of ray tracing (light3d::ray_traced_shadows), with NVRHI.
// Kept apart from raylib: it includes windows.h, which clashes with raylib.h,
// so render3d_rt.cpp talks to it through these plain types.
//
// One Vulkan device with ray queries beside raylib's OpenGL context, on the
// same GPU. The images both sides use (the pass's depth, the shadow mask) are
// allocated by Vulkan and imported into OpenGL (GL_EXT_memory_object_win32);
// two semaphores, also shared (GL_EXT_semaphore_win32), order the work: GL
// signals when the depth is written, Vulkan waits, traces, signals, and GL
// waits before it samples the mask. Without the semaphore extension the two
// sides wait for each other whole (glFinish, waitForIdle).
namespace njin::rtvk {
struct mesh_in {
  const float *pos = nullptr; // xyz per vertex
  uint32_t vertex_count = 0;
  const uint16_t *idx16 = nullptr; // one of the two, or none (a triangle list)
  const uint32_t *idx32 = nullptr;
  uint32_t index_count = 0;
};

// One instance of a mesh in the scene: its BLAS (from mesh()), transform
// (three rows of a 3 x 4 matrix), and how what a ray meets on it is lit: its
// colour and glow, in linear light.
struct instance_in {
  uint64_t blas = 0;
  float m[12]{};
  float albedo[4]{};
  float emission[4]{};
  bool two_sided = false; // drawn from both sides: its back is no inside
};

// What a trace needs from the pass.
struct trace_in {
  int width = 0, height = 0;
  float inv_view_proj[16]{}; // column-major, as raylib's Matrix
  float eye[3]{};
  float sun[4]{};          // direction towards the sun, tan of its angular radius; all 0 for no sun
  int rays = 4;
  float lamps[3][4]{};     // position, size (the light's radius)
  int lamp_count = 0;
  int blur = 3;            // denoise radius, pixels
  uint32_t frame = 0;
  // What to trace: the shadows into the mask, the reflections, the grid.
  bool shadows = true, sun_shadows = true, reflect = false, gi = false;
  // How what the rays meet is lit (linear light): the sun (w 1 when it lights),
  // the ambient light (w 1 when env_sh holds the environment, its rotation in
  // env_sh[0][3]), up to eight lamps (position and reach, colour and cos of
  // the inner cone, direction and cos of the outer cone, -2 for a point light).
  float sun_color[4]{};
  float ambient[4]{};
  float env_sh[9][4]{};
  float light_pos[8][4]{}, light_color[8][4]{}, light_spot[8][4]{};
  int light_count = 0;
  // The irradiance grid (gi): its corner and the share kept from last frame,
  // size and intensity, points along x, y, z and rays per point.
  float grid_min[4]{}, grid_size[4]{}, grid_count[4]{};
  bool grid_lights = false; // what rays meet takes its ambient light from the grid
};

// Starts Vulkan with ray queries and the GL interop; false (logged once) when
// the GPU, the driver or the GL context cannot. Call with raylib's GL context current.
bool init();
bool ready();
void shutdown();

// The bottom-level structure of a mesh, kept under `key` until `version`
// changes or it goes unused for a few frames; 0 on failure.
uint64_t mesh(uint64_t key, uint32_t version, const mesh_in &m);

// The shared images for a pass of w x h: the depth OpenGL writes (R32F) and the
// shadow mask it reads (RGBA8), as GL texture names. Remade when the size changes.
bool targets(int w, int h, unsigned &gl_depth, unsigned &gl_mask);

// The reflections' image for a pass of w x h (RGBA16F: the light a mirror
// ray met, alpha 1 where it met something), as a GL texture name.
bool reflect_target(int w, int h, unsigned &gl_refl);

// The irradiance grid of `points` points (RGBA32F, a column per point, a row
// per axis +x -x +y -y +z -z, as render3d's irrTex) and its depths (RGBA32F,
// a 16 x 16 octahedral tile per point: mean distance and its square, as
// render3d's irrDepth), as GL texture names; cleared when they are made.
// (The depth tiles are 16 x 16.)
// `fresh` is set when they were made now.
bool grid_target(int points, unsigned &gl_grid, unsigned &gl_depth, bool &fresh);
// Clears the grid at the next trace (it was set again).
void grid_reset();

// Builds the scene from `inst`, traces the shadows into the mask and hands it
// back to OpenGL; call after the depth was drawn. False on failure.
bool trace(const instance_in *inst, uint32_t count, const trace_in &t);

// Reads `bytes` of OpenGL buffer `buffer` back (an instance buffer uploaded
// before ray tracing asked for copies). False when it cannot.
bool gl_read_buffer(unsigned buffer, size_t bytes, void *out);

// Drops the meshes not used since `frames` traces ago.
void collect(uint32_t frames);
} // namespace njin::rtvk
