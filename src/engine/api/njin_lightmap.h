#pragma once
#include "njin_renderer.h"
#include <functional>

namespace njin {
struct model_handle;
struct transform3d;
/// Asset lightmap thuộc một context; không chứa native GPU handle.
struct lightmap_handle {std::uint64_t id=0; ///< Resource ID; 0 không hợp lệ.
};
/// Component cho model3d tĩnh: asset và identity object trong scene bake.
struct lightmap3d {
  lightmap_handle asset; ///< Asset cùng context với entity.
  std::uint64_t object=0; ///< Identity object trong scene bake.
};
/// Đánh dấu model tĩnh chỉ tham gia bake như vật chắn/bề mặt bounce.
struct lightmap_occluder3d {std::uint64_t object=0; ///< Identity ổn định của object.
};
/// Texture CPU của material cho path tracing; không chứa resource GPU/path máy.
struct LightmapTexture {
  std::shared_ptr<const RenderImageAsset> image; ///< Base image được snapshot.
  RenderUvTransform uv; ///< UV0 hoặc secondary UV1 của material.
  RenderWrap wrap_s=RenderWrap::Repeat; ///< Sampler wrap theo U.
  RenderWrap wrap_t=RenderWrap::Repeat; ///< Sampler wrap theo V.
  bool linear=true; ///< Bilinear hoặc nearest như material nguồn.
};
/// Snapshot tĩnh lấy từ geometry/transform của Renderer API; không chứa GPU handle.
/// UV lightmap nằm trong lightmap_uv; UV0 và secondary UV của material được giữ.
struct LightmapInstance {
  std::uint64_t identity=0; ///< ID instance ổn định, khác 0 và không trùng.
  std::shared_ptr<const RenderGeometryData> geometry; ///< Snapshot geometry tĩnh.
  RenderInstance placement; ///< Transform local sang world.
  bool receiver=true; ///< Nhận page; false chỉ làm occluder.
  bool dynamic=false; ///< Dynamic/skinned snapshots bị từ chối.
  std::uint64_t source_identity=0; ///< ID object gốc; 0 dùng identity.
  unsigned mesh_index=0; ///< Mesh trong model gốc.
  unsigned page_width=0; ///< 0 dùng size mặc định.
  unsigned page_height=0; ///< 0 dùng size mặc định.
  std::uint64_t source_geometry_fingerprint=0; ///< Do bước unwrap đặt.
  std::array<float,3> albedo{.8f,.8f,.8f}; ///< Diffuse reflectance tuyến tính cho bounce.
  std::array<float,3> emission{}; ///< Radiance emission tuyến tính cho bounce.
  LightmapTexture albedo_texture; ///< Texture albedo nguồn.
  LightmapTexture emission_texture; ///< Texture emission nguồn.
  std::array<float,3> emission_factor{1,1,1}; ///< Hệ số radiance ảnh emission.
};
/// Snapshot đầu vào bake, độc lập scene graph và backend.
struct LightmapScene {
  std::string identity; ///< Tên scene ổn định, không chứa đường dẫn tuyệt đối.
  std::vector<LightmapInstance> instances; ///< Receivers và occluders opaque.
  std::array<float,3> direction{0,-1,0}; ///< Hướng truyền của nguồn directional.
  std::array<float,3> illuminance{1,1,1}; ///< Tuyến tính, đơn vị irradiance Njin.
};
/// Chất lượng/density và seed của CPU baker.
struct LightmapSettings {
  unsigned size=64; ///< Chiều rộng/cao page: 8..512 texel.
  unsigned samples=4; ///< Mẫu vị trí mỗi texel: 1..64.
  unsigned padding=2; ///< Khoảng UV trống tối thiểu ở biên page, theo texel.
  std::uint32_t seed=1; ///< Seed cho multisampling tái lập.
  float bias=.001f; ///< Mét; scene dùng đơn vị mét.
  bool generate_uv=false; ///< Unwrap/pack bằng xatlas, giữ UV material.
  float texels_per_unit=0; ///< 0 auto theo size; khác 0 là texel/mét.
  unsigned indirect_bounces=0; ///< 0 direct-only; 1..8 bounce diffuse bằng CPU tracing.
  unsigned indirect_samples=64; ///< Cosine hemisphere paths mỗi texel sample, 1..1024.
};
/// Một chart và một instance, sở hữu toàn bộ page và mip chain.
struct LightmapPage {
  std::uint64_t instance=0; ///< Identity của receiver.
  std::uint64_t geometry_fingerprint=0; ///< Geometry/normal/UV lightmap đã bake.
  RenderInstance placement; ///< Transform đã bake.
  RenderImageAsset image; ///< RGBA32F tuyến tính, RGB = (direct + indirect) E/pi, alpha = 1.
  std::uint64_t source_instance=0; ///< Mapping về object gốc.
  std::uint64_t source_geometry_fingerprint=0; ///< Fingerprint mesh gốc.
  unsigned mesh_index=0; ///< Mesh material index trong object gốc.
  std::shared_ptr<const RenderGeometryData> geometry; ///< Chart geometry cùng UV lightmap, giữ UV vật liệu.
};
/// Asset CPU có thể serialize và upload riêng trên từng context.
struct LightmapAsset {
  std::uint64_t fingerprint=0; ///< Scene/settings fingerprint để nhận biết stale.
  LightmapSettings settings; ///< Settings gốc.
  std::vector<LightmapPage> pages; ///< Một page mỗi chart của receiver.
};
/// CPU baker: opaque diffuse triangles, directional direct và cosine paths nhiều bounce.
/// Callback trả false để hủy; có thể gọi từ worker unwrap, được tuần tự hóa.
/// indirect_bounces=0 bỏ indirect; emission chỉ truyền sáng khi bật tracing.
LightmapAsset lightmap_bake(const LightmapScene &, const LightmapSettings & = {},
                           const std::function<bool(float)> &progress = {});
std::uint64_t lightmap_fingerprint(const LightmapScene &, const LightmapSettings &);
/// Ghi atomically; file cũ giữ nguyên nếu bước bake/ghi thất bại.
void lightmap_save(const char *path, const LightmapAsset &);
/// Fingerprint là bắt buộc: từ chối dữ liệu hỏng, sai version hoặc scene stale.
LightmapAsset lightmap_load(const char *path, std::uint64_t expected_fingerprint);
/// Load asset vào resource pipeline của context; upload riêng trên device đó.
lightmap_handle lightmap_load(context &,const char *path,std::uint64_t expected_fingerprint);
/// Unload asset/texture; từ chối nếu có draw đang dùng nó.
void lightmap_unload(context &,lightmap_handle);
/// Binding page/geometry/transform, không chứa native GPU handle.
struct LightmapBinding {
  RenderTextureMap map; ///< Texture handle thuộc context upload.
  std::uint64_t geometry_fingerprint=0; ///< Geometry được phép dùng binding.
  RenderInstance placement; ///< Transform được phép dùng binding.
  std::uint64_t source_instance=0; ///< Mapping về model/object gốc.
  std::uint64_t source_geometry_fingerprint=0; ///< Kiểm tra mesh gốc.
  unsigned mesh_index=0; ///< Mesh của model gốc.
  std::shared_ptr<const RenderGeometryData> geometry; ///< Geometry chart đã unwrap.
};
/// Upload một page vào resource store của context; caller dùng renderer_destroy_texture.
LightmapBinding lightmap_upload(context &, const LightmapAsset &, std::uint64_t instance);
/// Upload mọi chart của object; trả handles riêng trên context hiện tại.
std::vector<LightmapBinding> lightmap_upload_instance(context &,const LightmapAsset &,std::uint64_t object);
/// Thu hồi toàn bộ texture của collection sau khi kết thúc frame sử dụng.
void lightmap_unload(context &,std::vector<LightmapBinding> &);
/// Snapshot các mesh tĩnh của model_load/model_create qua resource pipeline Njin.
void lightmap_add_model(context &,LightmapScene &,model_handle,const transform3d &,std::uint64_t object,bool receiver=true);
/// Snapshot ECS transform3d/model3d có lightmap3d hoặc lightmap_occluder3d.
/// Dùng directional của light3d_set; không bake ambient/IBL/local lights ngầm.
LightmapScene lightmap_scene_snapshot(context &,std::string identity);
/// Vẽ model bằng material hiện có và các chart bake của object, qua begin_3d/end_3d.
void draw_lightmapped_model(const context &,model_handle,const transform3d &,std::span<const LightmapBinding>);
/// Vẽ model bằng engine asset handle và object identity đã bake.
void draw_lightmapped_model(const context &,model_handle,const transform3d &,lightmap_handle,std::uint64_t object);
/// Binding static diffuse: tắt realtime lighting/IBL trên draw này, giữ emission và HDR.
/// Từ chối instancing/dynamic caller phải xuất từng static instance thành draw riêng.
void lightmap_bind(MeshDraw &, const LightmapBinding &);
}
