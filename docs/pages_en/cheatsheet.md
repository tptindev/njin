# What to use for X {#cheatsheet}

A quick lookup table: find what you want to do in the left column, use the thing in the middle column, read the page in the right column.
Everything lives in `njin::` and all you need is `#include <njin.h>`. To see the full parameters of a function, click its
name, or go to [API groups](topics.html).

Not sure where to start? Read @ref first_jump (platformer) or @ref first_walk (top-down) first: a character
that runs in 50 lines. Not used to `entt::registry` yet? Read @ref ecs.

## Starting up and structuring your game

| I want to | Use | See |
|---|---|---|
| Open a window, run, close | njin::njin_create(), njin::njin_run(), njin::njin_destroy() | @ref getting_started |
| Quit the game from inside the game | njin::njin_quit() | @ref window_files |
| Keep your game logic in one place | njin::mod_desc with `setup`, registered with njin::njin_mod_register() | @ref modules_systems |
| Run a function every frame, or once at the start | njin::ecs_register() with a njin::sys_phase | @ref game_loop |
| Steady physics, independent of FPS | `phase_fixed_update` | @ref time |
| Create an entity, attach components | njin::world() then `registry.create()`, `emplace<>()` | @ref ecs |
| Send messages between systems | njin::events() | @ref ecs |
| Create many identical entities (monsters, bullets) | njin::prefab_register(), njin::prefab_spawn() | @ref prefabs |
| Split the game into menu, level, game over | njin::scene_register(), njin::scene_set() | @ref scenes |
| Fade between scenes | njin::scene_fade() | @ref scenes |

## Characters and movement

| I want to | Use | See |
|---|---|---|
| A platformer character that runs and jumps | njin::platformer_body + njin::platformer_input_map | @ref first_jump, @ref platformer |
| Double jump, wall slide, wall jump | `air_jumps`, `wall_slide_speed`, `wall_jump` of njin::platformer_body | @ref platformer |
| A top-down character that walks in 8 directions and dashes | njin::topdown_body + njin::topdown_input_map | @ref first_walk, @ref topdown |
| Knockback when hit | write straight into the body's `velocity` | @ref platformer |
| Moving platforms, patrolling guards | njin::path_mover | @ref platformer |
| Enemies that chase and path-find around walls | njin::nav_grid_from_world(), njin::nav_find_path(), njin::nav_steer() | @ref topdown |
| Move an object to a point over a period of time | njin::tween_move() | @ref screen_timers |
| Write your own movement controller | njin::collision_move() | @ref collision |

## Collision

| I want to | Use | See |
|---|---|---|
| Give an object a collision shape | njin::collider (box or circle) | @ref collision |
| Know when two objects just touched (bullet hits monster, item pickup) | events njin::collision_enter, njin::collision_exit, `trigger = true` | @ref collision |
| Choose what collides with what | `layer` and `mask` of njin::collider, njin::layer_bit() | @ref collision |
| Walls and ground from a tile map | a `collider_tiles` collider on the njin::tilemap entity | @ref collision, @ref tilemap |
| What is in the attack area, the blast area, under the mouse | njin::collision_overlap_rect(), njin::collision_overlap_circle(), njin::collision_overlap_point() | @ref collision |
| Cast a ray, check line of sight | njin::collision_raycast(), njin::collision_line_of_sight() | @ref collision, @ref topdown |
| See the collision boxes for debugging | njin::collision_set_debug() | @ref collision |

## Maps and levels

| I want to | Use | See |
|---|---|---|
| Draw a square-grid map in code | njin::tilemap, njin::tilemap_set() | @ref tilemap |
| Write a map as text, one character per tile (no Tiled or LDtk needed) | njin::tilemap_from_text(), njin::tilemap_from_rows() | @ref tilemap |
| Tiles that are slopes, one-way platforms, or non-colliding | njin::tilemap_set_shape() | @ref platformer |
| Moving water, torches | njin::tilemap_animate() | @ref tilemap |
| Generate a top-down map (islands, land) from noise | njin::generate_topdown(), njin::noise_2d() | @ref procgen |
| Generate a playable platformer level (pits, caves, platforms) | njin::generate_platformer() | @ref procgen |
| Make a generated map less noisy: remove stray dots, add borders, scatter flowers | njin::grid_majority(), njin::grid_border(), njin::grid_scatter() | @ref procgen |
| Naturally rounded corners for ground, water, walls (autotile) | njin::grid_autotile(), njin::autotile_index() | @ref procgen_autotile |
| Generate a map with Wave Function Collapse, from a sample or your own rules | njin::wfc_learn(), njin::wfc_generate() | @ref procgen |
| Put a generated grid into a tilemap | njin::tilemap_from_grid() | @ref procgen |
| Load a level drawn in Tiled or LDtk | njin::level_load(), njin::level_load_ldtk() | @ref level |
| Find spawn points, doors, monsters placed in a level | njin::level_find() | @ref level |
| See which tile is at a position, whether something touches a tile | njin::tilemap_cell_at(), njin::tilemap_overlaps(), njin::tilemap_move() | @ref tilemap |

