#include "demo.h"

#include <algorithm>
#include <cmath>

namespace lighting_demo {
demo_state demo;

namespace {
texture_handle load(njin_ctx &ctx, const char *path) {
  const texture_handle t = texture_load(ctx, path);
  texture_set_filter(ctx, t, filter_nearest); // pixel art
  return t;
}
} // namespace

void load_images(njin_ctx &ctx) {
  images &i = demo.img;
  i.tiles = load(ctx, "assets/tiles.png");
  i.tree = load(ctx, "assets/sprites/tree.png");
  i.tree_n = load(ctx, "assets/sprites/tree_n.png");
  i.tree_trunk = load(ctx, "assets/sprites/tree_t.png");
  i.rock = load(ctx, "assets/sprites/rock.png");
  i.rock_n = load(ctx, "assets/sprites/rock_n.png");
  i.rock_m = load(ctx, "assets/sprites/rock_m.png");
  i.crate = load(ctx, "assets/sprites/crate.png");
  i.crate_n = load(ctx, "assets/sprites/crate_n.png");
  i.crate_m = load(ctx, "assets/sprites/crate_m.png");
  i.ball = load(ctx, "assets/sprites/ball.png");
  i.ball_n = load(ctx, "assets/sprites/ball_n.png");
  i.ball_m[0] = load(ctx, "assets/sprites/ball_m_rough.png");
  i.ball_m[1] = load(ctx, "assets/sprites/ball_m_glossy.png");
  i.ball_m[2] = load(ctx, "assets/sprites/ball_m_brushed.png");
  i.ball_m[3] = load(ctx, "assets/sprites/ball_m_mirror.png");
  i.gold = load(ctx, "assets/sprites/gold.png");
  i.crystal = load(ctx, "assets/sprites/crystal.png");
  i.crystal_n = load(ctx, "assets/sprites/crystal_n.png");
  i.crystal_e = load(ctx, "assets/sprites/crystal_e.png");
  i.hero = load(ctx, "assets/sprites/hero.png");
  i.hero_n = load(ctx, "assets/sprites/hero_n.png");
  i.hero_m = load(ctx, "assets/sprites/hero_m.png");
  i.lamp = load(ctx, "assets/sprites/lamp.png");
  i.lamp_e = load(ctx, "assets/sprites/lamp_e.png");
}

entt::entity spawn(njin_ctx &ctx, vec2 pos, f32 rot, f32 scale) {
  entt::registry &reg = world(ctx);
  const entt::entity e = reg.create();
  reg.emplace<transform>(e, transform{.pos = pos, .rot = rot, .scale = scale});
  reg.emplace<room_entity>(e);
  return e;
}

entt::entity add_light(njin_ctx &ctx, vec2 pos, const light_2d &light) {
  const entt::entity e = spawn(ctx, pos);
  world(ctx).emplace<light_2d>(e, light);
  return e;
}

entt::entity add_occluder(njin_ctx &ctx, vec2 pos, light_occluder shape, f32 rot) {
  const entt::entity e = spawn(ctx, pos, rot);
  world(ctx).emplace<light_occluder>(e, std::move(shape));
  return e;
}

entt::entity add_sprite(njin_ctx &ctx, vec2 pos, texture_handle texture, texture_handle normal, texture_handle material) {
  const entt::entity e = spawn(ctx, pos);
  world(ctx).emplace<sprite>(e, sprite{.texture = texture, .origin = {0.5f, 1.0f}, .layer = layer_things,
                                       .normal = normal, .material = material});
  return e;
}

entt::entity fill_floor(njin_ctx &ctx, i32 id, i32 id2) {
  tilemap map{};
  map.tileset = demo.img.tiles;
  map.tile_size = tile;
  map.layer = layer_floor;
  const i32 w = (i32)(room_size.x / tile.x), h = (i32)std::ceil(room_size.y / tile.y);
  for (i32 y = 0; y < h; y++)
    for (i32 x = 0; x < w; x++)
      tilemap_set(map, x, y, id2 >= 0 && (x + y) % 2 == 1 ? id2 : id);
  const entt::entity e = spawn(ctx, {});
  world(ctx).emplace<tilemap>(e, std::move(map));
  return e;
}

// Walls block light twice over: the tilemap's outline becomes light_occluder shapes (one closed loop per
// block of wall, holes marked), and the same tilemap is solid for bodies (collider_tiles).
entt::entity build_walls(njin_ctx &ctx, std::initializer_list<std::string_view> rows, i32 wall_tile) {
  tilemap map{};
  map.tileset = demo.img.tiles;
  map.tile_size = tile;
  map.layer = layer_walls;
  tilemap_from_rows(map, rows, {{'#', wall_tile}});
  const entt::entity e = spawn(ctx, {});
  entt::registry &reg = world(ctx);
  reg.emplace<tilemap>(e, std::move(map));
  reg.emplace<collider>(e, collider{.shape = collider_tiles});
  rebuild_wall_occluders(ctx, e);
  return e;
}

namespace {
// The occluders made from a wall tilemap sit on their own entities, remembered here.
struct wall_occluder {
  entt::entity walls = entt::null;
};
} // namespace

void rebuild_wall_occluders(njin_ctx &ctx, entt::entity walls) {
  entt::registry &reg = world(ctx);
  std::vector<entt::entity> old;
  for (auto [e, w] : reg.view<const wall_occluder>().each())
    if (w.walls == walls)
      old.push_back(e);
  reg.destroy(old.begin(), old.end());
  const vec2 origin = reg.get<transform>(walls).pos;
  // Every non-empty tile blocks light. A game with see-through tiles (glass, grass) says which ids do.
  for (light_occluder &o : light_occluders_from_tiles(reg.get<tilemap>(walls), [](i32 id) { return id >= 0; })) {
    const entt::entity e = add_occluder(ctx, origin, std::move(o));
    reg.emplace<wall_occluder>(e, wall_occluder{walls});
  }
}

lighting_desc base_lighting() {
  lighting_desc d{};
  d.enabled = demo.lit;
  d.ambient = {0.10f, 0.11f, 0.18f, 1.0f};
  return d;
}

void add_label(vec2 at, std::string text, rgba color) { demo.labels.push_back({at, std::move(text), color}); }

vec2 mouse_world(njin_ctx &ctx) { return scr2w(ctx, mouse_pos(ctx)); }

void draw_occluder_shape(njin_ctx &ctx, const light_occluder &o, const transform &t, rgba fill, rgba edge) {
  if (o.points.size() < 2)
    return;
  const f32 a = t.rot * (3.14159265f / 180.0f);
  const f32 c = std::cos(a), s = std::sin(a);
  std::vector<vec2> p;
  for (const vec2 q : o.points)
    p.push_back(t.pos + vec2{(q.x * c - q.y * s) * t.scale, (q.x * s + q.y * c) * t.scale});
  if (o.closed && p.size() >= 3) {
    vec2 centre{};
    for (const vec2 q : p)
      centre = centre + q;
    centre = centre * (1.0f / (f32)p.size());
    for (usize i = 0; i < p.size(); i++)
      draw_triangle(ctx, centre, p[i], p[(i + 1) % p.size()], fill);
  }
  const usize n = o.closed ? p.size() : p.size() - 1;
  for (usize i = 0; i < n; i++)
    draw_line(ctx, p[i], p[(i + 1) % p.size()], o.closed ? 1.0f : 3.0f, edge);
}

const room &room_at(i32 index) {
  if (index < 4)
    return rooms_lights[index];
  if (index < 8)
    return rooms_occluders[index - 4];
  return rooms_surfaces[index - 8];
}
} // namespace lighting_demo
