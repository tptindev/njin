# Animation and state machines {#animation}

njin::sprite_anim (see @ref sprites) is enough for a strip of frames that plays evenly. A real character needs
more: a separate duration for each frame, several named animations, and rules for switching between
them (idle → run → jump → attack). This part does that.

Three concepts:

| Concept | What it is | Created with |
|---|---|---|
| **Sheet** | An image + a list of frames (region, duration) + named **clips** | njin::anim_sheet_load() (Aseprite) or njin::anim_sheet_grid() |
| **Graph** | A state machine: states (each plays a clip) and transition rules | njin::anim_graph_create() |
| **Animator** | A component on an entity: the current state and its own parameters | `reg.emplace<njin::animator>(e, {.graph = g})` |

Many entities share one sheet and one graph; each entity has its own animator.

## Example

@include animation.cpp

## Exporting from Aseprite

In Aseprite, put a **tag** on each animation segment (idle, run...), then *File > Export Sprite
Sheet*:

- the *Output* tab: turn on **JSON Data**, choose *Array* or *Hash*, turn on **Tags**;
- the *Borders* tab: **turn off Trim** (trimmed frames end up misplaced; the engine warns if it sees this).

Or from the command line:

```sh
aseprite -b hero.aseprite --sheet hero.png --data hero.json --list-tags --format json-array
```

njin::anim_sheet_load() reads the JSON file, loads the image next to it, and turns each tag into a clip
with the same name. What it takes from Aseprite:

| In Aseprite | In njin |
|---|---|
| Each frame's duration | Kept as is, in milliseconds |
| The tag's direction: Forward, Reverse, Ping-pong, Ping-pong Reverse | njin::anim_direction |
| The tag's Repeat (∞ or a count) | The clip's `repeat`: 0 means loop forever |

With no tags, you get one clip named `"default"` containing all the frames.

For a sheet split into an even grid (not using Aseprite), create it with njin::anim_sheet_grid() then add clips
with njin::anim_sheet_add_clip().

## The state machine

Every frame, in `phase_post_update`, the engine:

1. checks the transitions **in the order they were declared**, using the first one that is satisfied (at most
   one per frame);
2. clears all triggers;
3. advances the clip by delta() times `animator.speed` and the state's `speed`;
4. writes `sprite.texture` and `sprite.source`.

A transition is satisfied when:

- `from` is the current state, or `from` is null (from any state except `to` itself);
- every condition in `when` is true;
- if `after_finish` is set: the current clip has finished at least one pass.

Because they are checked in order, put the important transitions first: hit, death, and only then attack,
jump, run.

### Parameters

A condition compares a **parameter** with a value. Parameters are named floats, created automatically from the
conditions (at most njin::anim_max_params of them). The game sets them every frame:

| Function | Used for | Compared with |
|---|---|---|
| njin::animator_set() | Numbers: speed, health | `anim_gt`, `anim_lt`, `anim_ge`, `anim_le`, `anim_eq`, `anim_ne` |
| njin::animator_set_bool() | On/off: standing on the ground | `anim_true`, `anim_false` |
| njin::animator_trigger() | One-time event: pressing the attack button | `anim_trigger` |

A trigger only lives **in the frame it was set**. It is consumed when it causes a transition, and switches itself
off at the end of the frame if unused, so a press made while jumping is not "saved up" and then suddenly
fires later.

### Without a graph

If you only need to play a clip by name, assign `sheet` instead of `graph` and call njin::animator_play():

```cpp
reg.emplace<njin::animator>(coin, njin::animator{.sheet = coin_sheet});
njin::animator_play(ctx, reg.get<njin::animator>(coin), "spin");
```

Like njin::anim_play(), it can be called every frame: if that clip is already playing, it does nothing.
With a graph, njin::animator_play() jumps straight to a state (for example on respawn).

## Reading the state

| Field / function | Meaning |
|---|---|
| njin::animator_in() | Whether it is in the state (or clip) with this name |
| njin::animator_current() | The current state (or clip) name |
| `animator.finished` | A non-looping clip has finished |
| `animator.state_time` | How long it has been in the current state |
| `animator.frame` | The frame currently shown in the sheet, for timing hitboxes per frame |
| njin::anim_clip_duration() | The total duration of one pass of the clip |
