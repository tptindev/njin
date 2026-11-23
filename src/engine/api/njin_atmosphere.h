#pragma once
#include "_math.h"
#include "njin_3d.h"
#include <cstdint>

namespace njin {
struct context;
/// Khí quyển cầu đối xứng, Rayleigh/Mie/ozone, mở rộng MS/AP/IBL/night opt-in.
/// Chỉ SDL3/bgfx; gọi một lần giữa begin_3d/end_3d với camera perspective.
/// Các mở rộng mặc định tắt để giữ hành vi cũ; preset physical bật chúng tường minh.
/// Không sửa light3d/environment/fog của game; IBL override chỉ áp dụng view hiện tại.
struct atmosphere3d_desc {
  float planet_radius_km=6360; ///< Bán kính hành tinh, 1000..10000 km.
  float atmosphere_height_km=100; ///< Độ dày khí quyển, 30..200 km.
  vec3 rayleigh_per_km{.005802f,.013558f,.0331f}; ///< Scattering RGB, 0..0.1 mỗi km.
  float rayleigh_height_km=8; ///< Scale height 0.1..30 km; mọi hệ số scattering/absorption 0..0.1 mỗi km.
  float mie_scattering_per_km=.003996f; ///< Scattering Mie, 0..0.1 mỗi km.
  float mie_absorption_per_km=.0044f; ///< Absorption Mie, 0..0.1 mỗi km.
  float mie_height_km=1.2f; ///< Scale height, 0.1..30 km.
  float mie_g=.8f; ///< Anisotropy, -0.95..0.95.
  vec3 ozone_per_km{.00065f,.001881f,.000085f}; ///< Absorption RGB tại đỉnh profile, 0..0.1 mỗi km.
  float ozone_center_km=25; ///< Tâm profile, 0..100 km.
  float ozone_half_width_km=15; ///< Nửa độ rộng profile, 0.1..100 km.
  vec3 sun_direction{0,.70710678f,.70710678f}; ///< Hướng tới mặt trời; tự chuẩn hóa.
  vec3 sun_color{1,1,1}; ///< RGB tuyến tính, 0..1.
  float sun_illuminance_lux=100000; ///< Ngoài khí quyển, 0..200000 lux.
  float sun_radius_rad=.004675f; ///< Bán kính góc, 0.001..0.02 radian.
  bool sun_disk=true; ///< Hiện disk đúng một lần trong background; không ghi vào LUT/IBL.
  /// Đổi cd/m² sang đơn vị radiance của cảnh, 0..1; exposure HDR vẫn áp dụng sau đó.
  float radiance_scale=.0001f;
  float sea_level_m=0; ///< Cao độ mặt biển trong world Y, ±10000000 m; world dùng mét.
  float multiple_scattering=0; ///< Cường độ closure isotropic Hillaire, 0..2; 0 giữ single scattering cũ.
  vec3 ground_albedo{}; ///< Albedo Lambert RGB tuyến tính, 0..0.95.
  bool aerial_perspective=false; ///< Ghép radiance và RGB transmittance bằng scene depth; fog cũ phải tắt.
  float aerial_distance_km=32; ///< Tầm LUT AP, 1..128 km; ngoài tầm giữ giá trị slice cuối.
  bool environment_lighting=false; ///< Override SH9/GGX cho view này, không thay environment của game.
  float environment_intensity=1; ///< Hệ số IBL, 0..1000000 (cho exposure ban đêm).
  float environment_update_seconds=1; ///< Khoảng cách tối thiểu hai lần bake IBL khi sun đổi, 0..60 s.
  float environment_altitude_m=2; ///< Cao độ probe IBL 1..20000 m, tách khỏi camera để nhiều view dùng cùng probe.
  vec3 moon_direction{0,.70710678f,-.70710678f}; ///< Hướng tới trăng; chuẩn hóa.
  vec3 moon_color{.9f,.95f,1}; ///< Màu illuminance tuyến tính, 0..1.
  float moon_illuminance_lux=0; ///< Illuminance full moon ngoài atmosphere, 0..2 lux.
  float moon_phase=1; ///< Phần đĩa trăng sáng, 0..1; illuminance theo phase function Lambert.
  float moon_radius_rad=.0045f; ///< Bán kính góc, 0.001..0.02 radian.
  bool moon_disk=true; ///< Disk có pha, không đưa vào IBL để tránh ánh sáng lặp.
  float night_luminance_cd_m2=0; ///< Airglow/starlight nền 0..1 cd/m², fade theo cao độ sun.
  float star_luminance_cd_m2=0; ///< Độ sáng procedural stars 0..100 cd/m², chỉ background.
  float star_rotation_rad=0; ///< Xoay sao quanh world Y, radian hữu hạn.
};
/// Preset đầy đủ: MS, AP, SH9/GGX, moon/stars/airglow. Các default cũ vẫn giữ nguyên.
/// @return Descriptor Earth với các mở rộng bật.
atmosphere3d_desc atmosphere3d_physical();
/// Đặt hướng sun/moon và góc sao theo quy ước giờ/vĩ độ/mùa của sky3d.
/// @param desc Tham số khí quyển gốc.
/// @param hour Giờ 0..24.
/// @param latitude Vĩ độ -89..89 độ.
/// @param season Mùa -1..1.
/// @return Descriptor; phase/illuminance của trăng được giữ nguyên, không phải lịch thiên văn.
atmosphere3d_desc atmosphere3d_at_hour(const atmosphere3d_desc &desc,float hour,float latitude=35,float season=0);
/// Bộ đếm tích lũy trong context từ lần tạo hoặc release gần nhất.
struct atmosphere3d_stats {
  std::uint64_t transmittance_builds=0; ///< Số lần submit pass transmittance.
  std::uint64_t sky_view_builds=0; ///< Số lần submit pass sky-view.
  std::uint64_t sky_draws=0; ///< Số background draw.
  std::uint64_t color_bytes=0; ///< Color của các LUT/cache RGBA16F, không tính depth hoặc AP scene resolve.
  double last_update_cpu_ms=0; ///< CPU submission, không phải thời gian GPU.
  double last_draw_cpu_ms=0; ///< CPU tạo/bind/submit quad sky, không tính rebuild LUT.
  std::uint64_t multiple_scattering_builds=0; ///< Số pass MS.
  std::uint64_t aerial_builds=0; ///< Số cặp pass AP radiance/transmittance.
  std::uint64_t aerial_composites=0; ///< Số resolve depth AP.
  std::uint64_t environment_builds=0; ///< Số lần bake SH9/GGX từ sky, không tính exposure.
  std::uint64_t environment_deferred=0; ///< Số view giữ IBL cũ do update budget.
  double last_environment_cpu_ms=0; ///< Render-thread submit snapshot/prefilter, không tính worker hoặc GPU time.
  bool environment_pending=false; ///< Job SH đang xử lý/chờ publish; dùng IBL trước đó hoặc environment của game.
  std::uint64_t environment_discarded=0; ///< Job cũ bị bỏ vì medium đổi trước khi hoàn tất.
  double last_environment_worker_ms=0; ///< Thời gian CPU worker, không chặn render thread.
};
/// Vẽ sky vật lý trong view 3D đang mở, vào HDR hiện có.
/// @param ctx Context engine.
/// @param desc Khí quyển và mặt trời; dữ liệu ngoài phạm vi gây exception.
void draw_atmosphere3d(context &ctx,const atmosphere3d_desc &desc = {});
/// Đọc bộ đếm cache và thời gian CPU submission.
/// @param ctx Context engine.
/// @return Bộ đếm, hoặc zero nếu chưa tạo cache.
atmosphere3d_stats atmosphere3d_statistics(const context &ctx);
/// Tính nắng trực tiếp suy giảm qua khí quyển bằng tích phân CPU (64 mẫu).
/// Chỉ đổi direction/color; giữ ambient, environment, fog và shadows của base.
/// Quy ước light3d: alpha = illuminance / 100000 lux; RGB là màu sau extinction,
/// mã hóa sRGB theo API đèn hiện có. Không tự gọi light3d_set hay cộng disk vào IBL.
/// @param desc Khí quyển và mặt trời.
/// @param altitude_m Cao độ trên mặt biển, 1..20000 mét.
/// @param base Ánh sáng gốc.
/// @return Ánh sáng để game truyền vào light3d_set trước begin_3d.
light3d atmosphere3d_sun_light(const atmosphere3d_desc &desc,float altitude_m,const light3d &base = {});
/// Ánh trăng attenuate, giữ các thuộc tính khác của base như sun helper.
/// @param desc Khí quyển/trăng.
/// @param altitude_m Cao độ 1..20000 m.
/// @param base Ánh sáng gốc.
/// @return Light3d hướng từ trăng tới cảnh, normalization như sun helper.
light3d atmosphere3d_moon_light(const atmosphere3d_desc &desc,float altitude_m,const light3d &base = {});
/// Chọn sun hoặc moon có illuminance attenuate mạnh hơn làm directional light của Njin.
/// @param desc Khí quyển và hai thiên thể.
/// @param altitude_m Cao độ 1..20000 m.
/// @param base Ánh sáng gốc, ambient/IBL/fog/shadows được giữ nguyên.
/// @return Đèn chính, không tự gọi light3d_set.
light3d atmosphere3d_key_light(const atmosphere3d_desc &desc,float altitude_m,const light3d &base = {});
/// Bỏ qua update interval cho lần vẽ IBL tiếp theo.
/// @param ctx Context engine.
void atmosphere3d_environment_refresh(context &ctx);
/// Giải phóng cache ngoài begin_3d và sau frame đã sử dụng nó; context cũng tự thu hồi.
/// @param ctx Context engine.
void atmosphere3d_release(context &ctx);
}
