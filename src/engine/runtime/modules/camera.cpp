#include "camera.h"
#include "_comps.h"
#include "fx.h"
#include "gizmo.h"
#include "lighting.h"
#include "post_fx.h"
#include "render3d.h"
#include "njin2rl.h"
#include "njin_ctx.h"
#include "njin_camera.h"
#include "njin_ctx_impl.h"
#include "njin_cfg.h"
#include "njin_view.h"
#include "rl2njin.h"
#include <algorithm>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

namespace njin {
namespace {
// Identity view: world coordinates are screen pixels.
constexpr camera_view default_view{.zoom = 1.0f,
                                   .rotation = 0.0f,
                                   .offset = {0.0f, 0.0f},
                                   .target = {0.0f, 0.0f}};

Camera2D active_raylib_camera(const context &ctx) {
  Camera2D camera{};
  to_raylib(camera_active(ctx), camera);
  return camera;
}

// A render target whose depth is a texture, not a renderbuffer, so the depth
// of field (post_fx::dof) can read how far each pixel is. raylib's
// UnloadRenderTexture frees either kind.
RenderTexture2D load_target(i32 w, i32 h) {
  RenderTexture2D t{};
  t.id = rlLoadFramebuffer();
  if (t.id == 0)
    return t;
  t.texture = Texture2D{rlLoadTexture(nullptr, w, h, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1), w, h, 1,
                        PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
  t.depth = Texture2D{rlLoadTextureDepth(w, h, false), w, h, 1, 19};
  rlFramebufferAttach(t.id, t.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
  rlFramebufferAttach(t.id, t.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
  if (rlFramebufferComplete(t.id))
    return t;
  // A driver that cannot attach a depth texture still gets a world target.
  UnloadRenderTexture(t);
  t = LoadRenderTexture(w, h);
  t.depth.id = 0;
  return t;
}

// (Re)creates the post target when the window size changed. Sized by
// render_scale like view.target, and bound the same way, so a game's post_fx
// or its own post shader do not bake the world in at 1x before render_scale
// gets a chance to smooth it.
bool ensure_post_target(const context &ctx, camera_post &post) {
  const vec2 screen = screen_size(ctx);
  const i32 scale = ctx.view.render_scale;
  const i32 w = (i32)screen.x * scale;
  const i32 h = (i32)screen.y * scale;
  if (IsRenderTextureValid(post.target) && post.target.texture.width == w &&
      post.target.texture.height == h)
    return true;
  if (IsRenderTextureValid(post.target))
    UnloadRenderTexture(post.target);
  post.target = load_target(w, h);
  if (!IsRenderTextureValid(post.target))
    return false;
  SetTextureFilter(post.target.texture, scale > 1 ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
  return true;
}

void begin_world_space(context &ctx) {
  camera_post &post = ctx.post;
  const bool wanted = shader_slot_of(ctx.shader, post.shader) != nullptr ||
                      post_chain_active(ctx.postfx) || lighting_active(ctx.light);
  post.drawing = wanted && ensure_post_target(ctx, post);
  if (post.drawing) {
    bind_view_target(post.target, screen_size(ctx));
    Color clear{};
    to_raylib(ctx.cfg.clear_bg_color, clear);
    ClearBackground(clear);
  }
  // The shake only moves what is drawn; camera_active, w2scr and scr2w keep
  // answering for the steady camera.
  Camera2D camera = active_raylib_camera(ctx);
  fx_apply_shake(ctx, camera);
  post.world_camera = camera;
  BeginMode2D(camera);
  ctx.view.world_depth++;
}

void finish_world_post(context &ctx);

void end_world_space(context &ctx) {
  render3d_close(ctx);
  EndMode2D();
  ctx.view.world_depth = std::max(0, ctx.view.world_depth - 1);
  finish_world_post(ctx);
  // The world is done and on the virtual image. With smooth UI, the rest of the
  // frame (the screen-space phase and what follows) is drawn on the window.
  view_ui_begin(ctx.view);
  // Debug gizmos over the finished world, under the game's UI.
  gizmo_draw_screen(ctx);
}

void finish_world_post(context &ctx) {
  camera_post &post = ctx.post;
  if (!post.drawing)
    return;
  post.drawing = false;
  EndTextureMode();
  // Built-in effects first; the game's own shader sees their result. Every
  // texture mode ends on the window, so go back to the virtual screen after.
  const Texture2D &lit = lighting_apply(ctx, post.world_camera, post.target.texture);
  // The depth of the 3D drawn this frame, for the depth of field.
  post_depth depth{};
  if (ctx.render3d.depth_drawn && post.target.depth.id != 0)
    depth = {post.target.depth.id, ctx.render3d.depth_near, ctx.render3d.depth_far,
             ctx.render3d.depth_inv_view_proj};
  ctx.render3d.depth_drawn = false;
  const Texture2D &texture = post_chain_run(ctx, lit, depth);
  view_rebind(ctx.view);
  // Framebuffers are stored bottom-up: a negative source height flips it. The
  // dest is the logical size, not the texture's own (render_scale times
  // bigger) pixel size: DrawTextureRec draws 1:1 by texture pixel, which would
  // draw render_scale times too big without an explicit dest rect.
  const vec2 screen = screen_size(ctx);
  const Rectangle source{0.0f, 0.0f, (f32)texture.width, -(f32)texture.height};
  const Rectangle dest{0.0f, 0.0f, screen.x, screen.y};
  const shader_slot *slot = shader_slot_of(ctx.shader, post.shader);
  if (slot != nullptr) {
    BeginShaderMode(slot->shader);
    shader_bind_textures(ctx, *slot);
  }
  // The finished world replaces what is under it: copied, not blended by its
  // alpha, which half-transparent shapes leave below 1 (see view_draw_end).
  rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
  BeginBlendMode(BLEND_CUSTOM);
  DrawTexturePro(texture, source, dest, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
  EndBlendMode();
  if (slot != nullptr)
    EndShaderMode();
}

void setup(context &ctx) {
  ecs_register(ctx, phase_pre_render, begin_world_space, "begin_world_space");
  ecs_register(ctx, phase_post_render, end_world_space, "end_world_space");
}
} // namespace

void world_target_rebind(context &ctx) {
  if (ctx.post.drawing) {
    bind_view_target(ctx.post.target, screen_size(ctx));
  } else if (ctx.view.drawing) {
    bind_view_target(ctx.view.target, view_logical_size(ctx.view));
  } else {
    // The window, as BeginDrawing leaves it.
    rlDrawRenderBatchActive();
    rlDisableFramebuffer();
    const i32 w = GetRenderWidth();
    const i32 h = GetRenderHeight();
    rlViewport(0, 0, w, h);
    rlSetFramebufferWidth(w);
    rlSetFramebufferHeight(h);
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    rlOrtho(0, w, h, 0, 0.0f, 1.0f);
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
  }
  rlMultMatrixf(MatrixToFloat(GetCameraMatrix2D(ctx.post.world_camera)));
}

mod_desc camera_module() {
  return mod_desc{.name = "njin.camera", .setup = setup};
}

camera_post::~camera_post() {
  if (IsRenderTextureValid(target))
    UnloadRenderTexture(target);
}

void camera_set_post_shader(context &ctx, shader_handle shader) {
  ctx.post.shader = shader;
}

rect camera_bounds(const context &ctx) {
  const Camera2D camera = active_raylib_camera(ctx);
  const vec2 screen = screen_size(ctx);
  const f32 w = screen.x;
  const f32 h = screen.y;
  const Vector2 corners[4] = {{0.0f, 0.0f}, {w, 0.0f}, {0.0f, h}, {w, h}};
  // With a rotated camera the visible area is a rotated rectangle; its
  // bounding box is what callers culling against it want.
  Vector2 lo = GetScreenToWorld2D(corners[0], camera);
  Vector2 hi = lo;
  for (const Vector2 &corner : corners) {
    const Vector2 p = GetScreenToWorld2D(corner, camera);
    lo.x = p.x < lo.x ? p.x : lo.x;
    lo.y = p.y < lo.y ? p.y : lo.y;
    hi.x = p.x > hi.x ? p.x : hi.x;
    hi.y = p.y > hi.y ? p.y : hi.y;
  }
  return rect{{lo.x, lo.y}, {hi.x - lo.x, hi.y - lo.y}};
}

camera_view camera_active(const context &ctx) {
  const auto cameras =
      ctx.ecs.registry
          .view<const camera_on, const camera_2d, const transform>();
  for (const entt::entity entity : cameras) {
    const camera_2d &cam = cameras.get<const camera_2d>(entity);
    const transform &tr = cameras.get<const transform>(entity);
    return camera_view{.zoom = cam.zoom > 0.0f ? cam.zoom : 1.0f,
                       .rotation = tr.rot,
                       .offset = cam.offset,
                       .target = tr.pos};
  }
  return default_view;
}

vec2 w2scr(const context &ctx, vec2 pos) {
  Vector2 point{};
  to_raylib(pos, point);
  vec2 result{};
  from_raylib(GetWorldToScreen2D(point, active_raylib_camera(ctx)), result);
  return result;
}

vec2 scr2w(const context &ctx, vec2 pos) {
  Vector2 point{};
  to_raylib(pos, point);
  vec2 result{};
  from_raylib(GetScreenToWorld2D(point, active_raylib_camera(ctx)), result);
  return result;
}
} // namespace njin
