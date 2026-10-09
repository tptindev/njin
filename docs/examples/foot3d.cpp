#include <njin.h>

namespace {
using namespace njin;

model_handle hero{};
foot3d_handle feet{};
spring3d_handle hair{};
character3d_handle body{};
i32 idle = -1;
f32 t = 0.0f;
bone_pose3d placed[128], swayed[128];

void load(context &ctx) {
  hero = model_load(ctx, "models/hero.glb");
  idle = model_anim_find(ctx, hero, "Idle_Loop");
  // Không đưa `legs`: tìm hai chân người theo tên (thigh_l, LeftUpLeg, upper_leg.L...).
  feet = foot3d_create(ctx, {.model = hero, .max_step = 0.4f});
  const spring3d_chain tail{.bone = model_bone_find(ctx, hero, "hair_1")};
  hair = spring3d_create(ctx, {.model = hero, .chains = &tail, .chain_count = 1});
  body = character3d_create(ctx, {.position = {0.0f, 0.0f, 0.0f}});
}

void render(context &ctx) {
  t += delta(ctx);
  // Gốc của lần vẽ là chân của character3d (đáy viên nang).
  const transform3d at{.position = character3d_position(ctx, body)};
  // Animation, rồi đặt chân lên bậc thang, dốc, đá; nhảy lên thì thôi đặt chân.
  const f32 weight = character3d_grounded(ctx, body) ? 1.0f : 0.0f;
  foot3d_update(ctx, feet, {.anim = idle, .time = t}, at, delta(ctx), placed, 128, weight);
  // Rồi tóc lò xo trên tư thế đó.
  spring3d_update(ctx, hair, {.bones = placed}, at, delta(ctx), swayed, 128);
  draw_model_anim(ctx, hero, at, {.bones = swayed});
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, load, "load");
  ecs_register(ctx, phase_render, render, "render");
}
} // namespace

mod_desc feet_module() { return {.name = "feet", .setup = setup}; }
