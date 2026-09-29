#include <njin.h>

namespace {
using namespace njin;

// An object standing on the ground, blocking light with each pixel of its image. If `mask` is given, only the sufficiently
// opaque pixels of the mask image block light (same size as the sprite image).
void place(context &ctx, const char *image, vec2 pos, f32 scale, const char *mask = nullptr) {
  entt::registry &reg = world(ctx);
  const entt::entity e = reg.create();
  reg.emplace<transform>(e, transform{.pos = pos, .scale = scale});
  reg.emplace<sprite>(e, sprite{.texture = texture_load(ctx, image), .origin = {0.5f, 1.0f}});
  reg.emplace<light_occluder_pixels>(e, light_occluder_pixels{.mask = mask != nullptr ? texture_load(ctx, mask) : texture_handle{}});
}

void load(context &ctx) {
  entt::registry &reg = world(ctx);
  lighting_set(ctx, {.enabled = true, .ambient = {0.34f, 0.38f, 0.55f, 1.0f}});

  const entt::entity lamp = reg.create();
  reg.emplace<transform>(lamp, transform{.pos = {400.0f, 300.0f}});
  reg.emplace<light_2d>(lamp, light_2d{.temperature = 3000.0f, .intensity = 14.0f, .radius = 380.0f, .size = 60.0f, .height = 90.0f});

  place(ctx, "assets/sprites/tree.png", {250.0f, 330.0f}, 4.0f);                                // the whole tree blocks light
  place(ctx, "assets/sprites/tree.png", {560.0f, 330.0f}, 4.0f, "assets/sprites/tree_t.png");   // only the trunk blocks
  place(ctx, "assets/sprites/rock.png", {400.0f, 190.0f}, 5.0f);
  place(ctx, "assets/sprites/bush.png", {400.0f, 480.0f}, 5.0f);
}

void setup(context &ctx) { ecs_register(ctx, phase_startup, load, "load"); }
} // namespace

mod_desc light_pixels_module() { return {.name = "light_pixels", .setup = setup}; }
