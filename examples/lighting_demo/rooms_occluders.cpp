// Rooms 5 to 8: what casts shadows. Shapes (light_occluder), shapes that move, walls from a tilemap,
// and shadows taken from the sprites themselves (light_occluder_sprite, light_occluder_pixels).
#include "demo.h"

#include <algorithm>
#include <cmath>

namespace lighting_demo {
namespace {
constexpr rgba shape_fill{0.42f, 0.40f, 0.46f, 1.0f};
constexpr rgba shape_edge{0.75f, 0.72f, 0.80f, 1.0f};

// Marks the outer circle of a ring, drawn as a band so the floor shows through its hole.
struct ring_shape {
  f32 outer, inner;
};

// Draws every light_occluder of the room that has no picture of its own (not a sprite, not a wall).
void draw_all_shapes(context &ctx) {
  entt::registry &reg = world(ctx);
  for (auto [e, o, t] : reg.view<const light_occluder, const transform>().each()) {
    if (reg.all_of<sprite>(e) || o.hole)
      continue;
    if (const ring_shape *ring = reg.try_get<ring_shape>(e)) {
      constexpr i32 n = 32;
      for (i32 i = 0; i < n; i++) {
        const vec2 a = from_angle(360.0f * (f32)i / n), b = from_angle(360.0f * (f32)(i + 1) / n);
        draw_triangle(ctx, t.pos + a * ring->outer, t.pos + b * ring->outer, t.pos + a * ring->inner, shape_fill);
        draw_triangle(ctx, t.pos + b * ring->outer, t.pos + b * ring->inner, t.pos + a * ring->inner, shape_fill);
      }
      draw_circle_lines(ctx, t.pos, ring->outer, 1.0f, shape_edge);
      draw_circle_lines(ctx, t.pos, ring->inner, 1.0f, shape_edge);
      continue;
    }
    draw_occluder_shape(ctx, o, t, shape_fill, shape_edge);
  }
}

// A light that follows the mouse; the wheel changes its size.
struct mouse_light {
  entt::entity e = entt::null;
  i32 label = -1;

