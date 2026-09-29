// Rooms 9 to 12: how surfaces answer the light (normal, material and emissive maps), colour and HDR,
// many lights at once, and the same lighting in a side view platformer level.
#include "demo.h"

#include <algorithm>
#include <cmath>

namespace lighting_demo {
namespace {
// --- 9. PBR maps: normal, metallic, roughness, ambient occlusion, emissive ---

struct pbr_room {
  entt::entity light = entt::null;
  bool normals = true;
  bool materials = true;
  // What each sprite had, to put back when the keys turn the maps on again.
  struct maps {
    entt::entity e;
    texture_handle normal, material;
  };
  std::vector<maps> saved;
} pbr;

entt::entity pbr_sprite(context &ctx, vec2 at, f32 scale, texture_handle tex, texture_handle normal, texture_handle material,
                        const char *name) {
  const entt::entity e = add_sprite(ctx, at, tex, normal, material);
  world(ctx).get<transform>(e).scale = scale;
  pbr.saved.push_back({e, normal, material});
  add_label(at + vec2{0.0f, 4.0f}, name);
  return e;
}

void pbr_build(context &ctx) {
  fill_floor(ctx, t_stone, t_stone2);
  pbr.saved.clear();
  const images &i = demo.img;
  // The four corners of the material model, on the same white ball with the same normal map.
  constexpr const char *balls[4] = {"nhám (rough 0.9)", "bóng (rough 0.2)", "kim loại xước", "kim loại gương"};
  for (i32 k = 0; k < 4; k++)
    pbr_sprite(ctx, {100.0f + 146.0f * (f32)k, 170.0f}, 3.0f, i.ball, i.ball_n, i.ball_m[k], balls[k]);
  // Metal reflects in its own colour.
  pbr_sprite(ctx, {90.0f, 300.0f}, 2.5f, i.gold, i.ball_n, i.ball_m[2], "vàng (metallic)");
  // The same crate and rock, with and without their maps.
  pbr_sprite(ctx, {200.0f, 300.0f}, 3.0f, i.crate, i.crate_n, i.crate_m, "normal + material");
  pbr_sprite(ctx, {290.0f, 300.0f}, 3.0f, i.crate, {}, {}, "phẳng");
  pbr_sprite(ctx, {390.0f, 300.0f}, 3.0f, i.rock, i.rock_n, i.rock_m, "normal + material");
  pbr_sprite(ctx, {470.0f, 300.0f}, 3.0f, i.rock, {}, {}, "phẳng");
  // It glows without any light: the emissive map, times emissive_power.
  const entt::entity crystal = pbr_sprite(ctx, {565.0f, 300.0f}, 3.0f, i.crystal, i.crystal_n, {}, "emissive");
  sprite &s = world(ctx).get<sprite>(crystal);
  s.emissive = i.crystal_e;
  s.emissive_power = 2.0f;
  pbr.light = add_light(ctx, {}, {.temperature = 4500.0f, .intensity = 4.0f, .radius = 320.0f, .size = 10.0f,
                                  .falloff = falloff_smooth, .height = 40.0f});
  add_label({}, "");
  lighting_desc d = base_lighting();
  d.ambient = {0.16f, 0.16f, 0.20f, 1.0f};
  lighting_set(ctx, d);
}

void pbr_update(context &ctx) {
  entt::registry &reg = world(ctx);
  light_2d &l = reg.get<light_2d>(pbr.light);
  // Height over the scene: a low light grazes the surfaces and brings out the relief of the normal maps.
  l.height = std::clamp(l.height + mouse_wheel(ctx) * 5.0f, 2.0f, 300.0f);
  const vec2 at = mouse_world(ctx);
  reg.get<transform>(pbr.light).pos = at;
  demo.labels.back().at = at + vec2{0.0f, 8.0f};
  demo.labels.back().text = fmt("height = %.0f", l.height);
  if (key_pressed(ctx, key_n))
    pbr.normals = !pbr.normals;
  if (key_pressed(ctx, key_m))
    pbr.materials = !pbr.materials;
  for (const pbr_room::maps &m : pbr.saved) {
    sprite &s = reg.get<sprite>(m.e);
    s.normal = pbr.normals ? m.normal : texture_handle{};
    s.material = pbr.materials ? m.material : texture_handle{};
  }
}

// --- 10. colour: temperature, mixing, HDR and tonemapping ---

struct colour_room {
  i32 tonemap = 0;
  f32 exposure = 1.0f;
  entt::entity rgb[3]{};
} colour;

void colour_build(context &ctx) {
  fill_floor(ctx, t_stone, t_stone2);
  // Colour temperature: the colour of a glowing black body, from candle to blue sky.
  constexpr f32 kelvin[6] = {1900.0f, 2700.0f, 4000.0f, 5500.0f, 6500.0f, 10000.0f};
  for (i32 k = 0; k < 6; k++) {
    const vec2 at{60.0f + 104.0f * (f32)k, 140.0f};
    add_light(ctx, at, {.temperature = kelvin[k], .intensity = 5.0f, .radius = 60.0f, .size = 10.0f});
    add_label(at + vec2{0.0f, 46.0f}, fmt("%.0f K", kelvin[k]));
  }
  // Red, green and blue add up to white where all three reach.
  constexpr rgba rgb[3] = {{1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}};
  for (i32 k = 0; k < 3; k++)
    colour.rgb[k] = add_light(ctx, {}, {.color = rgb[k], .intensity = 5.0f, .radius = 90.0f, .size = 6.0f, .falloff = falloff_smooth});
  add_label({190.0f, 336.0f}, "đỏ + xanh lá + xanh dương");
  // Far brighter than white: HDR. The tonemap decides how it rolls off to the screen.
  add_light(ctx, {470.0f, 270.0f}, {.temperature = 3500.0f, .intensity = 40.0f, .radius = 120.0f, .size = 12.0f});
  add_label({470.0f, 336.0f}, "intensity = 40 (HDR)");
  add_label({470.0f, 220.0f}, "");
  colour.exposure = 1.0f;
}

void colour_update(context &ctx) {
  entt::registry &reg = world(ctx);
  const f32 t = elapsed(ctx);
  for (i32 k = 0; k < 3; k++)
    reg.get<transform>(colour.rgb[k]).pos = vec2{190.0f, 270.0f} + from_angle(t * 30.0f + 120.0f * (f32)k) * 34.0f;
  if (key_pressed(ctx, njin::key_t))
    colour.tonemap = (colour.tonemap + 1) % 3;
  colour.exposure = std::clamp(colour.exposure + mouse_wheel(ctx) * 0.1f, 0.1f, 4.0f);
  lighting_desc d = base_lighting();
  d.ambient = {0.04f, 0.04f, 0.05f, 1.0f};
  d.tonemap = (light_tonemap)colour.tonemap;
  d.exposure = colour.exposure;
  lighting_set(ctx, d);
  constexpr const char *names[3] = {"tonemap_shoulder", "tonemap_reinhard", "tonemap_aces"};
  demo.labels.back().text = fmt("%s   exposure = %.1f", names[colour.tonemap], colour.exposure);
}

// --- 11. many lights ---

struct swarm_room {
  struct firefly {
    entt::entity e;
    vec2 velocity;
  };
  std::vector<firefly> flies;
  i32 wanted = 48;
  bool shadows = true;
} swarm;

void swarm_set(context &ctx, i32 wanted) {
  entt::registry &reg = world(ctx);
  rng &r = random(ctx);
  // The engine draws at most 64 lights a frame (the ones nearest the middle of the view); more are skipped.
  swarm.wanted = std::clamp(wanted, 0, 64);
  while ((i32)swarm.flies.size() < swarm.wanted) {
    const rgba c = light_color_kelvin(r.range(1800.0f, 12000.0f));
    const entt::entity e = add_light(ctx, r.point_in(rect{{16.0f, room_top + 16.0f}, {608.0f, 264.0f}}),
                                     {.color = c, .intensity = 0.6f, .radius = 56.0f, .size = 2.0f, .falloff = falloff_smooth});
    swarm.flies.push_back({e, r.direction() * r.range(20.0f, 60.0f)});
  }
  while ((i32)swarm.flies.size() > swarm.wanted) {
    reg.destroy(swarm.flies.back().e);
    swarm.flies.pop_back();
  }
}

void swarm_build(context &ctx) {
  fill_floor(ctx, t_wood);
  swarm.flies.clear();
  for (i32 y = 0; y < 3; y++)
    for (i32 x = 0; x < 7; x++)
      add_occluder(ctx, {60.0f + 86.0f * (f32)x, 130.0f + 86.0f * (f32)y}, light_occluder_box({14.0f, 14.0f}));
  swarm_set(ctx, swarm.wanted);
  lighting_desc d = base_lighting();
  d.ambient = {0.03f, 0.03f, 0.05f, 1.0f};
  lighting_set(ctx, d);
}

void swarm_update(context &ctx) {
  entt::registry &reg = world(ctx);
  if (key_pressed(ctx, key_equal))
    swarm_set(ctx, swarm.wanted + 8);
  if (key_pressed(ctx, key_minus))
    swarm_set(ctx, swarm.wanted - 8);
  if (key_pressed(ctx, key_o))
    swarm.shadows = !swarm.shadows;
  const f32 dt = delta(ctx);
  for (swarm_room::firefly &f : swarm.flies) {
    vec2 &p = reg.get<transform>(f.e).pos;
    p += f.velocity * dt;
    if (p.x < 8.0f || p.x > room_size.x - 8.0f)
      f.velocity.x = -f.velocity.x;
    if (p.y < room_top + 8.0f || p.y > room_size.y - 8.0f)
      f.velocity.y = -f.velocity.y;
    p.x = std::clamp(p.x, 8.0f, room_size.x - 8.0f);
    p.y = std::clamp(p.y, room_top + 8.0f, room_size.y - 8.0f);
    reg.get<light_2d>(f.e).cast_shadows = swarm.shadows;
  }
}

void swarm_draw(context &ctx) {
  entt::registry &reg = world(ctx);
  for (auto [e, o, t] : reg.view<const light_occluder, const transform>().each())
    draw_rect(ctx, rect{t.pos - vec2{7.0f, 7.0f}, {14.0f, 14.0f}}, {0.45f, 0.42f, 0.40f, 1.0f});
  // The fireflies themselves: a dot where each light is.
  for (const swarm_room::firefly &f : swarm.flies)
    draw_circle(ctx, reg.get<transform>(f.e).pos, 1.5f, reg.get<light_2d>(f.e).color);
}

// --- 12. a side view level ---

struct side_room {
  entt::entity hero = entt::null;
  entt::entity lantern = entt::null;
} side;

void side_build(context &ctx) {
  entt::registry &reg = world(ctx);
  fill_floor(ctx, t_back);
  // The same helper as the dungeon: the ground is a tilemap, solid for the body and outlined into occluders.
  const entt::entity ground = build_walls(ctx,
                                          {
                                              "",
                                              "",
                                              "",
                                              "",
                                              "#......................................#",
                                              "#......................................#",
                                              "#......................................#",
                                              "#......................................#",
                                              "#......................................#",
                                              "#...........########...................#",
                                              "#......................................#",
                                              "#...................................####",
                                              "#.....######...........................#",
                                              "#.........................########.....#",
                                              "#......................................#",
                                              "#................####..................#",
                                              "####...................................#",
                                              "#............................####......#",
                                              "#.........######.......................#",
                                              "#......................................#",
                                              "########################################",
                                              "########################################",
                                          },
                                          t_dirt);
  // Grass on the tiles that have air above them.
  tilemap &map = reg.get<tilemap>(ground);
  for (i32 y = 5; y < 23; y++)
    for (i32 x = 0; x < 40; x++)
      if (tilemap_get(map, x, y) == t_dirt && tilemap_get(map, x, y - 1) < 0)
        tilemap_set(map, x, y, t_dirt_top);

  side.hero = add_sprite(ctx, {60.0f, 300.0f}, demo.img.hero, demo.img.hero_n, demo.img.hero_m);
  reg.emplace<collider>(side.hero, collider{.size = {8.0f, 14.0f}, .offset = {0.0f, -7.0f}});
  reg.emplace<platformer_body>(side.hero, platformer_body{});
  reg.emplace<light_occluder>(side.hero, light_occluder_box({8.0f, 14.0f}, {0.5f, 1.0f}));
  side.lantern = add_light(ctx, {}, {.temperature = 2400.0f, .intensity = 3.0f, .radius = 150.0f, .size = 5.0f,
                                     .falloff = falloff_smooth, .height = 16.0f});
  // Lamp posts: a sprite that glows (emissive) and a light at its lamp.
  for (const vec2 at : {vec2{250.0f, 320.0f}, vec2{560.0f, 320.0f}, vec2{140.0f, 192.0f}}) {
    const entt::entity lamp = add_sprite(ctx, at, demo.img.lamp);
    sprite &s = reg.get<sprite>(lamp);
    s.emissive = demo.img.lamp_e;
    s.emissive_power = 3.0f;
    add_light(ctx, at - vec2{0.0f, 29.0f}, {.temperature = 2000.0f, .intensity = 3.0f, .radius = 130.0f, .size = 4.0f,
                                            .falloff = falloff_smooth, .height = 10.0f});
  }
  // The moon, from high above and a little to the left: platforms shade what is under them.
  add_light(ctx, room_centre, {.kind = light_directional, .temperature = 9000.0f, .intensity = 0.9f, .size = 20.0f,
                               .angle = 75.0f, .elevation = 25.0f});
  lighting_desc d = base_lighting();
  d.ambient = {0.05f, 0.05f, 0.10f, 1.0f};
  d.shadow_reach = 160.0f;
  lighting_set(ctx, d);
}

void side_update(context &ctx) {
  entt::registry &reg = world(ctx);
  platformer_body &body = reg.get<platformer_body>(side.hero);
  body.input.move_x = (key_held(ctx, key_d) || key_held(ctx, key_right) ? 1.0f : 0.0f) -
                      (key_held(ctx, key_a) || key_held(ctx, key_left) ? 1.0f : 0.0f);
  const bool jump = key_held(ctx, key_space) || key_held(ctx, key_w) || key_held(ctx, key_up);
  if (key_pressed(ctx, key_space) || key_pressed(ctx, key_w) || key_pressed(ctx, key_up))
    body.input.jump = true;
  body.input.jump_held = jump;
  reg.get<sprite>(side.hero).flip_x = body.facing < 0;
  // The lantern is held in front of the hero, and swings a little as they run.
  const vec2 at = reg.get<transform>(side.hero).pos;
  reg.get<transform>(side.lantern).pos = at + vec2{6.0f * (f32)body.facing, -9.0f + std::sin(elapsed(ctx) * 9.0f) * std::abs(body.velocity.x) / 110.0f};
}
} // namespace

const room rooms_surfaces[4] = {
    {"Bề mặt PBR: normal map, metallic, roughness, emissive",
     "normal map làm sprite nổi khối theo hướng đèn; bản đồ vật liệu (MRA) nói chỗ nào là kim loại, nhám hay bóng, bị che.\n"
     "Kim loại không có màu khuếch tán, chỉ phản chiếu (theo màu của chính nó), nên gương gần như đen trừ điểm sáng. emissive tự sáng.",
     "sprite{.texture = ball, .normal = ball_n, .material = ball_m, .emissive = glow, .emissive_power = 2};   light_2d{.height = 40}",
     "Chuột: đèn   Lăn chuột: height   N: normal map   M: material map", pbr_build, pbr_update, nullptr},
    {"Màu: nhiệt độ màu, pha màu, HDR và tonemap",
     "temperature nhân màu đèn với màu vật đen ở nhiệt độ đó. Ánh sáng cộng lại: đỏ + xanh lá + xanh dương thành trắng.\n"
     "Ánh sáng là HDR (vượt 1 được); tonemap nén về màn hình, exposure chỉnh độ sáng chung như máy ảnh.",
     "light_2d{.temperature = 2700};   desc.tonemap = tonemap_aces;   desc.exposure = 1.5f;   light_color_kelvin(6500)",
     "T: tonemap   Lăn chuột: exposure", colour_build, colour_update, nullptr},
    {"Nhiều đèn cùng lúc",
     "Mỗi đèn là một quad không lớn hơn tầm chiếu của nó, nên đèn nhỏ rất rẻ; bóng là phần tốn nhất. Mỗi frame vẽ tối đa 64 đèn,\n"
     "những đèn gần giữa khung nhìn nhất. Xem FPS và số đèn đã vẽ ở góc dưới, bật / tắt cast_shadows để so sánh.",
     "for (...) add_light(ctx, p, {.color = light_color_kelvin(k), .radius = 56, .size = 2});   l.cast_shadows = false;",
     "+ / -: thêm / bớt đèn (0 đến 64)   O: bóng bật / tắt", swarm_build, swarm_update, swarm_draw},
    {"Platformer nhìn ngang",
     "Cùng một hệ ánh sáng cho game nhìn ngang: nền đất là tilemap (vừa va chạm vừa chắn sáng), trăng là đèn hướng chiếu từ trên xuống\n"
     "nên dưới bục tối, cột đèn là sprite emissive cộng một đèn điểm. Nhân vật cầm đèn lồng và đổ bóng lên tường phía sau.",
     "light_2d{.kind = light_directional, .angle = 75, .elevation = 25};   desc.shadow_reach = 160;",
     "A / D: chạy   Space / W: nhảy", side_build, side_update, nullptr},
};
} // namespace lighting_demo
