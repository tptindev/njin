// The context's constructor: the one source that sees every core store
// complete (njin_ctx_impl.h).
#include "njin_ctx_impl.h"

#include "modules/camera.h"
#include "modules/collision.h"
#include "modules/debug.h"
#include "modules/dialog.h"
#include "modules/fx.h"
#include "modules/gizmo.h"
#include "modules/lighting.h"
#include "modules/particles_gpu.h"
#include "modules/post_fx.h"
#include "modules/reload.h"
#include "modules/render_stats.h"
#include "modules/sprite.h"
#include "modules/timer.h"
#include "modules/ui.h"
#include "njin_anim_impl.h"
#include "njin_audio_impl.h"
#include "njin_backdrop.h"
#include "njin_ecs.h"
#include "njin_font.h"
#include "njin_i18n_impl.h"
#include "njin_input_impl.h"
#include "njin_level_impl.h"
#include "njin_prefab_impl.h"
#include "njin_quality_impl.h"
#include "njin_scene_impl.h"
#include "njin_sdf2d.h"
#include "njin_shader.h"
#include "njin_texture.h"
#include "njin_view.h"

namespace njin {
context::context(const config &config) : cfg(config), window(config) {
  view.emplace();
  quality.emplace();
  input.emplace();
  shader.emplace();
  texture.emplace();
  render_texture.emplace();
  font.emplace();
  sdf2d.emplace();
  backdrop.emplace();
  audio.emplace();
  post.emplace();
  fx.emplace();
  gizmos.emplace();
  postfx.emplace();
  light.emplace();
  particles_gpu.emplace();
  stats.emplace();
  reload.emplace();
  ui.emplace();
  dialog.emplace();
  i18n.emplace();
  debug.emplace();
  sprites.emplace();
  timers.emplace();
  collision.emplace();
  scene.emplace();
  prefab.emplace();
  anim.emplace();
  level.emplace();
  ecs.emplace();
}

context::~context() = default;
} // namespace njin
