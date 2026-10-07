#include <njin.h>

namespace {
using namespace njin;

sound_handle engine;
sound_handle fire;
sound_handle boom;
vehicle3d_handle car;
voice3d_handle engine_voice;

void load(context &ctx) {
  engine = sound_load(ctx, "assets/engine.ogg");
  fire = sound_load(ctx, "assets/fire.ogg");
  boom = sound_load(ctx, "assets/boom.wav");

  body3d_create(ctx, {.position = {0.0f, -0.5f, 0.0f}, .size = {200.0f, 1.0f, 200.0f}});
  // Một bức tường giữa camera và đống lửa: tiếng lửa nhỏ đi khi bị che.
  body3d_create(ctx, {.position = {0.0f, 1.5f, -6.0f}, .size = {6.0f, 3.0f, 0.3f}});
  car = vehicle3d_create(ctx, {.position = {4.0f, 1.0f, 0.0f}});

  // Lửa tí tách đứng yên một chỗ: nghe trong 25 m, bị tường che thì còn 30%.
  sound_loop3d(ctx, fire, vec3{0.0f, 0.5f, -10.0f},
               {.max_distance = 25.0f, .occlusion = true, .occlusion_volume = 0.3f});

  // Tiếng máy: lặp, vị trí và vận tốc lấy từ thân xe mỗi frame (ở update).
  // Thân xe là body của chính nó nên tia che bỏ qua 2,5 m quanh nguồn.
  engine_voice = sound_loop3d(ctx, engine, vec3{4.0f, 1.0f, 0.0f},
                              {.volume = 0.6f, .max_distance = 60.0f, .occlusion = true, .occlusion_margin = 2.5f});
}

void update(context &ctx) {
  const body3d_handle body = vehicle3d_body(ctx, car);
  voice3d_set_position(ctx, engine_voice, body3d_transform(ctx, body).position);
  // Vận tốc thật của body cho Doppler, thay cho vận tốc engine đo từ quãng đi.
  voice3d_set_velocity(ctx, engine_voice, body3d_velocity(ctx, body));
  // Cao độ theo vòng tua máy: 1 ở 1000 vòng, 2 ở 6000 vòng.
  sound3d_desc d = voice3d_desc(ctx, engine_voice);
  d.pitch = 0.8f + vehicle3d_rpm(ctx, car) / 5000.0f;
  voice3d_set_desc(ctx, engine_voice, d);

  // Một tiếng nổ một lần ở chỗ cách xe 10 m phía trước.
  if (key_pressed(ctx, key_b))
    sound_play3d(ctx, boom, body3d_transform(ctx, body).position + vec3{0.0f, 0.0f, -10.0f},
                 {.min_distance = 3.0f, .max_distance = 120.0f});
}

void render(context &ctx) {
  // Tai nghe đi theo camera này: không cần gọi gì thêm.
  begin_3d(ctx, {.position = {0.0f, 6.0f, 14.0f}, .target = {0.0f, 0.0f, 0.0f}});
  draw_plane3d(ctx, {0.0f, 0.0f, 0.0f}, {200.0f, 200.0f}, colors::gray);
  end_3d(ctx);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, load);
  ecs_register(ctx, phase_update, update);
  ecs_register(ctx, phase_render, render);
}
} // namespace

njin::mod_desc audio3d_demo_module() { return {.name = "audio3d_demo", .setup = setup}; }