  void make(context &ctx, f32 radius, f32 size) {
    e = add_light(ctx, mouse_world(ctx), {.temperature = 3400.0f, .intensity = 3.0f, .radius = radius, .size = size,
                                          .falloff = falloff_smooth});
    add_label({0.0f, 0.0f}, "");
    label = (i32)demo.labels.size() - 1;
  }
  void update(context &ctx) {
    entt::registry &reg = world(ctx);
    light_2d &l = reg.get<light_2d>(e);
    l.size = std::clamp(l.size + mouse_wheel(ctx) * 2.0f, 0.0f, 60.0f);
    const vec2 at = mouse_world(ctx);
    reg.get<transform>(e).pos = at;
    demo.labels[label].at = at + vec2{0.0f, 8.0f};
    demo.labels[label].text = fmt("size = %.0f", l.size);
  }
};

// --- 5. the shapes of light_occluder ---

mouse_light shapes_light;

// A star: a closed polygon does not need to be convex.
light_occluder star(f32 outer, f32 inner, i32 points) {
  light_occluder o;
  for (i32 i = 0; i < points * 2; i++) {
    const f32 r = i % 2 == 0 ? outer : inner;
    o.points.push_back(from_angle(-90.0f + 180.0f * (f32)i / (f32)points) * r);
  }
  o.reach = outer; // the farthest point from the anchor; 0 lets the engine measure it every frame
  return o;
}

// A ring: a closed shape, and a hole inside it (hole = true): the inside is open, light passes there.
void add_ring(context &ctx, vec2 at, f32 outer, f32 inner) {
  const entt::entity e = add_occluder(ctx, at, light_occluder_circle(outer, {}, 20));
  world(ctx).emplace<ring_shape>(e, ring_shape{outer, inner});
  light_occluder hole = light_occluder_circle(inner, {}, 20);
  hole.hole = true;
  add_occluder(ctx, at, std::move(hole));
}

void shapes_build(context &ctx) {
  fill_floor(ctx, t_stone, t_stone2);
  const f32 y1 = 140.0f, y2 = 270.0f;
  add_occluder(ctx, {80.0f, y1}, light_occluder_box({28.0f, 20.0f}));
  add_label({80.0f, y1 + 22.0f}, "light_occluder_box");
  add_occluder(ctx, {240.0f, y1}, light_occluder_circle(14.0f));
  add_label({240.0f, y1 + 22.0f}, "light_occluder_circle");
  add_occluder(ctx, {400.0f, y1}, light_occluder_ellipse({22.0f, 9.0f}));
  add_label({400.0f, y1 + 22.0f}, "light_occluder_ellipse");
  add_occluder(ctx, {560.0f, y1}, light_occluder_capsule({-14.0f, 6.0f}, {14.0f, -6.0f}, 6.0f));
  add_label({560.0f, y1 + 22.0f}, "light_occluder_capsule");
  add_occluder(ctx, {80.0f, y2}, star(18.0f, 8.0f, 5));
  add_label({80.0f, y2 + 24.0f}, "points (không lồi)");
  add_occluder(ctx, {240.0f, y2}, light_occluder_line({{-24.0f, 10.0f}, {-8.0f, -12.0f}, {8.0f, 10.0f}, {24.0f, -12.0f}}));
  add_label({240.0f, y2 + 24.0f}, "light_occluder_line (mở)");
  add_ring(ctx, {400.0f, y2}, 24.0f, 16.0f);
  add_label({400.0f, y2 + 30.0f}, "vòng + hole = true");
  // Rotated: the transform turns the shape.
  add_occluder(ctx, {560.0f, y2}, light_occluder_box({40.0f, 8.0f}), 30.0f);
  add_label({560.0f, y2 + 24.0f}, "transform.rot = 30");
  shapes_light.make(ctx, 260.0f, 6.0f);
  lighting_set(ctx, base_lighting());
}

void shapes_update(context &ctx) { shapes_light.update(ctx); }

void shapes_draw(context &ctx) { draw_all_shapes(ctx); }

// --- 6. occluders that move, turn, grow and change shape ---

struct moving_room {
  entt::entity spinner = entt::null;
  entt::entity orbiters[3]{};
  entt::entity pulse = entt::null;
  entt::entity door = entt::null;
  f32 time = 0.0f;
} moving;

void moving_build(context &ctx) {
  fill_floor(ctx, t_wood);
  moving.time = 0.0f;
  // Slembcke's demo: a spinning box between two coloured lights. Light adds up, so where both reach the
  // floor is magenta and each shadow is tinted by the other light.
  moving.spinner = add_occluder(ctx, {200.0f, 210.0f}, light_occluder_box({40.0f, 40.0f}));
  add_light(ctx, {110.0f, 170.0f}, {.color = {1.0f, 0.25f, 0.2f, 1.0f}, .intensity = 9.0f, .radius = 260.0f, .size = 10.0f});
  add_light(ctx, {290.0f, 260.0f}, {.color = {0.25f, 0.45f, 1.0f, 1.0f}, .intensity = 9.0f, .radius = 260.0f, .size = 10.0f});
  add_label({200.0f, 330.0f}, "transform.rot");
  // Circles orbiting a lamp.
  add_light(ctx, {470.0f, 210.0f}, {.temperature = 3000.0f, .intensity = 10.0f, .radius = 200.0f, .size = 4.0f});
  for (entt::entity &e : moving.orbiters)
    e = add_occluder(ctx, {470.0f, 210.0f}, light_occluder_circle(7.0f, {}, 12));
  add_label({470.0f, 150.0f}, "transform.pos");
  // Growing and shrinking: transform.scale.
  moving.pulse = add_occluder(ctx, {560.0f, 300.0f}, star(14.0f, 6.0f, 4));
  add_label({560.0f, 326.0f}, "transform.scale");
  // A door: its points are rewritten every frame, the shadow follows at once.
  moving.door = add_occluder(ctx, {420.0f, 130.0f}, light_occluder_line({{0.0f, 0.0f}, {40.0f, 0.0f}}));
  add_label({440.0f, 100.0f}, "sửa points mỗi frame");
  lighting_desc d = base_lighting();
  d.ambient = {0.06f, 0.06f, 0.08f, 1.0f};
  lighting_set(ctx, d);
}

void moving_update(context &ctx) {
  entt::registry &reg = world(ctx);
  moving.time += delta(ctx);
  const f32 t = moving.time;
  reg.get<transform>(moving.spinner).rot = t * 35.0f;
  for (i32 i = 0; i < 3; i++)
    reg.get<transform>(moving.orbiters[i]).pos = vec2{470.0f, 210.0f} + from_angle(t * 50.0f + 120.0f * (f32)i) * 44.0f;
  reg.get<transform>(moving.pulse).scale = 1.0f + 0.6f * std::sin(t * 2.0f);
  // The door swings between shut (flat) and open (turned 80 degrees about its hinge).
  const f32 open = 0.5f + 0.5f * std::sin(t * 1.3f);
  std::vector<vec2> &p = reg.get<light_occluder>(moving.door).points;
  p[1] = from_angle(-80.0f * open) * 40.0f;
}

void moving_draw(context &ctx) { draw_all_shapes(ctx); }

// --- 7. walls from a tilemap, and a top-down hero with a torch ---

struct dungeon_room {
  entt::entity walls = entt::null;
  entt::entity hero = entt::null;
  entt::entity torch = entt::null;
} dungeon;

void dungeon_build(context &ctx) {
  fill_floor(ctx, t_stone, t_stone2);
  // The first four rows are under the text. '#' is a wall; the rest is open floor.
  dungeon.walls = build_walls(ctx, {
                                       "",
                                       "",
                                       "",
                                       "",
                                       "########################################",
                                       "#..........#..........#................#",
                                       "#..........#..........#................#",
                                       "#..........#....##....#.....#####......#",
                                       "#..........#....##....#.....#...#......#",
                                       "#...........................#...#......#",
                                       "#..........#..........#.....##.##......#",
                                       "#####.######..........###.##############",
                                       "#..........#..........#................#",
                                       "#..........#...####...#................#",
                                       "#...##.....#...#..#...#.....#.#.#.#....#",
                                       "#...##.........#..#....................#",
                                       "#..........#...##.#...#.....#.#.#.#....#",
                                       "#..........#..........#................#",
                                       "#..........#..........#................#",
                                       "#..........#..........#................#",
                                       "#..........#..........#................#",
                                       "########################################",
                                   });
  entt::registry &reg = world(ctx);
  dungeon.hero = add_sprite(ctx, {90.0f, 140.0f}, demo.img.hero, demo.img.hero_n, demo.img.hero_m);
  reg.emplace<collider>(dungeon.hero, collider{.size = {8.0f, 5.0f}, .offset = {0.0f, -2.5f}});
  reg.emplace<topdown_body>(dungeon.hero, topdown_body{.speed = 100.0f});
  // The hero casts a shadow too. The torch they carry sits inside this shape, so it is not blocked by it.
  reg.emplace<light_occluder>(dungeon.hero, light_occluder_capsule({0.0f, -3.0f}, {0.0f, -10.0f}, 3.0f));
  dungeon.torch = add_light(ctx, {}, {.temperature = 2200.0f, .intensity = 11.0f, .radius = 190.0f, .size = 8.0f, .height = 24.0f});
  // Braziers in some of the rooms.
  for (const vec2 at : {vec2{280.0f, 300.0f}, vec2{560.0f, 140.0f}, vec2{500.0f, 300.0f}})
    add_light(ctx, at, {.temperature = 1900.0f, .intensity = 7.0f, .radius = 140.0f, .size = 6.0f});
  lighting_desc d = base_lighting();
  d.ambient = {0.03f, 0.03f, 0.05f, 1.0f};
  lighting_set(ctx, d);
}

void dungeon_update(context &ctx) {
  entt::registry &reg = world(ctx);
  vec2 move{};
  move.x = (key_held(ctx, key_d) || key_held(ctx, key_right) ? 1.0f : 0.0f) - (key_held(ctx, key_a) || key_held(ctx, key_left) ? 1.0f : 0.0f);
  move.y = (key_held(ctx, key_s) || key_held(ctx, key_down) ? 1.0f : 0.0f) - (key_held(ctx, key_w) || key_held(ctx, key_up) ? 1.0f : 0.0f);
  reg.get<topdown_body>(dungeon.hero).input.move = move;
  const vec2 hero_at = reg.get<transform>(dungeon.hero).pos;
  reg.get<transform>(dungeon.torch).pos = hero_at - vec2{0.0f, 7.0f};
  const f32 t = elapsed(ctx);
  reg.get<light_2d>(dungeon.torch).intensity = 11.0f + 0.8f * std::sin(t * 12.0f) + 0.4f * std::sin(t * 27.0f);

  // A click digs a wall away or builds one; the occluders are rebuilt from the tiles.
  if (mouse_pressed(ctx, mouse_left)) {
    tilemap &map = reg.get<tilemap>(dungeon.walls);
    const cell c = tilemap_cell_at(map, {}, mouse_world(ctx));
    if (c.y >= 5 && c.y < 21 && c.x > 0 && c.x < 39) {
      tilemap_set(map, c.x, c.y, tilemap_get(map, c.x, c.y) >= 0 ? -1 : t_brick);
      rebuild_wall_occluders(ctx, dungeon.walls);
    }
  }
}

// --- 8. shadows from the sprites themselves ---

struct sprites_room {
  entt::entity lights[3]{};
  f32 time = 0.0f;
} sprites;

void sprites_build(context &ctx) {
  fill_floor(ctx, t_grass);
  entt::registry &reg = world(ctx);
  rng &r = random(ctx);
  r.reseed(21);
  // Left: light_occluder_sprite. The outline of the sprite's opaque pixels is traced once per frame of
  // the picture and becomes an occluder like any other (simplify smooths the staircase of pixels).
  // Middle: light_occluder_pixels. The pixels themselves block light: every hole and leaf counts, and the
  // cost does not grow with the number of occluders.
  // Right: light_occluder_pixels with a mask: only the trunk blocks light, the leaves let it through.
  for (i32 column = 0; column < 3; column++) {
    for (i32 i = 0; i < 7; i++) {
      const vec2 at{column * 213.0f + r.range(24.0f, 190.0f), r.range(110.0f, 330.0f)};
      const bool tree = i % 3 != 0 || column == 2;
      const entt::entity e = add_sprite(ctx, at, tree ? demo.img.tree : (i % 2 ? demo.img.rock : demo.img.crystal),
                                        tree ? demo.img.tree_n : demo.img.rock_n);
      if (column == 0)
        reg.emplace<light_occluder_sprite>(e, light_occluder_sprite{.alpha = 0.5f, .simplify = 1.0f});
      else if (column == 1)
        reg.emplace<light_occluder_pixels>(e);
      else
        reg.emplace<light_occluder_pixels>(e, light_occluder_pixels{.mask = demo.img.tree_trunk});
    }
  }
  add_label({107.0f, 340.0f}, "light_occluder_sprite");
  add_label({320.0f, 340.0f}, "light_occluder_pixels");
  add_label({533.0f, 340.0f}, "light_occluder_pixels + mask");
  // One light circling in each column, and thin walls between the columns so each shows one method.
  for (i32 column = 0; column < 3; column++)
    sprites.lights[column] = add_light(ctx, {}, {.temperature = 3800.0f, .intensity = 3.0f, .radius = 170.0f, .size = 6.0f,
                                                 .falloff = falloff_smooth});
  for (const f32 x : {213.0f, 427.0f})
    add_occluder(ctx, {x, 0.0f}, light_occluder_line({{0.0f, room_top}, {0.0f, room_size.y}}));
  add_label({320.0f, 76.0f}, "");
  lighting_set(ctx, base_lighting());
}

void sprites_update(context &ctx) {
  entt::registry &reg = world(ctx);
  sprites.time += delta(ctx);
  for (i32 column = 0; column < 3; column++) {
    light_2d &l = reg.get<light_2d>(sprites.lights[column]);
    l.size = std::clamp(l.size + mouse_wheel(ctx) * 2.0f, 0.0f, 40.0f);
    reg.get<transform>(sprites.lights[column]).pos = vec2{106.0f + 213.0f * (f32)column, 220.0f} + from_angle(sprites.time * 30.0f) * vec2{60.0f, 50.0f};
  }
  demo.labels.back().text = fmt("size = %.0f", reg.get<light_2d>(sprites.lights[0]).size);
}
} // namespace

const room rooms_occluders[4] = {
    {"Các hình vật chắn (light_occluder)",
     "Vật chắn là đa giác gắn vào entity có transform. Đa giác đóng là vật đặc: điểm bên trong không bị chính nó che,\n"
     "đèn nằm trong nó cũng không bị chặn. Đường mở (closed = false) mỏng, chắn cả hai phía. hole = true khoét lỗ trong khối đặc.",
     "add_occluder(ctx, at, light_occluder_capsule({-14, 6}, {14, -6}, 6));   light_occluder{points, closed, hole, reach}",
     "Chuột: đèn   Lăn chuột: size của đèn", shapes_build, shapes_update, shapes_draw},
    {"Vật chắn chuyển động",
     "Bóng đi theo transform mỗi frame: xoay (rot), dời (pos), co giãn (scale). Sửa points cũng được, bóng theo ngay.\n"
     "Hai đèn màu cộng vào nhau: bóng của hộp dưới đèn đỏ vẫn có màu của đèn xanh, và ngược lại.",
     "reg.get<transform>(box).rot = t * 35;   reg.get<light_occluder>(door).points[1] = from_angle(-80 * open) * 40;",
     "", moving_build, moving_update, moving_draw},
    {"Tường từ tilemap, nhân vật cầm đuốc",
     "light_occluders_from_tiles() viền các ô đặc thành vài vòng đa giác (nối các cạnh thẳng hàng, tự đánh dấu lỗ).\n"
     "Nhân vật có vật chắn hình viên thuốc; đuốc nằm trong đó nên không bị chính nhân vật chặn. Đổi ô thì tính lại vật chắn.",
     "for (auto &o : light_occluders_from_tiles(map, [](i32 id) { return id >= 0; })) add_occluder(ctx, origin, o);",
     "WASD: đi   Click: đào / xây tường", dungeon_build, dungeon_update, nullptr},
    {"Bóng lấy từ chính sprite",
     "light_occluder_sprite dò viền pixel đục của frame đang hiện thành đa giác (nhớ lại cho lần sau).\n"
     "light_occluder_pixels đổ bóng theo đúng từng pixel; mask chọn pixel nào chắn sáng (ở đây: chỉ thân cây).",
     "reg.emplace<light_occluder_sprite>(e, {.alpha = 0.5f, .simplify = 1});   reg.emplace<light_occluder_pixels>(e, {.mask = trunk});",
     "Lăn chuột: size của đèn", sprites_build, sprites_update, nullptr},
};
} // namespace lighting_demo
