#include "render3d_gpu_timer.h"
#include <array>

// raylib does not wrap timer queries, so those few GL calls are loaded here,
// as render3d_cull.cpp loads its own.
#include "njin_gl_proc.h"

#if defined(_WIN32)
#define NJIN_GLAPI __stdcall
#else
#define NJIN_GLAPI
#endif

namespace njin {
namespace {
constexpr u32 gl_timestamp = 0x8E28;
constexpr u32 gl_query_result = 0x8866;
constexpr u32 gl_query_result_available = 0x8867;
constexpr i32 timestamps = gpu_timer_stages + 1;

struct gl_fns {
  void(NJIN_GLAPI *gen_queries)(i32, u32 *) = nullptr;
  void(NJIN_GLAPI *delete_queries)(i32, const u32 *) = nullptr;
  void(NJIN_GLAPI *query_counter)(u32, u32) = nullptr;
  void(NJIN_GLAPI *get_query_object_iv)(u32, u32, i32 *) = nullptr;
  void(NJIN_GLAPI *get_query_object_ui64v)(u32, u32, u64 *) = nullptr;
  bool loaded = false;
  bool ok = false;
};

template <typename T> void load(T &fn, const char *name) { fn = reinterpret_cast<T>(::njin::detail::gl_proc_address(name)); }

const gl_fns &gl() {
  static gl_fns g;
  if (!g.loaded) {
    g.loaded = true;
    load(g.gen_queries, "glGenQueries");
    load(g.delete_queries, "glDeleteQueries");
    load(g.query_counter, "glQueryCounter");
    load(g.get_query_object_iv, "glGetQueryObjectiv");
    load(g.get_query_object_ui64v, "glGetQueryObjectui64v");
    g.ok = g.gen_queries && g.delete_queries && g.query_counter && g.get_query_object_iv && g.get_query_object_ui64v;
  }
  return g;
}

// One pass's timestamps. A record is in flight from its first timestamp until
// its last one has a result; the ring holds a frame's passes several times
// over, so a record is reused only long after the GPU finished it.
struct record {
  std::array<u32, timestamps> query{};
  i32 written = 0; // timestamps issued so far
  bool in_flight = false;
};
constexpr usize ring_size = 64;

struct timer_state {
  std::array<record, ring_size> ring;
  usize next = 0;         // the record the next pass takes
  record *current = nullptr;
  bool made = false;
};

timer_state &st() {
  static timer_state s;
  return s;
}
} // namespace

bool gpu_timer_begin() {
  if (!gl().ok)
    return false;
  timer_state &s = st();
  if (!s.made) {
    for (record &r : s.ring)
      gl().gen_queries(timestamps, r.query.data());
    s.made = true;
  }
  record &r = s.ring[s.next];
  // The ring is full of passes the GPU has not finished: this one goes untimed.
  if (r.in_flight) {
    s.current = nullptr;
    return false;
  }
  s.next = (s.next + 1) % ring_size;
  r.in_flight = true;
  r.written = 0;
  gl().query_counter(r.query[0], gl_timestamp);
  r.written = 1;
  s.current = &r;
  return true;
}

void gpu_timer_stage(i32 index) {
  timer_state &s = st();
  record *r = s.current;
  if (r == nullptr || index < 0 || index >= gpu_timer_stages || r->written != index + 1)
    return;
  gl().query_counter(r->query[(usize)index + 1], gl_timestamp);
  r->written = index + 2;
}

void gpu_timer_collect(f32 *ms) {
  timer_state &s = st();
  s.current = nullptr;
  if (!s.made)
    return;
  for (record &r : s.ring) {
    if (!r.in_flight)
      continue;
    // A pass that stopped short of its last stage is dropped.
    if (r.written != timestamps) {
      r.in_flight = false;
      continue;
    }
    i32 ready = 0;
    gl().get_query_object_iv(r.query[timestamps - 1], gl_query_result_available, &ready);
    if (ready == 0)
      continue;
    u64 at[timestamps];
    for (i32 i = 0; i < timestamps; i++)
      gl().get_query_object_ui64v(r.query[(usize)i], gl_query_result, &at[i]);
    for (i32 i = 0; i < gpu_timer_stages; i++)
      ms[i] += at[i + 1] >= at[i] ? (f32)((double)(at[i + 1] - at[i]) * 1e-6) : 0.0f;
    r.in_flight = false;
  }
}

void gpu_timer_free() {
  timer_state &s = st();
  if (!s.made)
    return;
  for (record &r : s.ring)
    gl().delete_queries(timestamps, r.query.data());
  s = timer_state{};
}
} // namespace njin
