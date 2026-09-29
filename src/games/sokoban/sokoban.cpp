#include "sokoban.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// Sokoban in 2.5D on njin's 3D API (njin_3d.h):
//   grid        the rules run on cells; the 3D scene only shows them
//   camera      tilted down at the level, framed to its size
//   instancing  the floor tiles and wall blocks, one draw call each
//               (draw_instanced3d), rebuilt when a level loads
//   crates      textured boxes (assets/crate.png, tools/make_assets.py)
//   player      a smooth SDF capsule (draw_shape3d) with a glossy surface and
//               a rim light, and two SDF eyes looking where it last moved
//   light       a sun with shadows, a warm point light over every goal, bloom
//   effects     dust when a crate is pushed, a flash and sparkles when one
//               lands on a goal, a small shake on bumping a wall, crates and
//               player dissolving in when a level starts, a screen flash and
//               a burst when it is solved
//   gizmos      G shows the debug view: the axes at the origin, the player's
//               cell, a box over every goal (njin_gizmo.h)
//   motion      each step slides the player (and the crate it pushes) over
//               a short time, with a small hop
//   undo        every step keeps a snapshot; Z steps back, R restarts

namespace sokoban {
namespace {
using namespace njin;

struct cell {
  i32 x = 0;
  i32 y = 0;
  bool operator==(const cell &o) const { return x == o.x && y == o.y; }
};
cell operator+(cell a, cell b) { return {a.x + b.x, a.y + b.y}; }

// '#' wall, '.' goal, '$' crate, '*' crate on a goal, '@' player, '+' player
// on a goal. All five have been checked solvable.
const std::vector<std::vector<std::string>> levels = {
    {"########",
     "#      #",
     "#@ $  .#",
     "#      #",
     "########"},
    {"########",
     "#   .  #",
     "# $ #  #",
     "# @$ . #",
     "#   #  #",
     "########"},
    {"  ######",
     "  #    #",
     "###$## #",
     "#  . $ #",
     "# #.@  #",
     "#   ####",
     "#####   "},
    {" ####### ",
     "##  .  ##",
     "#  $#$  #",
     "# .   . #",
     "#  $#$  #",
     "##  .@ ##",
     " ####### "},
    {"#########",
     "#.  #  .#",
     "# $   $ #",
     "## # # ##",
     "#  $@$  #",
     "#.  #  .#",
     "#########"},
};

constexpr f32 step_time = 0.13f;   // seconds to slide one cell
constexpr f32 repeat_time = 0.17f; // a held key steps again after this
constexpr f32 hop = 0.12f;

constexpr rgba floor_a{0.78f, 0.74f, 0.66f, 1.0f};
constexpr rgba floor_b{0.72f, 0.68f, 0.60f, 1.0f};
constexpr rgba wall_color{0.42f, 0.45f, 0.52f, 1.0f};
constexpr rgba wall_top_color{0.55f, 0.58f, 0.66f, 1.0f};
constexpr rgba goal_color{0.98f, 0.78f, 0.25f, 1.0f};
constexpr rgba crate_color{0.74f, 0.50f, 0.27f, 1.0f};
constexpr rgba crate_done_color{0.42f, 0.72f, 0.36f, 1.0f};
constexpr rgba player_color{0.26f, 0.56f, 0.96f, 1.0f};
constexpr f32 appear_time = 0.6f;   // crates and player dissolve in
constexpr f32 crate_flash_time = 0.35f;
constexpr f32 fx_scale = 0.012f;     // emitter units (pixels) to cells

struct snapshot {
  cell player;
  cell facing;
  std::vector<cell> crates;
  i32 moves = 0;
  i32 pushes = 0;
};

struct game_state {
  usize level = 0;
  i32 w = 0, h = 0;
  std::vector<u8> wall, goal, inside; // per cell
  cell player;
  cell facing{0, 1};
  std::vector<cell> crates;
  i32 moves = 0, pushes = 0;
  std::vector<snapshot> history;
  bool solved = false;
  // Sliding: the player, and the crate it pushed (index or -1), move from
  // their previous cell for step_time.
  cell player_from;
  cell crate_from;
  i32 moving_crate = -1;
  f32 slide = 1.0f; // 0..1, 1 = at rest
  f32 hold = 0.0f;  // time the current direction key has been held
  f32 appear = 0.0f; // time since the level started
  std::vector<f32> crate_flash; // per crate, time left on its goal flash
  texture_handle crate_texture;
  instance_buffer_handle floor_tiles, wall_blocks;
};
game_state g;

usize at(cell c) { return (usize)(c.y * g.w + c.x); }
bool in_grid(cell c) { return c.x >= 0 && c.y >= 0 && c.x < g.w && c.y < g.h; }
bool is_wall(cell c) { return !in_grid(c) || g.wall[at(c)] != 0; }
i32 crate_at(cell c) {
  for (usize i = 0; i < g.crates.size(); i++) {
    if (g.crates[i] == c)
      return (i32)i;
  }
  return -1;
}

// Instance data for draw_instanced3d: position and scale, colour, rotation,
// per-axis scale (16 floats).
void push_block(std::vector<f32> &out, vec3 pos, rgba color, vec3 size) {
  const f32 v[16] = {pos.x, pos.y, pos.z, 1.0f, color.r, color.g, color.b, color.a,
                     0.0f,  0.0f,  0.0f,  0.0f, size.x,  size.y,  size.z,  0.0f};
  out.insert(out.end(), std::begin(v), std::end(v));
}

vec3 world_of(vec2 c);
vec3 world_of(cell c);

// The floor and the walls of the level as two instance buffers.
void build_blocks(context &ctx) {
  std::vector<f32> floor, walls;
  for (i32 y = 0; y < g.h; y++) {
    for (i32 x = 0; x < g.w; x++) {
      const cell c{x, y};
      const vec3 p = world_of(c);
      if (g.wall[at(c)] != 0) {
        // A lighter cap inset on each block, so a row of walls reads as
        // separate blocks from above instead of one slab.
        push_block(walls, p + vec3{0.0f, 0.3f, 0.0f}, wall_color, {1.0f, 0.6f, 1.0f});
        push_block(walls, p + vec3{0.0f, 0.62f, 0.0f}, wall_top_color, {0.86f, 0.04f, 0.86f});
      } else if (g.inside[at(c)] != 0) {
        push_block(floor, p + vec3{0.0f, -0.05f, 0.0f}, (x + y) % 2 == 0 ? floor_a : floor_b,
                   {1.0f, 0.1f, 1.0f});
      }
    }
  }
  instance_buffer_upload(ctx, g.floor_tiles, floor.data(), (u32)(floor.size() / 16));
  instance_buffer_upload(ctx, g.wall_blocks, walls.data(), (u32)(walls.size() / 16));
}

void load_level(context &ctx, usize index) {
  const std::vector<std::string> &rows = levels[index];
  g.level = index;
  g.h = (i32)rows.size();
  g.w = 0;
  for (const std::string &r : rows)
    g.w = std::max(g.w, (i32)r.size());
  const usize n = (usize)(g.w * g.h);
  g.wall.assign(n, 0);
  g.goal.assign(n, 0);
  g.inside.assign(n, 0);
  g.crates.clear();
  for (i32 y = 0; y < g.h; y++) {
    for (i32 x = 0; x < g.w; x++) {
      const char c = x < (i32)rows[(usize)y].size() ? rows[(usize)y][(usize)x] : ' ';
      const cell p{x, y};
      g.wall[at(p)] = c == '#';
      g.goal[at(p)] = c == '.' || c == '*' || c == '+';
      if (c == '$' || c == '*')
        g.crates.push_back(p);
      if (c == '@' || c == '+')
        g.player = p;
    }
  }
  // The floor is what the player can reach, walls aside: flood from the start.
  std::vector<cell> open{g.player};
  g.inside[at(g.player)] = 1;
  while (!open.empty()) {
    const cell c = open.back();
    open.pop_back();
    for (cell d : {cell{1, 0}, cell{-1, 0}, cell{0, 1}, cell{0, -1}}) {
      const cell n2 = c + d;
      if (in_grid(n2) && !is_wall(n2) && g.inside[at(n2)] == 0) {
        g.inside[at(n2)] = 1;
        open.push_back(n2);
      }
    }
  }
  g.facing = {0, 1};
  g.moves = g.pushes = 0;
  g.history.clear();
  g.solved = false;
  g.slide = 1.0f;
  g.moving_crate = -1;
  g.appear = 0.0f;
  g.crate_flash.assign(g.crates.size(), 0.0f);
  particles3d_clear(ctx);
  build_blocks(ctx);
}

bool all_on_goals() {
  for (const cell &c : g.crates) {
    if (g.goal[at(c)] == 0)
      return false;
  }
  return true;
}

// One step of the rules. Returns false when the move is blocked.
bool try_move(context &ctx, cell d) {
  // Turning toward a wall still counts: the player looks that way.
  const cell was_facing = g.facing;
  g.facing = d;
  const cell next = g.player + d;
  if (is_wall(next)) {
    camera_shake(ctx, 0.15f);
    return false;
  }
  const i32 crate = crate_at(next);
  if (crate >= 0) {
    const cell beyond = next + d;
    if (is_wall(beyond) || crate_at(beyond) >= 0) {
      camera_shake(ctx, 0.15f);
      return false;
    }
  }
  g.history.push_back({g.player, was_facing, g.crates, g.moves, g.pushes});
  g.player_from = g.player;
  g.player = next;
  g.moving_crate = crate;
  if (crate >= 0) {
    g.crate_from = g.crates[(usize)crate];
    g.crates[(usize)crate] = next + d;
    g.pushes++;
    // Dust kicked up behind the crate, then a flash and sparkles if it lands
    // on a goal.
    const vec3 back = world_of(g.crate_from) - vec3{(f32)d.x, 0.0f, (f32)d.y} * 0.4f;
    particles3d_spawn(ctx, fx::dust(), back + vec3{0.0f, 0.05f, 0.0f}, 10, {.scale = fx_scale});
    const cell landed = g.crates[(usize)crate];
    if (g.goal[at(landed)] != 0) {
      g.crate_flash[(usize)crate] = crate_flash_time;
      particles3d_spawn(ctx, fx::sparkle(), world_of(landed) + vec3{0.0f, 0.7f, 0.0f}, 24,
                        {.scale = fx_scale});
    }
  }
  g.moves++;
  g.slide = 0.0f;
  g.solved = all_on_goals();
  return true;
}

void undo(context &ctx) {
  if (g.history.empty())
    return;
  const snapshot &s = g.history.back();
  g.player = s.player;
  g.facing = s.facing;
  g.crates = s.crates;
  g.moves = s.moves;
  g.pushes = s.pushes;
  g.history.pop_back();
  g.slide = 1.0f;
  g.moving_crate = -1;
  g.solved = all_on_goals();
  camera_shake(ctx, 0.05f);
}

// The direction asked for this frame, or {0, 0}. A fresh press always
// counts; a held key repeats every repeat_time.
cell input_direction(const context &ctx) {
  struct binding {
    key_code a, b;
    cell d;
  };
  static const binding keys[] = {{key_up, key_w, {0, -1}},
                                 {key_down, key_s, {0, 1}},
                                 {key_left, key_a, {-1, 0}},
                                 {key_right, key_d, {1, 0}}};
  for (const binding &k : keys) {
    if (key_pressed(ctx, k.a) || key_pressed(ctx, k.b)) {
      g.hold = 0.0f;
      return k.d;
    }
  }
  for (const binding &k : keys) {
    if (key_held(ctx, k.a) || key_held(ctx, k.b)) {
      g.hold += delta(ctx);
      if (g.hold >= repeat_time) {
        g.hold = 0.0f;
        return k.d;
      }
      return {0, 0};
    }
  }
  g.hold = 0.0f;
  return {0, 0};
}

vec3 world_of(vec2 c) {
  return {c.x - (f32)g.w / 2.0f + 0.5f, 0.0f, c.y - (f32)g.h / 2.0f + 0.5f};
}
vec3 world_of(cell c) { return world_of(vec2{(f32)c.x, (f32)c.y}); }

f32 smooth(f32 t) { return t * t * (3.0f - 2.0f * t); }

vec2 slid(cell from, cell to) {
  const f32 t = smooth(g.slide);
  return lerp(vec2{(f32)from.x, (f32)from.y}, vec2{(f32)to.x, (f32)to.y}, t);
}

void startup(context &ctx) {
  g = game_state{};
  g.crate_texture = texture_load(ctx, "assets/crate.png");
  g.floor_tiles = instance_buffer_create(ctx, 16);
  g.wall_blocks = instance_buffer_create(ctx, 16);
  light3d_set(ctx, {.direction = {-0.5f, -1.0f, -0.35f},
                    .color = {0.85f, 0.83f, 0.78f, 1.0f},
                    .ambient = {0.34f, 0.36f, 0.46f, 1.0f},
                    .shadows = true,
                    .shadow_range = 9.0f,
                    .shadow_softness = 1.5f});
  post_fx_set(ctx, {.vignette = 0.25f, .bloom = 0.7f, .bloom_threshold = 0.8f});
  gizmos_set_visible(ctx, false);
  load_level(ctx, 0);
}

void update(context &ctx) {
  const f32 dt = delta(ctx);
  if (g.slide < 1.0f)
    g.slide = std::min(1.0f, g.slide + dt / step_time);
  g.appear += dt;
  for (f32 &f : g.crate_flash)
    f = std::max(0.0f, f - dt);
  if (key_pressed(ctx, key_r))
    load_level(ctx, g.level);
  if (key_pressed(ctx, key_z))
    undo(ctx);
  if (key_pressed(ctx, key_g))
    gizmos_set_visible(ctx, !gizmos_visible(ctx));
  gizmo_axes3d(ctx, {0.0f, 0.0f, 0.0f}, 1.0f);
  char cell_text[32];
  std::snprintf(cell_text, sizeof cell_text, "(%d, %d)", g.player.x, g.player.y);
  gizmo_text3d(ctx, world_of(g.player) + vec3{0.3f, 1.1f, 0.0f}, cell_text);
  for (i32 y = 0; y < g.h; y++) {
    for (i32 x = 0; x < g.w; x++) {
      if (g.goal[at(cell{x, y})] != 0)
        gizmo_box3d(ctx, world_of(cell{x, y}) + vec3{0.0f, 0.4f, 0.0f}, {0.9f, 0.8f, 0.9f}, goal_color);
    }
  }
  if (key_pressed(ctx, key_n) || (g.solved && key_pressed(ctx, key_enter)))
    load_level(ctx, (g.level + 1) % levels.size());
  if (key_pressed(ctx, key_p))
    load_level(ctx, (g.level + levels.size() - 1) % levels.size());
  const cell d = input_direction(ctx);
  if (!g.solved && g.slide >= 1.0f && (d.x != 0 || d.y != 0)) {
    try_move(ctx, d);
    if (g.solved) {
      screen_flash(ctx, {1.0f, 0.95f, 0.75f, 0.45f}, 0.6f);
      camera_shake(ctx, 0.25f);
      for (const cell &c : g.crates)
        particles3d_spawn(ctx, fx::sparkle(), world_of(c) + vec3{0.0f, 0.9f, 0.0f}, 30, {.scale = fx_scale});
    }
  }
}

// Crates and player come in dissolving, reversed, when a level starts.
void set_appear(context &ctx) {
  const f32 t = std::min(g.appear / appear_time, 1.0f);
  fx3d_set(ctx, {.dissolve = 1.0f - t, .edge_color = {0.6f, 0.85f, 1.0f, 1.0f}, .grain = 0.06f});
}

void render(context &ctx) {
  // Framed to the level: the bigger side sets the distance.
  const f32 size = (f32)std::max(g.w, g.h);
  const vec3 target{0.0f, 0.0f, 0.4f};
  begin_3d(ctx, {.position = target + vec3{0.0f, size * 1.05f + 1.5f, size * 0.62f + 1.0f},
                 .target = target,
                 .fovy = 45.0f});

  draw_instanced3d(ctx, mesh3d_cube, g.floor_tiles, 0, (u32)(g.w * g.h));
  draw_instanced3d(ctx, mesh3d_cube, g.wall_blocks, 0, (u32)(g.w * g.h * 2));

  // Goals: a glowing disc with a warm light above it.
  material3d_set(ctx, {.emission = goal_color, .cast_shadows = false});
  for (i32 y = 0; y < g.h; y++) {
    for (i32 x = 0; x < g.w; x++) {
      const cell c{x, y};
      if (g.goal[at(c)] == 0)
        continue;
      const vec3 p = world_of(c);
      draw_cylinder3d(ctx, p, p + vec3{0.0f, 0.02f, 0.0f}, 0.3f, goal_color);
      light3d_add(ctx, {.position = p + vec3{0.0f, 0.45f, 0.0f}, .color = goal_color, .intensity = 0.9f,
                        .radius = 1.6f});
    }
  }

  set_appear(ctx);
  material3d_set(ctx, {.specular = 0.15f, .texture = g.crate_texture});
  for (usize i = 0; i < g.crates.size(); i++) {
    const cell c = g.crates[i];
    const bool moving = (i32)i == g.moving_crate && g.slide < 1.0f;
    const vec2 at_cell = moving ? slid(g.crate_from, c) : vec2{(f32)c.x, (f32)c.y};
    const bool done = g.goal[at(c)] != 0 && !moving;
    fx3d fx = fx3d{.dissolve = std::max(0.0f, 1.0f - g.appear / appear_time),
                   .edge_color = {0.6f, 0.85f, 1.0f, 1.0f},
                   .grain = 0.06f};
    fx.flash = {1.0f, 1.0f, 0.8f, 0.8f * g.crate_flash[i] / crate_flash_time};
    fx3d_set(ctx, fx);
    draw_cube3d(ctx, world_of(at_cell) + vec3{0.0f, 0.4f, 0.0f}, {0.8f, 0.8f, 0.8f},
                done ? crate_done_color : crate_color);
  }

  // The player: a smooth SDF capsule, glossy with a cool rim, and two eyes
  // looking where it last moved.
  set_appear(ctx);
  const vec3 base = world_of(g.slide < 1.0f ? slid(g.player_from, g.player)
                                            : vec2{(f32)g.player.x, (f32)g.player.y});
  const f32 lift = std::sin(pi * g.slide) * hop;
  const vec3 center = base + vec3{0.0f, 0.48f + lift, 0.0f};
  material3d_set(ctx, {.specular = 0.7f, .shininess = 60.0f, .rim = {0.55f, 0.75f, 1.0f, 0.5f}});
  draw_shape3d(ctx, {.kind = shape3d_capsule, .position = center, .radius = 0.28f, .height = 0.92f},
               player_color);
  const vec3 face{(f32)g.facing.x, 0.0f, (f32)g.facing.y};
  const vec3 side{-face.z, 0.0f, face.x};
  material3d_set(ctx, {.specular = 1.0f, .shininess = 90.0f});
  for (f32 s : {-1.0f, 1.0f}) {
    const vec3 eye = center + vec3{0.0f, 0.2f, 0.0f} + face * 0.25f + side * (0.1f * s);
    draw_shape3d(ctx, {.kind = shape3d_sphere, .position = eye, .radius = 0.06f}, {0.08f, 0.09f, 0.12f, 1.0f});
  }
  fx3d_set(ctx, {});
  material3d_set(ctx, {});
  end_3d(ctx);
}

void render_ui(context &ctx) {
  char line[96];
  std::snprintf(line, sizeof line, "Màn %zu/%zu   Bước %d   Đẩy %d", g.level + 1, levels.size(),
                g.moves, g.pushes);
  draw_text(ctx, line, {16.0f, 14.0f}, 22.0f, colors::white);
  const vec2 screen = screen_size(ctx);
  draw_text(ctx, "Mũi tên/WASD: đi   Z: lùi   R: chơi lại   N/P: đổi màn   G: gizmo",
            {16.0f, screen.y - 30.0f}, 16.0f, {0.8f, 0.82f, 0.88f, 1.0f});
  if (g.solved) {
    const char *msg = "Xong! Enter: màn tiếp";
    const f32 size = 40.0f;
    const vec2 m = text_measure(ctx, msg, size);
    draw_text(ctx, msg, {(screen.x - m.x) / 2.0f, 60.0f}, size, goal_color);
  }
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, startup, "sokoban_startup");
  ecs_register(ctx, phase_update, update, "sokoban_update");
  ecs_register(ctx, phase_render, render, "sokoban_render");
  ecs_register(ctx, phase_post_render, render_ui, "sokoban_ui");
}
} // namespace

mod_desc sokoban_module() { return mod_desc{.name = "sokoban", .setup = setup}; }
} // namespace sokoban
