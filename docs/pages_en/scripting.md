# Lua scripts {#scripting}

This page lets a game write its rules and the behaviour of its entities in **Lua 5.4** instead of C++: edit a
`.lua` file, save it, and the running game follows right away, without recompiling. The C++ side still builds the
world, loads assets and keeps whatever must be fast; scripts call into it through the `njin` module. Everything is
declared in `njin_script.h`; Lua and sol2 live inside the engine, and games never include their headers.

Read first: @ref ecs (entities, components, systems) and @ref getting_started. The 2D part uses the building blocks
of @ref platformer; the 3D part uses @ref graphics_3d.

A game without scripts pays nothing: the Lua machine is only created on the first script_* call.

## Running scripts and calling functions

script_run_file() runs a `.lua` file (path resolved like every asset); the functions and globals it creates can be
used from C++. script_call() calls a function by name (a dotted name is looked up in tables), and
script_set_global() and script_get_global() read and write variables.

```cpp
njin::script_run_file(ctx, "scripts/rules.lua");
const njin::script_result r = njin::script_call(ctx, "rules.coin_value", {3.0});
if (r.ok)
  NJIN_INFO("a coin is worth %g points", std::get<double>(r.value));
njin::script_set_global(ctx, "config.difficulty", 2.0);
```

Values passed back and forth are njin::script_value: nil, bool, number (`f64`), string, vec2, vec3 or entity. On
the Lua side an entity is an integer.

## Calling C++ from Lua

script_register() puts a C++ function under a name on the Lua side. The function can take and return bool,
numbers, strings, vec2, vec3 and entities, and can take `context &` first; a missing or mistyped Lua argument
becomes the default value of that type.

```cpp
njin::script_register(ctx, "add_score", [](int points) { score += points; });
njin::script_register(ctx, "game.player", [&] { return player; });
```

```lua
add_score(10)
local p = njin.position(game.player())
```

A C++ exception thrown in that function becomes a Lua error, with the file and line of the call.

## Scripts attached to entities

script_attach() attaches a file to an entity. The file returns a table, like a class; each entity gets its own
`self` table, which every function takes as its first parameter:

| Function | When |
|---|---|
| `on_start(self)` | Once, at the first update after attaching |
| `on_update(self, dt)` | Every frame, in `phase_update`, before the game's systems |
| `on_render(self)` | Every frame, in `phase_render`, to draw with `njin.draw_*` |
| `on_destroy(self)` | When the entity is destroyed or the script detached (script_detach()) |
| `on_reload(self)` | After the file was loaded again (hot reload) |

`self.entity` is the entity. Data the game writes into `self` (health, coins, state) stays with the entity; C++
reads and writes it with script_field() and script_set_field(). When several entities use one file, the file is
loaded once.

An example platformer character: the script reads the keys, feeds them to njin::platformer_body, then flips the
sprite and changes the clip from the state the controller returns.

@include script_player.lua

The C++ side builds the entity, registers functions for the script and attaches the file:

@include script_host.cpp

## The njin module

Every function of the module only calls the C++ API of the same name, adding no behaviour. Entities are integers;
positions are `vec2` or `vec3` (add, subtract, multiply by a number, `:length()`, `:normalized()`, `:dot()`,
`:cross()`). A colour is four numbers `r, g, b, a` (0..1, `a` can be left out).

