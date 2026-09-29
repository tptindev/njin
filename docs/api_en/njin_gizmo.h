#pragma once
#include "_math.h"

namespace njin {
struct njin_ctx;

/// @addtogroup grp_debug
/// @{

/// @name Gizmos
/// Shapes drawn for debugging: lines, arrows, boxes, in 2D or 3D world space.
///
/// Callable in any phase (both `phase_update` and `phase_fixed_update`), no
/// need to be inside a draw call: the engine collects them and draws them on
/// top of the world (never hidden by anything), thin 1-pixel lines, text at a
/// fixed screen size. A 2D gizmo draws through the 2D camera; a 3D gizmo
/// draws at end_3d() through that draw's camera (with no begin_3d(), it does
/// not show). While njin_inspector is connected, gizmos also show in its
/// World panel.
///
/// `duration` is how many seconds the gizmo still shows: 0 (the default) is
/// just this frame, so call it again every frame; higher to keep a trail
/// (a bullet path, a collision point).
///
/// @code
/// njin::gizmo_arrow(ctx, pos, pos + velocity * 0.2f, njin::colors::yellow);
/// njin::gizmo_text(ctx, pos + njin::vec2{0, -20}, "jumping");
/// njin::gizmo_box3d(ctx, enemy_pos, {1, 2, 1}, njin::colors::red);
/// njin::gizmo_line3d(ctx, muzzle, hit, njin::colors::yellow, 1.0f); // keeps it for 1 second
/// @endcode
/// @{

/// 2D line segment.
/// @param ctx Engine context.
/// @param a Start point, world coordinates.
/// @param b End point.
/// @param color Colour.
/// @param duration Seconds still shown. 0 is just this frame.
void gizmo_line(njin_ctx &ctx, vec2 a, vec2 b, rgba color = colors::green, f32 duration = 0.0f);

/// 2D arrow from `from` to `to`.
/// @param ctx Engine context.
/// @param from Base.
/// @param to Tip.
/// @param color Colour.
/// @param duration Seconds still shown. 0 is just this frame.
void gizmo_arrow(njin_ctx &ctx, vec2 from, vec2 to, rgba color = colors::green, f32 duration = 0.0f);

/// 2D rectangle outline.
/// @param ctx Engine context.
/// @param r Rectangle, world coordinates.
/// @param color Colour.
/// @param duration Seconds still shown. 0 is just this frame.
void gizmo_rect(njin_ctx &ctx, rect r, rgba color = colors::green, f32 duration = 0.0f);

/// 2D circle outline.
/// @param ctx Engine context.
/// @param center Centre.
/// @param radius Radius.
/// @param color Colour.
/// @param duration Seconds still shown. 0 is just this frame.
void gizmo_circle(njin_ctx &ctx, vec2 center, f32 radius, rgba color = colors::green, f32 duration = 0.0f);

/// A 2D point: a small cross, fixed size on screen.
/// @param ctx Engine context.
/// @param p Position.
/// @param color Colour.
/// @param duration Seconds still shown. 0 is just this frame.
void gizmo_point(njin_ctx &ctx, vec2 p, rgba color = colors::green, f32 duration = 0.0f);

/// A text label at a 2D world point, fixed size on screen.
/// @param ctx Engine context.
/// @param pos Position of the text's top-left corner.
/// @param text Text (UTF-8). nullptr is ignored.
/// @param color Colour.
/// @param duration Seconds still shown. 0 is just this frame.
void gizmo_text(njin_ctx &ctx, vec2 pos, const char *text, rgba color = colors::white, f32 duration = 0.0f);

/// 3D line segment.
/// @param ctx Engine context.
/// @param a Start point.
/// @param b End point.
/// @param color Colour.
/// @param duration Seconds still shown. 0 is just this frame.
void gizmo_line3d(njin_ctx &ctx, vec3 a, vec3 b, rgba color = colors::green, f32 duration = 0.0f);

/// 3D arrow from `from` to `to`.
/// @param ctx Engine context.
/// @param from Base.
/// @param to Tip.
/// @param color Colour.
/// @param duration Seconds still shown. 0 is just this frame.
void gizmo_arrow3d(njin_ctx &ctx, vec3 from, vec3 to, rgba color = colors::green, f32 duration = 0.0f);

/// 3D box outline, edges parallel to the axes.
/// @param ctx Engine context.
/// @param center Centre.
/// @param size Size along x, y, z.
/// @param color Colour.
/// @param duration Seconds still shown. 0 is just this frame.
void gizmo_box3d(njin_ctx &ctx, vec3 center, vec3 size, rgba color = colors::green, f32 duration = 0.0f);

/// 3D sphere outline: three great circles.
/// @param ctx Engine context.
/// @param center Centre.
/// @param radius Radius.
/// @param color Colour.
/// @param duration Seconds still shown. 0 is just this frame.
void gizmo_sphere3d(njin_ctx &ctx, vec3 center, f32 radius, rgba color = colors::green, f32 duration = 0.0f);

/// Three coordinate axes at `pos`: x red, y green, z blue.
/// @param ctx Engine context.
/// @param pos Origin.
/// @param size Length of each axis.
/// @param duration Seconds still shown. 0 is just this frame.
void gizmo_axes3d(njin_ctx &ctx, vec3 pos, f32 size = 1.0f, f32 duration = 0.0f);

/// A 3D point: a small cross, fixed size on screen.
/// @param ctx Engine context.
/// @param p Position.
/// @param color Colour.
/// @param duration Seconds still shown. 0 is just this frame.
void gizmo_point3d(njin_ctx &ctx, vec3 p, rgba color = colors::green, f32 duration = 0.0f);

/// A text label attached to a 3D point, fixed size on screen. Does not show
/// when the point is behind the camera.
/// @param ctx Engine context.
/// @param pos Position.
/// @param text Text (UTF-8). nullptr is ignored.
/// @param color Colour.
/// @param duration Seconds still shown. 0 is just this frame.
void gizmo_text3d(njin_ctx &ctx, vec3 pos, const char *text, rgba color = colors::white, f32 duration = 0.0f);

/// Turns gizmo drawing on or off. On by default. When off, `gizmo_*` calls
/// cost almost nothing; use it for a debug toggle key, or to turn them off in
/// a release build.
/// @param ctx Engine context.
/// @param visible `true` to draw them.
void gizmos_set_visible(njin_ctx &ctx, bool visible);

/// Whether gizmos are being drawn.
/// @param ctx Engine context.
/// @return The value set by gizmos_set_visible().
bool gizmos_visible(const njin_ctx &ctx);
/// @}
/// @}
} // namespace njin