## Camera

| I want to | Use | See |
|---|---|---|
| Create a camera | njin::camera_spawn() | @ref camera |
| A camera that follows the character and never shows outside the level | njin::camera_follow, `bounds` from njin::level_bounds() | @ref camera, @ref platformer |
| Turn a mouse position into a world position | njin::scr2w() (the reverse is njin::w2scr()) | @ref camera |
| Skip drawing whatever is off screen | njin::camera_bounds() | @ref camera |
| Shake the screen | njin::camera_shake() | @ref particles |
| Pixel art at a fixed resolution | njin::window_set_virtual_size(), or `virtual_size` in njin::njin_cfg | @ref drawing, @ref screen_timers |

## Input

| I want to | Use | See |
|---|---|---|
| Ask "is the jump button pressed" (without asking about a specific key) | njin::action_define(), njin::action_pressed() | @ref input |
| A horizontal/vertical axis from two keys or an analog stick | njin::axis_define(), njin::axis_value() | @ref input |
| Read keys, mouse, gamepad directly | njin::key_pressed(), njin::mouse_pos(), njin::pad_axis() | @ref input |
| Type text | njin::text_char() | @ref input |
| Let the player rebind keys | njin::action_rebind(), njin::input_bindings_save() | @ref settings |
| Rumble the gamepad | njin::pad_rumble() | @ref settings |

## Drawing

| I want to | Use | See |
|---|---|---|
| Draw rectangles, circles, lines | njin::draw_rect(), njin::draw_circle(), njin::draw_line() | @ref drawing |
| Draw text (Vietnamese included) | njin::draw_text(), njin::font_load() | @ref drawing |
| Measure or wrap text | njin::text_measure(), njin::text_wrap() | @ref drawing |
| Load and draw an image | njin::texture_load(), njin::texture_draw() | @ref rendering |
| Pixel art images that don't blur | njin::texture_set_filter() with `filter_nearest` | @ref rendering |
| A character with an image | njin::sprite | @ref sprites |
| Sort who stands in front of whom (top-down) | njin::draw_set_y_sort() | @ref topdown |
| Pack many small images into one page | njin::atlas_create(), njin::atlas_load() | @ref rendering |
| Lighting: point lights, spot lights, directional lights, night, sun | njin::lighting_set(), njin::light_2d | @ref lighting |
| Pixel-exact sprite shadows (trees, bushes, rocks, characters) | njin::light_occluder_pixels | @ref lighting |
| Shadows from walls, fences, geometric shapes | njin::light_occluder, njin::light_occluder_capsule(), njin::light_occluder_sprite | @ref lighting |
| Shadows from tilemap walls | njin::light_occluders_from_tiles() | @ref lighting |
| Sprites with volume, metal, mirror-like highlights under lights | `sprite::normal`, `sprite::material` (PBR, MRA channel layout like raylib) | @ref lighting |
| Self-illuminated sprites (glowing flowers, monster eyes, windows) | `sprite::emissive`, `sprite::emissive_power` | @ref lighting |
| Change how HDR is compressed (Reinhard, ACES) | njin::lighting_desc::tonemap | @ref lighting |
| Your own shader | njin::shader_load(), njin::shader_set_f32() | @ref rendering |
| A shader that reads an extra image (palette, noise, mask) | njin::shader_set_texture() | @ref shader_advanced |
| A shader that takes a list (light sources) | njin::shader_set_vec4_array() | @ref shader_advanced |
| One shader over the whole frame: lit nights, dusk, fog | njin::camera_set_post_shader() with extra images and uniform arrays | @ref shader_advanced |
| Draw thousands of identical shapes with one draw call | njin::instance_buffer_create(), njin::draw_instanced() | @ref instancing |
| Save a render texture (a baked sprite sheet) to PNG | njin::render_texture_save() | @ref instancing_bake |
| Pre-draw shapes into a sprite sheet and read it back every frame | njin::render_texture_begin(), njin::draw_instanced() with a render texture, njin::render_texture_set_filter() | @ref instancing_bake |
| Health bars, cooldown rings, shadows drawn with a formula (SDF) | njin::shader_begin() around a stretched image, njin::shader_set_vec2() | @ref learn_shader_sdf |
| Draw into an off-screen image | njin::render_texture_load(), njin::render_texture_begin() | @ref rendering |

