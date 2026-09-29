#include <njin.h>

namespace {
// Nơi nhận log của riêng bạn, ví dụ ghi ra file hoặc hiện lên màn hình.
void my_sink(njin::log_level level, const char *file, njin::i32 line,
             const char *msg, void *user) {
  (void)user;
  std::printf("[%s] %s:%d %s\n", njin::log_level_name(level),
              file != nullptr ? file : "?", (int)line, msg);
}

void setup(njin::context &) {
  njin::log_set_level(njin::log_warn); // ẩn trace/debug/info
  njin::log_set_sink(my_sink);         // nullptr để về mặc định (stderr)

  NJIN_INFO("dòng này bị ẩn vì thấp hơn log_warn");
  NJIN_WARN("texture thiếu: %s", "assets/player.png");
  NJIN_ERROR("mã lỗi %d", 42);
}
} // namespace

njin::mod_desc logging_demo_module() {
  return {.name = "logging_demo", .setup = setup};
}
