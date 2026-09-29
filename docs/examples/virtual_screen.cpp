#include <njin.h>

int main() {
  // Pixel art vẽ trên màn 320 x 180, engine phóng ra cửa sổ theo bội số nguyên:
  // pixel sắc nét ở mọi cỡ cửa sổ, phần thừa là viền.
  njin::context *ctx = njin::create({.title = "Pixel art",
                                           .width = 1280,
                                           .height = 720,
                                           .target_fps = 60,
                                           .resizable = true,
                                           .virtual_size = {320.0f, 180.0f},
                                           .integer_scale = true});
  // Bật tắt lúc chạy được: window_set_virtual_size(*ctx, {0, 0}) để vẽ thẳng lên cửa sổ.
  njin::window_set_bar_color(*ctx, {0.05f, 0.05f, 0.08f, 1.0f});
  // screen_size() trả 320 x 180 và mouse_pos() tính theo pixel ảo.
  // window_size() và window_viewport() cho cỡ thật của cửa sổ.
  njin::destroy(ctx);
}
