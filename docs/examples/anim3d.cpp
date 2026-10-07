#include <njin.h>
#include <cmath>

namespace {
using namespace njin;

model_handle hero, dwarf;
i32 run_clip = -1, blink = -1;
spring3d_handle hair;
retarget3d_handle hero_to_dwarf;
f32 t = 0.0f;
f32 x = 0.0f;

void load(context &ctx) {
  hero = model_load(ctx, "assets/hero.glb");   // có xương, tóc, mặt có morph "Blink"
  dwarf = model_load(ctx, "assets/dwarf.glb"); // bộ xương khác tên, chân ngắn hơn
  run_clip = model_anim_find(ctx, hero, "Run");
  blink = model_morph_find(ctx, hero, "Blink");
  // Tóc: xương "hair_1" và mọi xương dưới nó đung đưa, không xuyên qua đầu.
  const spring3d_chain chain{.bone = model_bone_find(ctx, hero, "hair_1"), .stiffness = 1.5f, .drag = 0.4f,
                             .gravity = 0.5f};
  const spring3d_collider head{.bone = model_bone_find(ctx, hero, "head"), .offset = {0.0f, 0.1f, 0.0f},
                               .tail = {0.0f, 0.1f, 0.0f}, .radius = 0.11f};
  hair = spring3d_create(ctx, {.model = hero, .chains = &chain, .chain_count = 1, .colliders = &head,
                               .collider_count = 1});
  // Người lùn chạy bằng clip của người hùng: ghép xương theo tên chuẩn.
  hero_to_dwarf = retarget3d_create(ctx, {.source = hero, .target = dwarf});
}

void render(context &ctx) {
  const f32 dt = delta(ctx);
  t += dt;
  x += 3.0f * dt;
  begin_3d(ctx, {.position = {x - 4.0f, 2.0f, 6.0f}, .target = {x, 1.0f, 0.0f}});
  // Nháy mắt 0.15 giây mỗi 3 giây, cộng thêm vào biểu cảm của clip.
  f32 morphs[16] = {};
  if (blink >= 0 && blink < 16)
    morphs[blink] = std::fmod(t, 3.0f) < 0.15f ? 1.0f : 0.0f;
  const transform3d at{.position = {x, 0.0f, 0.0f}, .rotation = {0.0f, 90.0f, 0.0f}};
  const model_pose pose{.anim = run_clip, .time = t, .morph_weights = morphs, .morph_count = 16};
  // Tóc trễ theo quán tính khi chạy.
  bone_pose3d bones[128];
  if (spring3d_update(ctx, hair, pose, at, dt, bones, 128) > 0) {
    model_pose with_hair = pose;
    with_hair.bones = bones;
    draw_model_anim(ctx, hero, at, with_hair);
  }
  // Người lùn đi bên cạnh, cùng một clip.
  bone_pose3d dwarf_bones[128];
  if (retarget3d_pose(ctx, hero_to_dwarf, pose, dwarf_bones, 128) > 0)
    draw_model_anim(ctx, dwarf, {.position = {x, 0.0f, 2.0f}, .rotation = {0.0f, 90.0f, 0.0f}},
                    {.bones = dwarf_bones});
  end_3d(ctx);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, load, "load");
  ecs_register(ctx, phase_render, render, "render");
}
} // namespace

mod_desc anim3d_module() { return {.name = "anim3d", .setup = setup}; }
