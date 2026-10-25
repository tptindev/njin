#include "demo.h"

#include <vector>

namespace render_demo {
demo_state demo;

// Every image twice, so key 1 can compare them: packed into one atlas, and loaded one by one.
void load_images(context &ctx) {
  // Pixel art: nearest sampling everywhere, atlas included.
  demo.images.atlas = atlas_create(ctx, {.size = 512, .padding = 1, .filter = filter_nearest});
  for (i32 i = 0; i < kind_count; i++) {
    demo.images.packed[i] = atlas_load(ctx, demo.images.atlas, image_paths[i]);
    demo.images.separate[i] = texture_load(ctx, image_paths[i]);
    texture_set_filter(ctx, demo.images.separate[i], filter_nearest);
    if (normal_paths[i] != nullptr) {
      demo.images.packed_normal[i] = atlas_load(ctx, demo.images.atlas, normal_paths[i]);
      demo.images.separate_normal[i] = texture_load(ctx, normal_paths[i]);
      texture_set_filter(ctx, demo.images.separate_normal[i], filter_nearest);
    }
    if (material_paths[i] != nullptr) {
      demo.images.packed_material[i] = atlas_load(ctx, demo.images.atlas, material_paths[i]);
      demo.images.separate_material[i] = texture_load(ctx, material_paths[i]);
      texture_set_filter(ctx, demo.images.separate_material[i], filter_nearest);
    }
    if (emissive_paths[i] != nullptr) {
      demo.images.packed_emissive[i] = atlas_load(ctx, demo.images.atlas, emissive_paths[i]);
      demo.images.separate_emissive[i] = texture_load(ctx, emissive_paths[i]);
      texture_set_filter(ctx, demo.images.separate_emissive[i], filter_nearest);
    }
  }
  demo.images.trunk_mask = texture_load(ctx, "assets/sprites/tree_t.png");
  texture_set_filter(ctx, demo.images.trunk_mask, filter_nearest);
}

texture_handle image(i32 which) { return demo.images.use_atlas ? demo.images.packed[which] : demo.images.separate[which]; }

texture_handle image_normal(i32 which) {
  if (!demo.images.normal_maps)
    return {};
  return demo.images.use_atlas ? demo.images.packed_normal[which] : demo.images.separate_normal[which];
}

texture_handle image_material(i32 which) {
  if (!demo.images.normal_maps)
    return {};
  return demo.images.use_atlas ? demo.images.packed_material[which] : demo.images.separate_material[which];
}

texture_handle image_emissive(i32 which) {
  if (!demo.images.normal_maps)
    return {};
  return demo.images.use_atlas ? demo.images.packed_emissive[which] : demo.images.separate_emissive[which];
}

void build_map(context &ctx) {
  entt::registry &reg = world(ctx);
  rng &r = random(ctx);
  const texture_handle tileset = texture_load(ctx, "assets/tiles.png");
  texture_set_filter(ctx, tileset, filter_nearest);

  tilemap map{};
  map.tileset = tileset;
  map.tile_size = tile;
  map.layer = layer_ground;
  tilemap_animate(map, 2, {2, 3, 4, 5}, 0.25f);
  // A few lakes: ellipses of water tile 2 (the tilemap animates it).
  struct lake {
    f32 x, y, rx, ry;
  };
  std::vector<lake> lakes;
  for (i32 i = 0; i < 9; i++)
    lakes.push_back({r.range(10.0f, map_w - 10.0f), r.range(10.0f, map_h - 10.0f), r.range(4.0f, 10.0f),
                     r.range(3.0f, 7.0f)});
  for (i32 y = 0; y < map_h; y++) {
    for (i32 x = 0; x < map_w; x++) {
      bool water = false;
      for (const lake &l : lakes) {
        const f32 dx = ((f32)x - l.x) / l.rx;
        const f32 dy = ((f32)y - l.y) / l.ry;
        water = water || dx * dx + dy * dy < 1.0f;
      }
      tilemap_set(map, x, y, water ? 2 : (r.range(0, 4) == 0 ? 1 : 0));
    }
  }
  const entt::entity e = reg.create();
  reg.emplace<transform>(e);
  reg.emplace<tilemap>(e, std::move(map));
}

namespace {
void spawn_crowd_member(context &ctx) {
  entt::registry &reg = world(ctx);
  rng &r = random(ctx);
  const i32 which = r.range(0, k_flower); // tree, bush, rock or flower (the range includes the top)
  const entt::entity e = reg.create();
  reg.emplace<transform>(e, transform{.pos = {r.range(8.0f, world_size.x - 8.0f), r.range(24.0f, world_size.y - 4.0f)}});
  reg.emplace<sprite>(e, sprite{.texture = image(which), .origin = image_origin, .layer = layer_things,
                                .normal = image_normal(which), .material = image_material(which),
                                .emissive = image_emissive(which), .emissive_power = 1.6f});
  reg.emplace<crowd_member>(e, crowd_member{which});
  set_occluder(reg, e, which);
}
} // namespace

// What stands on the ground blocks light. Two ways, key B: each pixel of the sprite (a tree only by its
// trunk, through a mask image; a bush and a rock by their own alpha), or shapes: a tree is its trunk (a
// capsule), a bush an ellipse, and a rock exactly its own outline, traced from the picture.
void set_occluder(entt::registry &reg, entt::entity e, i32 which) {
  if (which != k_tree && which != k_bush && which != k_rock)
    return;
  reg.remove<light_occluder>(e);
  reg.remove<light_occluder_sprite>(e);
  reg.remove<light_occluder_pixels>(e);
  if (demo.lights.pixel_shadows) {
    reg.emplace<light_occluder_pixels>(e, light_occluder_pixels{.mask = which == k_tree ? demo.images.trunk_mask : texture_handle{}});
  } else if (which == k_tree) {
    reg.emplace<light_occluder>(e, light_occluder_capsule({0.0f, -1.5f}, {0.0f, -5.0f}, 2.2f, 3));
  } else if (which == k_bush) {
    reg.emplace<light_occluder>(e, light_occluder_ellipse({6.0f, 3.0f}, {0.0f, -3.0f}, 8));
  } else {
    reg.emplace<light_occluder_sprite>(e, light_occluder_sprite{.alpha = 0.5f, .simplify = 0.8f});
  }
}

void apply_occluders(context &ctx) {
  entt::registry &reg = world(ctx);
  for (auto [e, member] : reg.view<const crowd_member>().each())
    set_occluder(reg, e, member.kind);
}

void build_orbs(context &ctx) {
  entt::registry &reg = world(ctx);
  rng &r = random(ctx);
  for (i32 i = 0; i < 90; i++) {
    const entt::entity e = reg.create();
    reg.emplace<transform>(e, transform{.pos = {r.range(24.0f, world_size.x - 24.0f), r.range(40.0f, world_size.y - 24.0f)}});
    reg.emplace<sprite>(e, sprite{.texture = image(k_orb), .origin = image_origin, .layer = layer_things,
                                  .normal = image_normal(k_orb), .material = image_material(k_orb)});
    reg.emplace<crowd_member>(e, crowd_member{k_orb});
  }
}

void set_crowd(context &ctx, i32 wanted) {
  entt::registry &reg = world(ctx);
  demo.crowd = std::clamp(wanted, 0, 40000);
  // The gold balls are not part of the crowd that + and - change.
  i32 have = 0;
  for (auto [e, member] : reg.view<const crowd_member>().each())
    have += member.kind != k_orb ? 1 : 0;
  for (i32 i = have; i < demo.crowd; i++)
    spawn_crowd_member(ctx);
  if (have > demo.crowd) {
    std::vector<entt::entity> doomed;
    i32 extra = have - demo.crowd;
    for (auto [e, member] : reg.view<const crowd_member>().each()) {
      if (member.kind == k_orb)
        continue;
      if (extra-- <= 0)
        break;
      doomed.push_back(e);
    }
    reg.destroy(doomed.begin(), doomed.end());
  }
}

void build_hero(context &ctx) {
  entt::registry &reg = world(ctx);
  demo.hero = reg.create();
  reg.emplace<transform>(demo.hero, transform{.pos = world_size * 0.5f});
  reg.emplace<sprite>(demo.hero, sprite{.texture = image(k_hero), .origin = image_origin, .layer = layer_things,
                                         .normal = image_normal(k_hero), .material = image_material(k_hero)});
  // The hero casts a shadow too, moving with them. The lights they carry sit inside this shape and
  // are not blocked by it.
  reg.emplace<light_occluder>(demo.hero, light_occluder_capsule({0.0f, -3.0f}, {0.0f, -9.0f}, 3.0f));
  reg.emplace<collider>(demo.hero, collider{.size = {8.0f, 4.0f}, .offset = {0.0f, -2.0f}});
  reg.emplace<topdown_body>(demo.hero, topdown_body{.speed = 110.0f});

  demo.camera = camera_spawn(ctx, camera_zoom, world_size * 0.5f);
  reg.emplace<camera_follow>(demo.camera, camera_follow{.target = demo.hero,
                                                        .smoothing = 0.1f,
                                                        .bounds = rect{{0.0f, 0.0f}, world_size},
                                                        .pixel_snap = true});
}

// The normal and material maps follow key 1 (atlas or not) and key M (on or off).
void apply_normals(context &ctx) {
  entt::registry &reg = world(ctx);
  for (auto [e, spr, member] : reg.view<sprite, const crowd_member>().each()) {
    spr.normal = image_normal(member.kind);
    spr.material = image_material(member.kind);
    spr.emissive = image_emissive(member.kind);
  }
  reg.get<sprite>(demo.hero).normal = image_normal(k_hero);
  reg.get<sprite>(demo.hero).material = image_material(k_hero);
}

void apply_atlas(context &ctx) {
  entt::registry &reg = world(ctx);
  for (auto [e, spr, member] : reg.view<sprite, const crowd_member>().each())
    spr.texture = image(member.kind);
  reg.get<sprite>(demo.hero).texture = image(k_hero);
  apply_normals(ctx);
}
} // namespace render_demo
