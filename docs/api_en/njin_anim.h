#pragma once
#include "_math.h"
#include "_types.h"
#include <array>
#include <vector>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_anim
/// @{

/// Order in which a clip's frames play, like the tag's "Animation Direction" option
/// in Aseprite.
enum anim_direction {
  anim_forward,           ///< From the first frame to the last frame.
  anim_reverse,           ///< From the last frame back to the first frame.
  anim_ping_pong,         ///< Forth and back: 0 1 2 3 2 1, then repeat.
  anim_ping_pong_reverse, ///< Back and forth: 3 2 1 0 1 2, then repeat.
};

/// Describes a clip (a named piece of animation) in a sprite sheet, used with
/// anim_sheet_add_clip().
struct anim_clip_desc {
  const char *name = nullptr; ///< Clip name, unique within the sheet.
  i32 from = 0;               ///< First frame, counted from 0.
  i32 to = 0;                 ///< Last frame, **inclusive**.
  anim_direction direction = anim_forward; ///< Play order.
  /// Number of runs before stopping on the last frame. 0 loops forever.
  i32 repeat = 0;
};

/// Loads a sprite sheet and its clips from a JSON file exported by Aseprite.
///
/// In Aseprite: *File > Export Sprite Sheet*, on the *Output* tab enable **JSON
/// Data** and **Tags** (either "Array" or "Hash" works), on the *Borders* tab disable
/// **Trim**. Or from the command line:
/// @code{.sh}
/// aseprite -b player.aseprite --sheet player.png --data player.json --list-tags --format json-array
/// @endcode
///
/// Each tag becomes a clip of the same name, keeping the tag's direction and repeat
/// count. Each frame's duration is taken exactly as in Aseprite. With no tags there is
/// one clip named `"default"` containing every frame. The image (`meta.image`) is
/// looked up next to the JSON file and belongs to the sheet: unloading the sheet also
/// frees the image.
/// @param ctx Engine context.
/// @param json_path Path of the JSON file.
/// @return Handle of the sheet, or a handle with id 0 if the file is missing or broken (logged).
anim_sheet_handle anim_sheet_load(njin_ctx &ctx, const char *json_path);

/// Creates a sprite sheet from an image split into an even grid, with no clips.
///
/// Frames are laid out in rows from left to right and then top to bottom, numbered
/// from 0, like njin::sprite_anim. Add clips with anim_sheet_add_clip(). The image
/// does **not** belong to the sheet: the game unloads it itself.
/// @param ctx Engine context.
/// @param texture Sprite sheet image.
/// @param frame_size Size of one frame, in pixels.
/// @param fps Frames per second, used for every frame.
/// @return Handle of the sheet, or a handle with id 0 if the image or the size is invalid.
anim_sheet_handle anim_sheet_grid(njin_ctx &ctx, texture_handle texture,
                                  vec2 frame_size, f32 fps);

/// Adds a clip to the sheet.
/// @param ctx Engine context.
/// @param sheet Sheet.
/// @param desc Clip description.
/// @return `false` if the sheet is invalid, the name is a duplicate, or a frame is outside the sheet.
bool anim_sheet_add_clip(njin_ctx &ctx, anim_sheet_handle sheet,
                         const anim_clip_desc &desc);

/// Frees the sheet, and its image if the sheet was loaded from Aseprite.
/// @param ctx Engine context.
/// @param sheet Sheet. An invalid handle is ignored.
void anim_sheet_unload(njin_ctx &ctx, anim_sheet_handle sheet);

/// Finds a clip by name.
/// @param ctx Engine context.
/// @param sheet Sheet.
/// @param name Clip name.
/// @return Index of the clip, or -1 if there is none.
i32 anim_clip_find(const njin_ctx &ctx, anim_sheet_handle sheet,
                   const char *name);

