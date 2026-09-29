#include <njin.h>

namespace {
using namespace njin;

model_handle crate;
instance_buffer_handle trees;

void load(njin_ctx &ctx) {
  crate = model_load(ctx, "assets/crate.glb");
  // Mặt trời chiếu chéo xuống và đổ bóng, ánh sáng nền hơi xanh.
  light3d_set(ctx, {.direction = {-0.5f, -1.0f, -0.3f},
                    .ambient = {0.35f, 0.38f, 0.45f, 1.0f},
                    .shadows = true,
                    .shadow_range = 12.0f});
  // Một hàng 20 "cây": mỗi instance là vị trí và tỉ lệ, rồi màu (8 số).
  trees = instance_buffer_create(ctx, 8);
  f32 data[20 * 8];
  for (i32 i = 0; i < 20; i++) {
    const f32 v[8] = {-9.5f + i, 0.5f, -4.0f, 0.8f, 0.2f, 0.5f + 0.02f * i, 0.25f, 1.0f};
    for (i32 k = 0; k < 8; k++)
      data[i * 8 + k] = v[k];
  }
  instance_buffer_upload(ctx, trees, data, 20);
}

void update(njin_ctx &ctx) {
  // Gizmo gọi được ở bất kỳ phase nào: một mũi tên chỉ lên model.
  gizmo_arrow3d(ctx, {2.0f, 2.5f, 0.0f}, {2.0f, 1.2f, 0.0f}, colors::yellow);
}

void render(njin_ctx &ctx) {
  // Camera đứng ở (0, 6, 8), nhìn vào gốc tọa độ.
  begin_3d(ctx, {.position = {0.0f, 6.0f, 8.0f}, .target = {0.0f, 0.0f, 0.0f}, .fovy = 50.0f});
  // Một đèn điểm màu cam cạnh hộp.
  light3d_add(ctx, {.position = {-2.0f, 1.5f, 1.0f}, .color = {1.0f, 0.6f, 0.3f, 1.0f}, .radius = 4.0f});

  draw_plane3d(ctx, {0.0f, 0.0f, 0.0f}, {20.0f, 20.0f}, {0.7f, 0.7f, 0.65f, 1.0f});
  draw_cube3d(ctx, {-2.0f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, colors::red);
  draw_instanced3d(ctx, mesh3d_cube, trees, 0, 20);

  // Model xoay quanh trục y theo thời gian.
  draw_model(ctx, crate, {.position = {2.0f, 0.0f, 0.0f}, .rotation = {0.0f, elapsed(ctx) * 45.0f, 0.0f}});

  // Nhân vật: viên nang SDF mịn, bóng như nhựa, có viền sáng.
  material3d_set(ctx, {.specular = 0.6f, .shininess = 60.0f, .rim = {0.55f, 0.75f, 1.0f, 0.45f}});
  draw_shape3d(ctx, {.kind = shape3d_capsule, .position = {0.0f, 0.6f, 2.0f}, .radius = 0.4f, .height = 1.2f},
               colors::green);
  material3d_set(ctx, {});

  end_3d(ctx);
}

void setup(njin_ctx &ctx) {
  ecs_register(ctx, phase_startup, load, "load");
  ecs_register(ctx, phase_update, update, "update");
  ecs_register(ctx, phase_render, render, "render");
}
} // namespace

mod_desc draw_3d_module() { return {.name = "draw_3d", .setup = setup}; }
