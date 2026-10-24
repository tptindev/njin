// The video feature (njin_features.h): MPEG-1 playback on pl_mpeg.
#include "njin_ctx_get.h"
#include "njin_video_impl.h"

namespace njin {
void feature_install_video(context &ctx) {
  ctx_ref<video_store>(ctx).emplace();
  ctx_ref<feature_hooks>(ctx).modules_mid.push_back(video_module());
}
} // namespace njin