/// Duration of one run of the clip, in seconds. Use it to time an attack or an effect
/// to the animation.
/// @param ctx Engine context.
/// @param sheet Sheet.
/// @param name Clip name.
/// @return Total duration of the frames, or 0 if there is no such clip.
f32 anim_clip_duration(const njin_ctx &ctx, anim_sheet_handle sheet,
                       const char *name);

/// Comparison of a transition condition.
enum anim_cmp {
  anim_gt,      ///< Parameter is greater than `value`.
  anim_lt,      ///< Parameter is less than `value`.
  anim_ge,      ///< Parameter is greater than or equal to `value`.
  anim_le,      ///< Parameter is less than or equal to `value`.
  anim_eq,      ///< Parameter equals `value`.
  anim_ne,      ///< Parameter differs from `value`.
  anim_true,    ///< Parameter is non-zero (bool is on).
  anim_false,   ///< Parameter equals 0 (bool is off).
  /// Parameter was set by animator_trigger() **during this frame**. Consumed when a
  /// transition fires, and cleared at the end of the frame if unused.
  anim_trigger,
};

/// One condition of a transition.
struct anim_cond {
  const char *param = nullptr; ///< Parameter name.
  anim_cmp cmp = anim_true;    ///< Comparison.
  f32 value = 0.0f;            ///< Value to compare with (ignored for true/false/trigger).
};

/// One state of the state machine: a clip that plays while in that state.
struct anim_state_desc {
  const char *name = nullptr; ///< State name, unique within the graph.
  const char *clip = nullptr; ///< Clip name in the graph's sheet.
  f32 speed = 1.0f;           ///< Clip playback speed. 2 is twice as fast.
  /// Overrides the clip's run count (see anim_clip_desc::repeat). -1 keeps the clip's own.
  i32 repeat = -1;
};

/// A transition: from `from` to `to` when all conditions are true.
///
/// Each frame the engine checks transitions in **declaration order** and uses the
/// first one that is satisfied, so put important transitions (hit, death) first.
struct anim_transition_desc {
  /// Source state. null means "from any state" (except `to` itself).
  const char *from = nullptr;
  const char *to = nullptr; ///< Destination state.
  std::vector<anim_cond> when{}; ///< Conditions, all must be true. Empty is always true.
  /// Transition only once the current clip has finished at least one run. Used for
  /// attacks: `{.from = "attack", .to = "idle", .after_finish = true}`.
  bool after_finish = false;
};

/// Describes an animation state machine, used with anim_graph_create().
///
/// @code
/// g.player_anim = njin::anim_graph_create(ctx, {
///     .sheet = sheet,
///     .states = {{.name = "idle", .clip = "idle"},
///                {.name = "run", .clip = "run"},
///                {.name = "jump", .clip = "jump", .repeat = 1},
///                {.name = "attack", .clip = "attack", .repeat = 1}},
///     .transitions = {
///         {.to = "attack", .when = {{"attack", njin::anim_trigger}}},
///         {.from = "attack", .to = "idle", .after_finish = true},
///         {.from = "idle", .to = "jump", .when = {{"grounded", njin::anim_false}}},
///         {.from = "run", .to = "jump", .when = {{"grounded", njin::anim_false}}},
///         {.from = "jump", .to = "idle", .when = {{"grounded", njin::anim_true}}},
///         {.from = "idle", .to = "run", .when = {{"speed", njin::anim_gt, 10}}},
///         {.from = "run", .to = "idle", .when = {{"speed", njin::anim_le, 10}}},
///     }});
/// @endcode
struct anim_graph_desc {
  anim_sheet_handle sheet{}; ///< Sheet containing the clips.
  std::vector<anim_state_desc> states{}; ///< The states.
  std::vector<anim_transition_desc> transitions{}; ///< The transitions.
  const char *start = nullptr; ///< Initial state. null means the first state.
};

/// Maximum number of parameters of a state machine.
inline constexpr i32 anim_max_params = 16;

