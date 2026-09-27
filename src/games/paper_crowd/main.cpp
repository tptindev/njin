#include "game.h"

int main() {
  using namespace njin;
  njin_ctx *ctx = njin_create({.title = "Đám Đông Trên Giấy",
                               .width = 1280.0f,
                               .height = 720.0f,
                               .target_fps = 60.0f,
                               .clear_bg_color = {0.945f, 0.94f, 0.925f, 1.0f},
                               // Esc first lets go of a followed person; game.cpp
                               // quits on it only when nobody is followed.
                               .exit_key = key_none,
                               .resizable = true,
                               .app_name = "PaperCrowd",
                               .virtual_size = {paper_crowd::world_w, paper_crowd::world_h},
                               .integer_scale = false,
                               .smooth_ui = true,
                               .render_scale = paper_crowd::render_scale});
  njin_mod_register(*ctx, paper_crowd::module());
#ifndef NDEBUG
  debug_server_start(*ctx);
#endif
  njin_run(*ctx);
  njin_destroy(ctx);
}
