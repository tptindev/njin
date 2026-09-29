// Rooms 1 to 4: the kinds of light (point, spot, directional) and what their settings do.
#include "demo.h"

#include <algorithm>
#include <cmath>

namespace lighting_demo {
namespace {
constexpr f32 deg = 180.0f / 3.14159265f;

// --- 1. point lights and falloff ---

struct falloff_room {
  entt::entity lights[4]{};
  f32 radius = 76.0f;
} falloff;

void falloff_build(context &ctx) {
  fill_floor(ctx, t_stone, t_stone2);
  constexpr light_falloff kinds[4] = {falloff_physical, falloff_linear, falloff_smooth, falloff_none};
  constexpr const char *names[4] = {"falloff_physical", "falloff_linear", "falloff_smooth", "falloff_none"};
  for (i32 i = 0; i < 4; i++) {
    const vec2 at{80.0f + 160.0f * (f32)i, 210.0f};
    // The only difference between the four lights is `falloff`.
    falloff.lights[i] = add_light(ctx, at, {.temperature = 3000.0f, .intensity = 6.0f, .radius = falloff.radius,
                                            .size = 24.0f, .falloff = kinds[i]});
    add_label(at + vec2{0.0f, 96.0f}, names[i]);
  }
  add_label({320.0f, 76.0f}, "");
  lighting_set(ctx, base_lighting());
}

void falloff_update(context &ctx) {
  const f32 wheel = mouse_wheel(ctx);
  if (wheel != 0.0f)
    falloff.radius = std::clamp(falloff.radius + wheel * 8.0f, 30.0f, 200.0f);
  for (const entt::entity e : falloff.lights)
    world(ctx).get<light_2d>(e).radius = falloff.radius;
  demo.labels.back().text = fmt("radius = %.0f", falloff.radius);
}

// --- 2. the size of a light: the penumbra ---

struct size_room {
  entt::entity lights[3]{};
  vec2 pillars[3]{};
  f32 time = 0.0f;
  bool paused = false;
} sizes;

void size_build(context &ctx) {
  fill_floor(ctx, t_wood);
  constexpr f32 source[3] = {0.0f, 8.0f, 28.0f};
  constexpr const char *names[3] = {"size = 0: bóng sắc", "size = 8", "size = 28: nửa tối rộng"};
  for (i32 i = 0; i < 3; i++) {
    const vec2 centre{107.0f + 213.0f * (f32)i, 214.0f};
    sizes.pillars[i] = centre;
    add_occluder(ctx, centre, light_occluder_box({18.0f, 18.0f}));
    // falloff_smooth: with falloff_physical the size would also set how fast the light fades.
    sizes.lights[i] = add_light(ctx, centre, {.temperature = 4200.0f, .intensity = 3.0f, .radius = 150.0f, .size = source[i],
                                              .falloff = falloff_smooth});
    add_label(centre + vec2{0.0f, 112.0f}, names[i]);
  }
  // Thin walls between the three: open polylines (closed = false) block light from both sides.
  for (const f32 x : {213.0f, 427.0f})
    add_occluder(ctx, {x, 0.0f}, light_occluder_line({{0.0f, room_top}, {0.0f, room_size.y}}));
  lighting_set(ctx, base_lighting());
}

void size_update(context &ctx) {
  if (key_pressed(ctx, key_space))
    sizes.paused = !sizes.paused;
  if (!sizes.paused)
    sizes.time += delta(ctx);
  for (i32 i = 0; i < 3; i++)
    world(ctx).get<transform>(sizes.lights[i]).pos = sizes.pillars[i] + from_angle(sizes.time * 40.0f) * 42.0f;
}

void size_draw(context &ctx) {
  for (const vec2 p : sizes.pillars)
    draw_rect(ctx, rect{p - vec2{9.0f, 9.0f}, {18.0f, 18.0f}}, {0.55f, 0.5f, 0.45f, 1.0f});
  for (const entt::entity e : sizes.lights)
    draw_circle(ctx, world(ctx).get<transform>(e).pos, 2.0f, colors::white);
}

// --- 3. spot lights ---

struct spot_room {
  entt::entity flashlight = entt::null;
  entt::entity lighthouse = entt::null;
  f32 cone = 50.0f;
  f32 softness = 0.3f;
} spot;

void spot_build(context &ctx) {
  fill_floor(ctx, t_stone, t_stone2);
  rng &r = random(ctx);
  r.reseed(4);
  for (i32 i = 0; i < 14; i++) {
    const vec2 at{r.range(150.0f, 600.0f), r.range(110.0f, 340.0f)};
    const entt::entity e = add_sprite(ctx, at, demo.img.crate, demo.img.crate_n, demo.img.crate_m);
    // The occluder is the crate's box: the sprite's origin is its bottom centre, so is the box's.
    world(ctx).emplace<light_occluder>(e, light_occluder_box({16.0f, 16.0f}, {0.5f, 1.0f}));
  }
  // The flashlight turns with `angle`, set each frame towards the mouse.
  spot.flashlight = add_light(ctx, {60.0f, 220.0f}, {.kind = light_spot, .temperature = 5600.0f, .intensity = 4.0f,
                                                     .radius = 420.0f, .size = 6.0f, .falloff = falloff_smooth, .cone = spot.cone,
                                                     .softness = spot.softness});
  // The lighthouse turns with its transform: `transform.rot` is added to `angle`.
  spot.lighthouse = add_light(ctx, {560.0f, 110.0f}, {.kind = light_spot, .color = {1.0f, 0.35f, 0.3f, 1.0f},
                                                      .intensity = 4.0f, .radius = 300.0f, .size = 4.0f, .falloff = falloff_smooth,
                                                      .cone = 20.0f,
                                                      .softness = 0.6f});
  add_label({60.0f, 240.0f}, "");
  add_label({560.0f, 84.0f}, "transform.rot += 50 / s");
  lighting_desc d = base_lighting();
  d.ambient = {0.14f, 0.14f, 0.20f, 1.0f};
  lighting_set(ctx, d);
}

void spot_update(context &ctx) {
  entt::registry &reg = world(ctx);
  spot.cone = std::clamp(spot.cone + mouse_wheel(ctx) * 5.0f, 5.0f, 170.0f);
  if (key_held(ctx, key_q))
    spot.softness = std::max(0.0f, spot.softness - delta(ctx) * 0.5f);
  if (key_held(ctx, key_e))
    spot.softness = std::min(1.0f, spot.softness + delta(ctx) * 0.5f);
  light_2d &l = reg.get<light_2d>(spot.flashlight);
  const vec2 aim = mouse_world(ctx) - reg.get<transform>(spot.flashlight).pos;
  l.angle = std::atan2(aim.y, aim.x) * deg;
  l.cone = spot.cone;
  l.softness = spot.softness;
  reg.get<transform>(spot.lighthouse).rot += 50.0f * delta(ctx);
  demo.labels[0].text = fmt("cone = %.0f  softness = %.2f", spot.cone, spot.softness);
}

// --- 4. the sun: a directional light, and shadow_reach ---

struct sun_room {
  entt::entity sun = entt::null;
  f32 hour = 8.0f;
  bool paused = false;
  f32 reach = 40.0f;
} sun;

void sun_build(context &ctx) {
  fill_floor(ctx, t_grass);
  rng &r = random(ctx);
  r.reseed(11);
  for (i32 i = 0; i < 40; i++) {
    const bool is_tree = i % 4 != 0;
    const vec2 at{r.range(20.0f, 620.0f), r.range(96.0f, 356.0f)};
    const entt::entity e = add_sprite(ctx, at, is_tree ? demo.img.tree : demo.img.rock, is_tree ? demo.img.tree_n : demo.img.rock_n,
                                      is_tree ? texture_handle{} : demo.img.rock_m);
    // Pixel shadows: the sprite's own opaque pixels cast the shadow, so it has the tree's shape.
    world(ctx).emplace<light_occluder_pixels>(e);
  }
  // A directional light has no position and no radius: it lights the whole scene from `angle`,
  // `elevation` degrees above the ground.
  sun.sun = add_light(ctx, room_centre, {.kind = light_directional, .intensity = 3.0f, .size = 30.0f});
  add_label({320.0f, 76.0f}, "");
}

void sun_update(context &ctx) {
  if (key_pressed(ctx, key_space))
    sun.paused = !sun.paused;
  if (!sun.paused)
    sun.hour = std::fmod(sun.hour + delta(ctx) * 1.2f, 24.0f);
  sun.reach = std::clamp(sun.reach + mouse_wheel(ctx) * 5.0f, 5.0f, 600.0f);

  // The day: the sun rises in the east (right), is highest at noon, sets in the west. At night the
  // same light is a cold, dim moon on the other side of the sky.
  const bool day = sun.hour >= 6.0f && sun.hour < 18.0f;
  const f32 t = day ? (sun.hour - 6.0f) / 12.0f : std::fmod(sun.hour + 6.0f, 24.0f) / 12.0f; // 0 rise .. 1 set
  const f32 height = std::sin(t * 3.14159265f);                                               // 0 at the horizon, 1 at the top
  light_2d &l = world(ctx).get<light_2d>(sun.sun);
  l.angle = 180.0f + 150.0f * t - 75.0f;             // the light travels from the right to the left, a bit downwards
  l.angle = std::fmod(l.angle, 360.0f);
  l.elevation = 8.0f + 60.0f * height;               // low sun: long shadows, grazing light on the normal maps
  l.temperature = day ? 2200.0f + 4000.0f * height : 9000.0f;
  l.intensity = day ? 0.5f + 1.7f * height : 0.5f * height;

  lighting_desc d = base_lighting();
  const rgba sky = light_color_kelvin(day ? 7000.0f : 12000.0f);
  const f32 amb = day ? 0.14f + 0.22f * height : 0.08f;
  d.ambient = {sky.r * amb, sky.g * amb, sky.b * amb, 1.0f};
  // How long shadows get behind what casts them: a tree is about 24 tall, so with a low sun its shadow
  // is about 24 / tan(elevation) long. Without a limit, a forest vanishes in one big shadow.
  d.shadow_reach = sun.reach;
  lighting_set(ctx, d);
  demo.labels[0].text = fmt("%02.0f:%02.0f   elevation = %.0f   shadow_reach = %.0f", std::floor(sun.hour),
                            std::floor(std::fmod(sun.hour, 1.0f) * 60.0f), l.elevation, sun.reach);
}
} // namespace

const room rooms_lights[4] = {
    {"Đèn điểm và đường tối dần",
     "Bốn đèn điểm giống hệt nhau, chỉ khác light_2d::falloff: cách độ sáng giảm từ tâm ra đến radius.\n"
     "physical giống đèn thật: sáng gắt ở lõi, còn một nửa ở khoảng cách size. linear dễ đoán, smooth là quầng sáng cổ điển, none sáng đều.",
     "add_light(ctx, at, {.temperature = 3000, .intensity = 6, .radius = 120, .size = 24, .falloff = falloff_linear});",
     "Lăn chuột: radius", falloff_build, falloff_update, nullptr},
    {"Kích thước nguồn sáng: bóng mềm",
     "light_2d::size là bán kính của chính nguồn sáng: 0 cho bóng sắc, càng lớn nửa tối càng rộng, sắc ở chân vật chắn và mềm dần khi ra xa.\n"
     "Với falloff_physical, size còn quyết định đèn tắt nhanh thế nào, nên ở đây dùng falloff_smooth. Hai bức tường mỏng là đường mở.",
     "add_light(ctx, at, {.radius = 150, .size = 28, .falloff = falloff_smooth});   add_occluder(ctx, at, light_occluder_box({18, 18}));",
     "Space: dừng / chạy", size_build, size_update, size_draw},
    {"Đèn nón (spot)",
     "light_spot chiếu trong góc cone quanh hướng angle; softness làm mờ rìa nón. Đèn pin quay theo chuột bằng angle,\n"
     "đèn đỏ quay bằng transform.rot (cộng vào angle). Các thùng gỗ vừa là sprite có normal map vừa là vật chắn hình hộp.",
     "light_2d{.kind = light_spot, .radius = 420, .falloff = falloff_smooth, .angle = atan2(aim) * deg, .cone = 50, .softness = 0.3f}",
     "Chuột: hướng đèn   Lăn chuột: cone   Q / E: softness", spot_build, spot_update, nullptr},
    {"Mặt trời: đèn hướng và shadow_reach",
     "light_directional sáng cả cảnh từ hướng angle, cao elevation độ. Mặt trời thấp cho bóng dài và làm normal map nổi rõ.\n"
     "lighting_desc::shadow_reach giới hạn độ dài bóng; ambient và nhiệt độ màu đổi theo giờ. Bóng ở đây là light_occluder_pixels.",
     "light_2d{.kind = light_directional, .angle = a, .elevation = e, .temperature = k};   desc.shadow_reach = 40;",
     "Space: dừng thời gian   Lăn chuột: shadow_reach", sun_build, sun_update, nullptr},
};
} // namespace lighting_demo