| Group | Functions |
|---|---|
| Time | `delta`, `elapsed`, `time_scale`, `set_time_scale` |
| Random | `random`, `random_range(lo, hi)`, `random_int(lo, hi)` |
| Entities | `entity_create`, `entity_destroy`, `entity_valid` |
| Transforms | `position`, `set_position`, `rotation`, `set_rotation`, `scale`, `set_scale`; the 3D ones add `3d`: `position3d`... |
| Input | `key_pressed("space")`, `key_held`, `key_released`, `mouse_pos`, `mouse_delta`, `mouse_wheel`, `mouse_pressed("left")`, `action_pressed("jump")`, `action_held`, `action_released`, `axis("move")` |
| Timers | `after(seconds, fn)`, `every(seconds, fn, count)`, `cancel(id)`, `tween_move`, `tween_scale`, `tween_rotate`, `tween_value(from, to, seconds, fn)`, `tween_cancel` |
| Sound | `sound_load`, `sound_play`, `sound_play_at(id, vec2)`, `sound_play3d(id, vec3 or entity)`, `sound_stop` |
| Scenes | `scene_set(name)`, `scene_fade(name)` |
| 2D drawing | `draw_rect`, `draw_circle`, `draw_line`, `draw_text` |
| 2D bodies | `platformer_input(e, move_x, jump, jump_held, drop)`, `platformer(e)`, `platformer_set_velocity`, `topdown_input(e, vec2, dash)`, `topdown(e)`, `topdown_set_velocity` |
| 2D collision | `collision_move(e, vec2)`, `overlap_rect(x, y, w, h)`, `overlap_point(vec2)`, `raycast2d(from, to, ignore)` |
| Sprites | `sprite_flip(e, x, y)`, `sprite_visible`, `sprite_tint`, `anim_play(e, clip)`, `anim_stop`, `anim_resume`, `anim_current`, `anim_set(e, param, value)`, `anim_trigger` |
| 2D camera | `camera_spawn(zoom, vec2)`, `camera_follow(camera, target, {offset, deadzone, lookahead, smoothing, bounds = {x, y, w, h}})` |
| Tilemaps | `tile_get(map, column, row)`, `tile_set`, `tile_cell(map, vec2)`, `tile_solid(map, vec2)` |
| 2D particles | `particles_burst(e, count)`, `particles_spawn(preset, vec2, count)` |
| 3D physics | `raycast3d(origin, direction, max_distance)`, `body_velocity`, `body_set_velocity`, `body_impulse`, `character_move(e, vec3)`, `character_position`, `character_grounded` |
| 3D pathfinding | `nav3d_path(navmesh, from, to)`, `nav3d_set_target(agent, target)`, `nav3d_stop`, `nav3d_position`, `nav3d_velocity`, `nav3d_arrived` (see @ref nav_3d) |
| Log | `log`, `warn`, `error` (and `print`), with the script's file and line |

Functions that return a table: `platformer(e)` has `velocity`, `grounded`, `on_slope`, `on_wall`, `facing`,
`jumped`, `landed`; `topdown(e)` has `velocity`, `facing`, `moving`, `dashing`; `collision_move` has `moved`,
`hit_x`, `hit_y`, `grounded`, `other_x`, `other_y`; `raycast2d` and `raycast3d` have `point`, `normal`, `distance`,
`entity` (and `body` for 3D), or are nil on a miss. An entity without the component a function needs is left
alone (or gives nil).

`platformer_input` and `topdown_input` write to the body's `input` the same way C++ does: a jump or dash press is
kept until the next physics step, so calling them every frame is enough.

## Editing scripts while the game runs

With hot_reload_enable() on (@ref rendering), the engine also watches script files. A file attached to entities is
run again and its new functions replace the old ones for every entity using it, while the data in `self` stays;
`on_reload(self)` is called afterwards. A file loaded with script_run_file() is run again from the top. A new file
with an error keeps the old functions and writes the error to the log. Each reload sends an njin::asset_reloaded
with `script = true`.

## Errors and safety

A Lua error (syntax or at run time) never stops the game: the function reports failure and the log gets the
message with the file and line, for example `scripts/enemy.lua:12: attempt to index a nil value`. An `on_update`
that fails fails every frame: each message is logged once, until the next hot reload.

Scripts run in a restricted Lua machine: no `os.execute`, `io.popen`, `package.loadlib` or bytecode loading (`load`
only takes text); by default no `io`, `dofile` or `loadfile`. `require("ai.patrol")` still works: it looks for
`ai/patrol.lua` (or `scripts/ai/patrol.lua`) like every asset. Trusted tools (level editors, builds) can open `io`
with `script_init(ctx, {.allow_io = true})`, called before any other script_* function.

## Performance and limits

Measured with the harness on 1000 entities with an empty `on_update`: the Debug engine adds about 0.8 ms a frame,
the Release engine about 0.3 ms. A function of the `njin` module is a single C++ call, but each crossing
between Lua and C++ still costs more than a plain C++ call: hot loops over thousands of things (particles,
bullets) belong in C++.

- The module only covers common gameplay; the rest of the engine (3D models, UI, lighting) has no Lua functions
  yet. A game that needs them adds them itself with script_register().
- `on_update` runs in `phase_update`; scripts have no `fixed_update` of their own: drive physics bodies through
  `platformer_input`, `topdown_input` and `character_move`, as C++ does.
- Data in `self` is kept across hot reloads but not written to save files; saving the game is still done in C++
  (@ref window_files).