/// Creates an animation state machine. Many entities share one graph, and each
/// entity has its own state and parameters in njin::animator.
/// @param ctx Engine context.
/// @param desc Graph description.
/// @return Handle of the graph, or a handle with id 0 if a clip or state name does
/// not exist, or there are more than anim_max_params parameters (logged).
anim_graph_handle anim_graph_create(njin_ctx &ctx, const anim_graph_desc &desc);

/// Plays animation from a sheet with clips, for an entity that has a sprite.
///
/// The engine's anim module, in `phase_post_update`, evaluates transitions, advances
/// frames by delta() and writes `sprite.texture`, `sprite.source`. Two ways to use it:
/// - **With a graph**: set `graph`, then each frame the game only updates parameters
///   with animator_set(), animator_set_bool(), animator_trigger(). The graph picks the clip.
/// - **Without a graph**: set `sheet`, then call animator_play() with a clip name.
///
/// This is the full version of njin::sprite_anim: per-frame durations, named clips,
/// reverse and ping-pong playback. Do not attach both to one entity.
struct animator {
  anim_graph_handle graph{}; ///< State machine. If set, `sheet` is ignored.
  anim_sheet_handle sheet{}; ///< Sheet, when not using a graph.
  f32 speed = 1.0f;          ///< Overall speed. 0 freezes the animation.
  bool playing = true;       ///< `false` to stop on the current frame.

  /// @name State (managed by the engine, read-only)
  /// @{
  i32 state = -1;  ///< Current state in the graph, -1 if not started.
  i32 clip = -1;   ///< Clip that is playing, -1 if none.
  i32 frame = -1;  ///< Frame currently shown in the sheet, -1 if none yet.
  i32 step = 0;    ///< Position in the clip's play order.
  f32 time = 0.0f; ///< Time spent on the current frame, in seconds.
  f32 state_time = 0.0f; ///< Time spent in the current state (or clip), in seconds.
  i32 loops = 0;   ///< Number of runs the clip has completed.
  i32 repeat = 0;  ///< Run count of the current clip, 0 loops forever.
  bool finished = false; ///< A non-looping clip has run to the end and stopped on the last frame.
  std::array<f32, anim_max_params> params{}; ///< Values of the graph's parameters.
  /// @}
};

/// Switches immediately to a clip (no graph) or a state (with a graph).
///
/// If it is already playing that one and has not finished, does nothing, so it can be
/// called every frame. With a graph, transitions are still evaluated from the next frame.
/// @param ctx Engine context.
/// @param anim Animator.
/// @param name Clip name, or state name if there is a graph.
/// @param restart Start over even if it is already playing that one.
/// @return `false` if the name is not found.
bool animator_play(const njin_ctx &ctx, animator &anim, const char *name,
                   bool restart = false);

/// Sets a numeric parameter of the graph, for example run speed.
/// @param ctx Engine context.
/// @param anim Animator.
/// @param param Parameter name (the name used in anim_cond).
/// @param value Value.
void animator_set(const njin_ctx &ctx, animator &anim, const char *param,
                  f32 value);

/// Sets a bool parameter of the graph, for example standing on the ground.
/// @param ctx Engine context.
/// @param anim Animator.
/// @param param Parameter name.
/// @param value Value.
void animator_set_bool(const njin_ctx &ctx, animator &anim, const char *param,
                       bool value);

/// Sets a trigger of the graph for this frame, for example the attack button was just pressed.
/// @param ctx Engine context.
/// @param anim Animator.
/// @param param Parameter name (used with anim_trigger).
void animator_trigger(const njin_ctx &ctx, animator &anim, const char *param);

/// Whether it is in this state (with a graph) or clip (without a graph).
/// @param ctx Engine context.
/// @param anim Animator.
/// @param name State or clip name.
/// @return `true` if it is.
bool animator_in(const njin_ctx &ctx, const animator &anim, const char *name);

/// Name of the current state (with a graph) or clip (without a graph).
/// @param ctx Engine context.
/// @param anim Animator.
/// @return The name, or an empty string if there is none yet. Never null.
const char *animator_current(const njin_ctx &ctx, const animator &anim);
/// @}
} // namespace njin