## Sprites and animation

| I want to | Use | See |
|---|---|---|
| An animation from an evenly spaced sprite sheet | njin::sprite_anim | @ref sprites |
| An animation from Aseprite | njin::anim_sheet_load() | @ref animation |
| Switch between idle, run, jump by state | njin::anim_graph_create(), njin::animator_set_bool(), njin::animator_play() | @ref animation |

## Effects and game feel

| I want to | Use | See |
|---|---|---|
| Explosions, dust, sparks, smoke, fire | njin::particles_spawn() with a preset from `njin::fx::` | @ref particles |
| Freeze the frame briefly on a hit | njin::hitstop() | @ref particles |
| Flash a sprite white, flash the whole screen | njin::sprite_flash(), njin::screen_flash() | @ref particles |
| An enemy dies: the sprite dissolves away (or fades in) | njin::sprite_dissolve(), njin::dissolve_fx | @ref particles |
| Slow motion, pause | njin::time_set_scale(), njin::time_set_paused() | @ref time |
| Full-screen blur, CRT, bloom, vignette | njin::post_fx_set() | @ref post_processing |
| Blend smoothly between two sets of effects | njin::post_fx_lerp() | @ref post_processing |

## Time and randomness

| I want to | Use | See |
|---|---|---|
| The frame time, to multiply into velocity | njin::delta() | @ref time |
| Do something after N seconds | njin::timer_after() | @ref screen_timers |
| Do something every N seconds (spawn monsters, regenerate health) | njin::timer_every() | @ref screen_timers |
| Run a value from A to B, smoothly | njin::tween_value(), njin::tween_move() | @ref screen_timers |
| Random numbers | njin::random() | @ref math |
| Geometry: vec2, rectangles, basic collision | njin::vec2, njin::rect, njin::collide_rects() | @ref math |

## Audio

| I want to | Use | See |
|---|---|---|
| Play a sound effect | njin::sound_load(), njin::sound_play_once() | @ref audio |
| Positional sound that fades with distance | njin::sound_play_at() | @ref audio |
| Background music, smooth music transitions | njin::music_load(), njin::music_play(), njin::music_crossfade() | @ref audio |
| Volume sliders for music, effects, interface | njin::audio_set_bus_volume() | @ref settings |

## UI, dialogs, multiple languages

| I want to | Use | See |
|---|---|---|
| A menu with buttons and sliders that works with a gamepad | njin::ui_begin(), njin::ui_button(), njin::ui_slider() | @ref ui |
| A small notice like "Game saved" | njin::ui_toast() | @ref ui |
| Ask for confirmation (popup) | njin::ui_popup() | @ref ui |
| NPC dialog, typewriter text, choices | njin::dialog_load(), njin::dialog_start() | @ref dialog |
| Multiple languages | njin::i18n_load(), njin::i18n_set_language(), njin::tr() | @ref dialog |

## Saving, settings, window

| I want to | Use | See |
|---|---|---|
| The user's save-game path | njin::save_path() | @ref window_files |
| Read and write files | njin::file_read(), njin::file_write() | @ref window_files |
| Save the game as JSON | njin::json_save(), njin::json_load() | @ref json |
| Save and load volume and rebound keys | njin::settings_save(), njin::settings_load() | @ref settings |
| Fullscreen | njin::window_set_fullscreen() | @ref window_files |
| Take a screenshot | njin::screenshot() | @ref window_files |

## Debug

| I want to | Use | See |
|---|---|---|
| See FPS, entities, colliders, logs in a separate window | njin::debug_server_start() then open njin_inspector | @ref debug |
| Watch a value as it changes | njin::debug_watch() | @ref debug |
| Record the game window as a GIF (press F9 in the inspector) | njin::debug_server_start() then open njin_inspector | @ref debug_recording |
| Show your own components in the inspector | njin::debug_component | @ref debug |
| Write a log | `NJIN_INFO`, `NJIN_WARN`, `NJIN_ERROR` | @ref logging |
| Edit images and shaders while the game is running | njin::hot_reload_enable() | @ref rendering |
