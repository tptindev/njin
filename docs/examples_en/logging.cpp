#include <njin.h>

namespace {
// Your own log receiver, for example writing to a file or showing on screen.
void my_sink(njin::log_level level, const char *file, njin::i32 line,
             const char *msg, void *user) {
  (void)user;
  std::printf("[%s] %s:%d %s\n", njin::log_level_name(level),
              file != nullptr ? file : "?", (int)line, msg);
}

void setup(njin::context &) {
  njin::log_set_level(njin::log_warn); // hide trace/debug/info
  njin::log_set_sink(my_sink);         // nullptr to go back to the default (stderr)

  NJIN_INFO("this line is hidden because it is below log_warn");
  NJIN_WARN("missing texture: %s", "assets/player.png");
  NJIN_ERROR("error code %d", 42);
}
} // namespace

njin::mod_desc logging_demo_module() {
  return {.name = "logging_demo", .setup = setup};
}
