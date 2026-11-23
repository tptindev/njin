#pragma once
#include "_math.h"
#include "_types.h"
#include <string>
namespace njin {
struct context;
/// Cache software VT dùng chung cho albedo terrain/model, không cần native sparse textures.
/// Runtime API chỉ gọi từ thread context; request vùng UV trong phase_update.
struct virtual_texture_desc {
  u32 cache_side=4; ///< Số cell mỗi chiều; 2..16, cell 0 giữ mip tail.
  u32 idle_frames=8; ///< Hysteresis trước khi tái dùng cell.
};
/// Counters của cache; GPU bytes là kích thước atlas đã cấp, không gồm driver overhead.
struct virtual_texture_stats {
  u32 resident=0; ///< Số page resident, bao gồm tail.
  u32 pending=0; ///< Page đang đọc/upload.
  u64 hits=0; ///< Demand dùng lại page.
  u64 misses=0; ///< Demand tạo job đọc page.
  u64 evictions=0; ///< Cell cũ bị tái sử dụng.
  u64 deferred=0; ///< Demand chờ cell đủ idle, giữ ancestor fallback.
  usize gpu_bytes=0; ///< Atlas vật lý cố định.
  bool tail_ready=false; ///< Mip tail thật đã được upload; trước đó placeholder hồng.
  std::string error; ///< Lỗi page hoặc admission.
};
/// Cooker offline: ảnh RGBA8 vuông power-of-two -> file NVT1, mip sRGB/gutter repeat.
void virtual_texture_cook(const char *image,const char *output,u32 page_size=64);
/// Đọc metadata, tạo texture handle Njin và enqueue tail qua scheduler hiện có.
texture_handle virtual_texture_load(context &,const char *,const virtual_texture_desc & = {});
/// Demand UV rectangle [0,1], mip 0 chi tiết nhất. Tự request ancestor; gọi theo camera/scene.
void virtual_texture_request(context &,texture_handle,vec2 minimum,vec2 maximum,u32 mip=0,int priority=0);
/// Đọc cache residency/error; unload bằng texture_unload bình thường.
virtual_texture_stats virtual_texture_get(context &,texture_handle);
}
