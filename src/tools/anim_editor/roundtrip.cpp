// The engine's view of an exported file: what a game gets from model_load().
#include "roundtrip.h"
#include "njin.h"
#include <algorithm>
#include <cmath>

namespace anim_editor {
RoundtripResult njin_roundtrip(const std::string &glb, const std::string &clip,
                               const std::vector<RoundtripSample> &samples) {
  RoundtripResult r;
  njin::context *ctx = njin::create({.title = "njin anim_editor round trip",
                                     .width = 320.0f,
                                     .height = 240.0f,
                                     .target_fps = 60.0f,
                                     .clear_bg_color = {0, 0, 0, 1}});
  const njin::model_handle model = njin::model_load(*ctx, glb.c_str());
  r.bones = njin::model_bone_count(*ctx, model);
  r.clips = njin::model_anim_count(*ctx, model);
  const njin::i32 anim = njin::model_anim_find(*ctx, model, clip.c_str());
  r.loaded = model.id != 0 && r.bones > 0 && anim >= 0;
  if (!r.loaded)
    r.error = "model_load() gave no skinned model with the clip";
  for (const auto &s : samples) {
    if (!r.loaded)
      break;
    const njin::i32 bone = njin::model_bone_find(*ctx, model, s.bone.c_str());
    if (bone < 0) {
      r.error = "bone " + s.bone + " missing";
      r.loaded = false;
      break;
    }
    const njin::bone_pose3d p =
        njin::model_bone_pose(*ctx, model, {.anim = anim, .time = s.time, .loop = false}, bone);
    const njin::vec3 got[3]{p.x_axis, p.y_axis, p.z_axis};
    for (int a = 0; a < 3; ++a) {
      const double dot = got[a].x * s.axes[a * 3] + got[a].y * s.axes[a * 3 + 1] + got[a].z * s.axes[a * 3 + 2];
      r.max_angle = std::max(r.max_angle, std::acos(std::clamp(dot, -1.0, 1.0)) * 180.0 / 3.14159265358979);
    }
    const double dx = p.position.x - s.position[0], dy = p.position.y - s.position[1], dz = p.position.z - s.position[2];
    r.max_offset = std::max(r.max_offset, std::sqrt(dx * dx + dy * dy + dz * dz));
  }
  njin::destroy(ctx);
  return r;
}
} // namespace anim_editor
