#include "gizmo.h"
#include "njin2rl.h"
#include "njin_camera.h"
#include "njin_cfg.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_draw_shapes.h"
#include "njin_draw_text.h"
#include "njin_view.h"
#include "njin_window.h"
#include <algorithm>
#include <cmath>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

namespace njin {
namespace {
constexpr i32 circle_segments = 32;
// Enough for any debugging session; a runaway loop adding gizmos stops here.
constexpr usize max_gizmos = 65536;

Color rl(rgba c) {
  Color out{};
  to_raylib(c, out);
  return out;
}

gizmo_state *live(context &ctx) {
  gizmo_state &g = ctx_ref<gizmo_state>(ctx);
  return g.visible ? &g : nullptr;
}

void add_line(gizmo_state &g, vec2 a, vec2 b, rgba color, f32 duration) {
  if (g.lines.size() < max_gizmos)
    g.lines.push_back({a, b, color, std::max(duration, 0.0f)});
}

void add_line3d(gizmo_state &g, vec3 a, vec3 b, rgba color, f32 duration) {
  if (g.lines3d.size() < max_gizmos)
    g.lines3d.push_back({a, b, color, std::max(duration, 0.0f)});
}

// Two unit vectors at right angles to `d` and to each other.
void basis(vec3 d, vec3 &u, vec3 &v) {
  const vec3 helper = std::fabs(d.y) < 0.99f ? vec3{0.0f, 1.0f, 0.0f} : vec3{1.0f, 0.0f, 0.0f};
  u = normalize(cross(helper, d));
  v = cross(d, u);
}

template <typename T> void age(std::vector<T> &items, f32 dt) {
  for (T &i : items)
    i.left -= dt;
  std::erase_if(items, [](const T &i) { return i.left < 0.0f; });
}

// A cross of fixed screen size, then the label to its right.
void draw_mark(const context &ctx, vec2 p, const std::string &text, rgba color) {
  if (text.empty()) {
    draw_line(ctx, p + vec2{-4.0f, -4.0f}, p + vec2{4.0f, 4.0f}, 1.5f, color);
    draw_line(ctx, p + vec2{-4.0f, 4.0f}, p + vec2{4.0f, -4.0f}, 1.5f, color);
    return;
  }
  // A dark copy behind, so the label reads over any background.
  draw_text(ctx, text.c_str(), p + vec2{1.0f, 1.0f}, 16.0f, {0.0f, 0.0f, 0.0f, 0.7f * color.a});
  draw_text(ctx, text.c_str(), p, 16.0f, color);
}
void portable_gizmos(context &ctx,gizmo_state &g,const camera3d &camera) {
  if(g.lines3d.empty()&&g.marks3d.empty()) return;
  auto &r=ctx_ref<window_guard>(ctx).renderer_resources;const auto &view=ctx_ref<view_state>(ctx);
  auto size=window_size(ctx);
  if(r.active_target) {const auto &target=r.targets.at(r.active_target.id);size={float(target.width),float(target.height)};}
  if(size.x<=0||size.y<=0) return;
  const auto eye=camera.position,target=camera.target,up=camera.up;
  const auto view_matrix=MatrixLookAt({eye.x,eye.y,eye.z},{target.x,target.y,target.z},{up.x,up.y,up.z});
  const auto projection=camera.projection==camera3d_orthographic?
    MatrixOrtho(-camera.ortho_height*.5f*size.x/size.y,camera.ortho_height*.5f*size.x/size.y,-camera.ortho_height*.5f,camera.ortho_height*.5f,camera.near_plane,camera.far_plane):
    MatrixPerspective(camera.fovy*PI/180,size.x/size.y,camera.near_plane,camera.far_plane);
  const auto matrix=MatrixMultiply(view_matrix,projection);
  const auto clip=[&](vec3 p) {return std::array<float,4>{matrix.m0*p.x+matrix.m4*p.y+matrix.m8*p.z+matrix.m12,
    matrix.m1*p.x+matrix.m5*p.y+matrix.m9*p.z+matrix.m13,matrix.m2*p.x+matrix.m6*p.y+matrix.m10*p.z+matrix.m14,
    matrix.m3*p.x+matrix.m7*p.y+matrix.m11*p.z+matrix.m15};};
  std::vector<RenderVertex> vertices;vertices.reserve(g.lines3d.size()*6);
  for(const auto &line:g.lines3d) {
    const auto a=clip(line.a),b=clip(line.b);float first=0,last=1;bool visible=true;
    for(unsigned plane=0;plane<6;++plane) {
      const unsigned axis=plane/2;const float sign=plane%2?1.f:-1.f;
      const float da=a[3]+sign*a[axis],db=b[3]+sign*b[axis];
      if(da<0&&db<0) {visible=false;break;}
      if(da<0) first=std::max(first,da/(da-db));else if(db<0) last=std::min(last,da/(da-db));
    }
    if(!visible||first>last) continue;
    const auto point=[&](float t) {const float w=a[3]+(b[3]-a[3])*t;return std::array<float,3>{a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t,w};};
    const auto p=point(first),q=point(last);if(p[2]<=1e-6f||q[2]<=1e-6f) continue;
    const vec2 from{p[0]/p[2],p[1]/p[2]},to{q[0]/q[2],q[1]/q[2]};const auto delta=(to-from)*size;
    const float distance=length(delta);if(distance<1e-6f) continue;
    const vec2 offset{-delta.y/distance/size.x,delta.x/distance/size.y};
    const vec2 points[]{from-offset,to-offset,to+offset,from-offset,to+offset,from+offset};
    const auto c=line.color;for(auto v:points) vertices.push_back({v.x,v.y,0,.5f,.5f,1,{c.r,c.g,c.b,c.a}});
  }
  if(!vertices.empty()) {
    auto &white=r.draw_2d.white;if(!white) {const unsigned char pixel[]{255,255,255,255};white=renderer_create_texture(ctx,{1,1,pixel});}
    MeshDraw draw;draw.vertices=vertices;draw.texture=white;draw.depth_test=false;renderer_draw_mesh(ctx,draw);
  }
  const bool offscreen=r.active_target&&(!view->drawing||r.active_target.id!=view->portable_target.id);
  const auto logical=offscreen?size:screen_size(ctx);
  for(const auto &mark:g.marks3d) {
    const auto p=clip(mark.pos);if(p[3]<=1e-6f||p[2]<-p[3]||p[2]>p[3]) continue;
    const vec2 at{(p[0]/p[3]*.5f+.5f)*logical.x,(.5f-p[1]/p[3]*.5f)*logical.y};
    if(offscreen) draw_mark(ctx,at,mark.text,mark.color);
    else g.projected.push_back({at,mark.text,mark.color,0});
  }
}

} // namespace

void gizmo_frame_begin(context &ctx) {
  gizmo_state &g = ctx_ref<gizmo_state>(ctx);
  const f32 dt = ctx_ref<time_state>(ctx).dt_real;
  age(g.lines, dt);
  age(g.marks, dt);
  age(g.lines3d, dt);
  age(g.marks3d, dt);
  g.projected.clear();
}

void gizmo_draw_3d(context &ctx, const camera3d &view) {
  gizmo_state &g = ctx_ref<gizmo_state>(ctx);
  if (!g.visible)
    return;
  if(ctx_ref<config>(ctx).graphics_backend!=GraphicsBackend::OpenGL) {portable_gizmos(ctx,g,view);return;}
  if (!g.lines3d.empty()) {
    // On top of the pass: no depth test, and nothing written to depth.
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    for (const gizmo_line3d_item &l : g.lines3d)
      DrawLine3D(Vector3{l.a.x, l.a.y, l.a.z}, Vector3{l.b.x, l.b.y, l.b.z}, rl(l.color));
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
  }
  if (g.marks3d.empty())
    return;
  Camera3D camera{};
  to_raylib(view.position, camera.position);
  to_raylib(view.target, camera.target);
  to_raylib(view.up, camera.up);
  const bool ortho = view.projection == camera3d_orthographic;
  camera.fovy = ortho ? view.ortho_height : view.fovy;
  camera.projection = ortho ? CAMERA_ORTHOGRAPHIC : CAMERA_PERSPECTIVE;
  const vec2 screen = screen_size(ctx);
  const vec3 forward = view.target - view.position;
  for (const gizmo_mark3d &m : g.marks3d) {
    if (dot(m.pos - view.position, forward) <= 0.0f)
      continue; // behind the camera
    Vector3 p{};
    to_raylib(m.pos, p);
    const Vector2 s = GetWorldToScreenEx(p, camera, (int)screen.x, (int)screen.y);
    g.projected.push_back({{s.x, s.y}, m.text, m.color, 0.0f});
  }
}

void gizmo_draw_screen(context &ctx) {
  const gizmo_state &g = ctx_ref<gizmo_state>(ctx);
  if (!g.visible)
    return;
  // The 2D lines too: in screen space they stay one pixel at any zoom, and
  // the world's post effects do not blur them.
  for (const gizmo_line2d &l : g.lines)
    draw_line(ctx, w2scr(ctx, l.a), w2scr(ctx, l.b), 1.0f, l.color);
  for (const gizmo_mark2d &m : g.marks)
    draw_mark(ctx, w2scr(ctx, m.pos), m.text, m.color);
  for (const gizmo_mark2d &m : g.projected)
    draw_mark(ctx, m.pos, m.text, m.color);
}

void gizmo_line(context &ctx, vec2 a, vec2 b, rgba color, f32 duration) {
  if (gizmo_state *g = live(ctx))
    add_line(*g, a, b, color, duration);
}

void gizmo_arrow(context &ctx, vec2 from, vec2 to, rgba color, f32 duration) {
  gizmo_state *g = live(ctx);
  if (g == nullptr)
    return;
  add_line(*g, from, to, color, duration);
  const f32 len = length(to - from);
  if (len <= 0.0f)
    return;
  const vec2 back = (from - to) / len * std::min(len * 0.25f, 12.0f);
  add_line(*g, to, to + rotate(back, 25.0f), color, duration);
  add_line(*g, to, to + rotate(back, -25.0f), color, duration);
}

void gizmo_rect(context &ctx, rect r, rgba color, f32 duration) {
  gizmo_state *g = live(ctx);
  if (g == nullptr)
    return;
  const vec2 a = r.pos, b = r.pos + vec2{r.size.x, 0.0f}, c = r.pos + r.size, d = r.pos + vec2{0.0f, r.size.y};
  add_line(*g, a, b, color, duration);
  add_line(*g, b, c, color, duration);
  add_line(*g, c, d, color, duration);
  add_line(*g, d, a, color, duration);
}

void gizmo_circle(context &ctx, vec2 center, f32 radius, rgba color, f32 duration) {
  gizmo_state *g = live(ctx);
  if (g == nullptr)
    return;
  for (i32 i = 0; i < circle_segments; i++) {
    const f32 a0 = 360.0f * (f32)i / circle_segments, a1 = 360.0f * (f32)(i + 1) / circle_segments;
    add_line(*g, center + from_angle(a0) * radius, center + from_angle(a1) * radius, color, duration);
  }
}

void gizmo_point(context &ctx, vec2 p, rgba color, f32 duration) {
  gizmo_state *g = live(ctx);
  if (g != nullptr && g->marks.size() < max_gizmos)
    g->marks.push_back({p, {}, color, std::max(duration, 0.0f)});
}

void gizmo_text(context &ctx, vec2 pos, const char *text, rgba color, f32 duration) {
  gizmo_state *g = live(ctx);
  if (g != nullptr && text != nullptr && *text != '\0' && g->marks.size() < max_gizmos)
    g->marks.push_back({pos, text, color, std::max(duration, 0.0f)});
}

void gizmo_line3d(context &ctx, vec3 a, vec3 b, rgba color, f32 duration) {
  if (gizmo_state *g = live(ctx))
    add_line3d(*g, a, b, color, duration);
}

void gizmo_arrow3d(context &ctx, vec3 from, vec3 to, rgba color, f32 duration) {
  gizmo_state *g = live(ctx);
  if (g == nullptr)
    return;
  add_line3d(*g, from, to, color, duration);
  const f32 len = distance(from, to);
  if (len <= 0.0f)
    return;
  const vec3 d = (to - from) / len;
  vec3 u, v;
  basis(d, u, v);
  const f32 head = len * 0.2f;
  for (vec3 side : {u, -u, v, -v})
    add_line3d(*g, to, to - d * head + side * (head * 0.4f), color, duration);
}

void gizmo_box3d(context &ctx, vec3 center, vec3 size, rgba color, f32 duration) {
  gizmo_state *g = live(ctx);
  if (g == nullptr)
    return;
  const vec3 h = size * 0.5f;
  const auto corner = [&](i32 i) {
    return center + vec3{i & 1 ? h.x : -h.x, i & 2 ? h.y : -h.y, i & 4 ? h.z : -h.z};
  };
  // The 12 edges: corners that differ in one bit.
  for (i32 i = 0; i < 8; i++) {
    for (i32 bit : {1, 2, 4}) {
      if ((i & bit) == 0)
        add_line3d(*g, corner(i), corner(i | bit), color, duration);
    }
  }
}

void gizmo_sphere3d(context &ctx, vec3 center, f32 radius, rgba color, f32 duration) {
  gizmo_state *g = live(ctx);
  if (g == nullptr)
    return;
  const vec3 axes[3][2] = {{{1, 0, 0}, {0, 1, 0}}, {{0, 1, 0}, {0, 0, 1}}, {{1, 0, 0}, {0, 0, 1}}};
  for (const auto &plane : axes) {
    for (i32 i = 0; i < circle_segments; i++) {
      const f32 a0 = 2.0f * pi * (f32)i / circle_segments, a1 = 2.0f * pi * (f32)(i + 1) / circle_segments;
      const vec3 p0 = center + (plane[0] * std::cos(a0) + plane[1] * std::sin(a0)) * radius;
      const vec3 p1 = center + (plane[0] * std::cos(a1) + plane[1] * std::sin(a1)) * radius;
      add_line3d(*g, p0, p1, color, duration);
    }
  }
}

void gizmo_axes3d(context &ctx, vec3 pos, f32 size, f32 duration) {
  gizmo_arrow3d(ctx, pos, pos + vec3{size, 0.0f, 0.0f}, colors::red, duration);
  gizmo_arrow3d(ctx, pos, pos + vec3{0.0f, size, 0.0f}, colors::green, duration);
  gizmo_arrow3d(ctx, pos, pos + vec3{0.0f, 0.0f, size}, colors::blue, duration);
}

void gizmo_point3d(context &ctx, vec3 p, rgba color, f32 duration) {
  gizmo_state *g = live(ctx);
  if (g != nullptr && g->marks3d.size() < max_gizmos)
    g->marks3d.push_back({p, {}, color, std::max(duration, 0.0f)});
}

void gizmo_text3d(context &ctx, vec3 pos, const char *text, rgba color, f32 duration) {
  gizmo_state *g = live(ctx);
  if (g != nullptr && text != nullptr && *text != '\0' && g->marks3d.size() < max_gizmos)
    g->marks3d.push_back({pos, text, color, std::max(duration, 0.0f)});
}

void gizmos_set_visible(context &ctx, bool visible) { ctx_ref<gizmo_state>(ctx)->visible = visible; }

bool gizmos_visible(const context &ctx) { return ctx_ref<gizmo_state>(ctx)->visible; }
} // namespace njin
