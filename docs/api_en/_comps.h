#pragma once

#include "_math.h"
#include "_types.h"
#include <entt/entity/entity.hpp>
namespace njin {
/// @addtogroup grp_comps
/// @{

/// Position, rotation and scale of an entity in the world.
///
/// The camera also reads the transform of the entity that carries it (see camera_2d).
/// Sprites use all three fields when drawing.
struct transform {
  vec2 pos{};       ///< Position in the world.
  f32 rot = 0.0f;   ///< Rotation in degrees, clockwise.
  f32 scale = 1.0f; ///< Scale. 1 is the original size.
};

/// 2D camera. Needs a transform on the same entity: `transform.pos` is the point
/// in the world the camera looks at, `transform.rot` is its rotation.
/// `transform.scale` is ignored.
///
/// The camera only has an effect when that entity also carries camera_on.
struct camera_2d {
  /// Position on the screen (in pixels) where `transform.pos` is drawn. Use half
  /// the screen size so the camera always stays centered on the target.
  vec2 offset{};
  /// 1 is no magnification, 2 makes everything twice as big. A value <= 0 is treated as 1.
  f32 zoom = 1.0f;
};

/// Tag marking the camera in use for drawing and for w2scr()/scr2w().
///
/// If several entities have this tag, the first one found is used. If no entity
/// has it, the world coincides with the screen.
struct camera_on {};

/// Image drawn at the entity's position. Needs a transform on the same entity.
///
/// The engine's sprite module draws every entity that has a transform and a sprite
/// in `phase_render`, in increasing `layer` order. The game's draw systems in
/// `phase_render` run afterwards, so they draw over sprites.
struct sprite {
  texture_handle texture{}; ///< Image to draw.
  /// Region of the image, in pixels. A size of 0 means the whole image. sprite_anim
  /// overwrites this field every frame.
  rect source{};
  /// Anchor point, as a fraction of the size: `{0, 0}` is the top-left corner, `{0.5,
  /// 0.5}` is the center. `transform.pos` lands exactly on this point, and the image rotates around it.
  vec2 origin{0.5f, 0.5f};
  rgba tint{1.0f, 1.0f, 1.0f, 1.0f}; ///< Color multiplied into the image.
  i32 layer = 0;       ///< Draw layer: smaller layers draw first, larger layers draw over them.
  /// Added to `transform.pos.y` when sorting by Y (see draw_set_y_sort()): set it to
  /// the distance from the anchor point down to the feet if the anchor is not at the feet.
  f32 sort_offset = 0.0f;
  bool flip_x = false; ///< Flip horizontally.
  bool flip_y = false; ///< Flip vertically.
  bool visible = true; ///< Hide without removing the component.
  /// Normal map of the sprite, so lights (njin_light.h) give the sprite volume. Must
  /// have the same size and frame layout as `texture`, OpenGL style (green channel
  /// points up), loaded with texture_load() or atlas_load(). Empty means a flat
  /// sprite. A rotated or horizontally flipped sprite does not rotate or flip the normal with it.
  texture_handle normal{};
  /// Material map of the sprite for PBR lighting (njin_light.h), with channels laid
  /// out in raylib's "MRA" style: red is metallic (0 non-metal to 1 metal),
  /// green is roughness (0 mirror-shiny to 1 rough), blue is ambient
  /// occlusion (1 means not occluded). Same size and frame layout as `texture`.
  /// Empty means the default material: non-metal, roughness 0.8, not occluded.
  texture_handle material{};
  /// Emissive map: wherever it has color, that spot glows on its own, with no light
  /// needed (monster eyes, lit windows, glowing mushrooms). The map's color is multiplied by
  /// `emissive_power`. Same size and frame layout as `texture`. Empty means no glow.
  texture_handle emissive{};
  f32 emissive_power = 1.0f; ///< Brightness of `emissive`, from 0 to 8. Above 1 is brighter than the image color, spilling into bloom.
};

/// Frame-based animation from a sprite sheet. Needs a sprite on the same entity.
///
/// The frames all have the same size `frame_size`, laid out in rows from left to
/// right and then top to bottom, numbered from 0. The engine's sprite module advances
/// the frame each frame and writes `sprite.source`. Use anim_play() to switch animation.
struct sprite_anim {
  vec2 frame_size{};    ///< Size of one frame, in pixels.
  i32 first = 0;        ///< Index of the animation's first frame.
  i32 count = 1;        ///< Number of frames in the animation.
  f32 fps = 10.0f;      ///< Frames per second.
  bool loop = true;     ///< Repeat when it ends.
  bool playing = true;  ///< Playing. Set `false` to stop on the current frame.
  f32 time = 0.0f;      ///< Time played, updated by the engine.
  i32 frame = 0;        ///< Current frame, from 0 to `count - 1`.
  bool finished = false; ///< Has run to the end (only when not looping).
};

/// Switches to another animation in the same sprite sheet.
///
/// If it is already playing this animation, does nothing, so it can be called every
/// frame: the animation is not restarted over and over.
/// @param anim Animation to switch.
/// @param first First frame.
/// @param count Number of frames.
/// @param fps Frames per second.
/// @param loop Whether to repeat.
inline void anim_play(sprite_anim &anim, i32 first, i32 count, f32 fps,
                      bool loop = true) {
  if (anim.first == first && anim.count == count && anim.playing &&
      !anim.finished)
    return;
  anim.first = first;
  anim.count = count;
  anim.fps = fps;
  anim.loop = loop;
  anim.playing = true;
  anim.time = 0.0f;
  anim.frame = 0;
  anim.finished = false;
}

/// Attaches the entity to a scene. When that scene is left, the engine destroys the entity.
///
/// See scene_set().
struct scene_owned {
  scene_handle scene{}; ///< Scene that owns the entity.
};

/// Attaches the entity to a parent entity: a weapon in a character's hand, the wheels of a car.
///
/// Needs a transform on the same entity. The engine's hierarchy module **overwrites**
/// that transform every frame with the parent's transform combined with `local`, in
/// `phase_post_update` (before animation, particles and drawing). To move the child
/// entity, edit `local`, not its transform.
///
/// The parent can itself be a child of another entity; the engine updates from the root
/// down. When the parent is destroyed, the child is destroyed with it, unless
/// `destroy_with_parent` is `false`: then the child is detached and stays still at its last position.
///
/// A system in `phase_update` that reads the child's transform sees the previous
/// frame's value. If you need the exact position right away, use transform_combine().
struct child_of {
  entt::entity parent = entt::null; ///< Parent entity.
  transform local{}; ///< Position, rotation, scale relative to the parent.
  bool destroy_with_parent = true; ///< Destroy along with the parent.
};

/// Combines the parent's transform with the child's relative transform.
///
/// `local.pos` is scaled and rotated by the parent, rotations add, scales
/// multiply. This is exactly the computation the hierarchy module uses for child_of.
/// @param parent Transform of the parent, in the world.
/// @param local Transform of the child, relative to the parent.
/// @return Transform of the child, in the world.
inline transform transform_combine(const transform &parent,
                                   const transform &local) {
  return transform{.pos = parent.pos + rotate(local.pos * parent.scale, parent.rot),
                   .rot = parent.rot + local.rot,
                   .scale = parent.scale * local.scale};
}

/// The inverse of transform_combine(): the relative transform of an entity with
/// respect to its parent, from the world transforms of both.
///
/// Use it when attaching an entity to a parent while keeping its current position:
/// `child_of{.parent = p, .local = transform_relative(tr_p, tr_c)}`.
/// @param parent Transform of the parent, in the world.
/// @param world Transform of the child, in the world.
/// @return Transform of the child, relative to the parent. A parent scale of 0 is treated as 1.
inline transform transform_relative(const transform &parent,
                                    const transform &world) {
  const f32 scale = parent.scale != 0.0f ? parent.scale : 1.0f;
  return transform{.pos = rotate(world.pos - parent.pos, -parent.rot) / scale,
                   .rot = world.rot - parent.rot,
                   .scale = world.scale / scale};
}
/// @}
} // namespace njin
