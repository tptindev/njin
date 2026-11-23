#pragma once
#include "_types.h"
#include "_draw_types.h"
#include <string>
namespace njin {
struct context;
struct model_handle;
struct model_lod_desc;
/// Request thuộc context; mỗi request trùng tăng reference, release giảm reference.
/// Chỉ gọi API trên thread sở hữu context. Cancel ảnh hưởng mọi reference trùng.
struct stream_handle {
  u32 id=0; ///< Chỉ số request; 0 không hợp lệ.
  u64 owner=0; ///< Identity context, ngăn request cũ alias sau reinitialization.
};
/// CPU decode và GPU upload là hai trạng thái khác nhau; ready đã gửi upload thật.
enum class stream_state { queued, loading, uploading, ready, failed, cancelled, evicted };
/// Budget chỉ áp dụng asset streamed; thời gian upload là giới hạn mềm từng unit.
struct stream_config {
  usize cpu_bytes=128*1024*1024; ///< Admission CPU payload; không gồm decoder scratch.
  usize gpu_bytes=256*1024*1024; ///< Ước tính texture/vertex/index bytes, không gồm driver overhead.
  usize upload_bytes_per_frame=8*1024*1024; ///< Giới hạn cứng bytes upload mỗi frame.
  f32 upload_ms_per_frame=2; ///< Ngừng sau unit vượt thời gian này; không preempt driver.
  u32 upload_units_per_frame=8; ///< Số texture/submesh tối đa mỗi frame.
  u32 idle_frames=120; ///< Chỉ eviction sau số frame không có demand này.
  u32 test_delay_ms=0; ///< Trì hoãn có thể hủy để tái hiện race trong fixture.
};
/// Trạng thái và nguyên nhân lỗi quan sát được của request.
struct stream_info {
  stream_state state=stream_state::cancelled; ///< Trạng thái mới nhất.
  usize cpu_bytes=0; ///< CPU payload được tính vào admission, giữ reservation sau ready.
  usize gpu_bytes=0; ///< Ước tính bytes đã upload.
  std::string error; ///< Lỗi loader/budget hoặc lý do đang chờ budget.
  bool renderable=false; ///< Có mức chi tiết resident thực, kể cả khi đang refine.
  u32 resident_level=~u32(0); ///< Mip/LOD resident tốt nhất; số lớn hơn là thô hơn.
  u32 target_level=0; ///< Mức chi tiết game yêu cầu.
};
/// Số đo của asset streaming, không phải toàn bộ bộ nhớ hoặc upload của renderer.
struct stream_stats {
  usize cpu_bytes=0; ///< Tổng reservation CPU.
  usize gpu_bytes=0; ///< Tổng bytes GPU ước tính.
  usize uploaded_bytes=0; ///< Bytes gửi tới bgfx trong frame vừa pump.
  usize peak_uploaded_bytes=0; ///< Peak upload mỗi frame.
  u32 queued=0; ///< Request chờ worker.
  u32 loading=0; ///< Request đang chuẩn bị CPU.
  u32 uploading=0; ///< Payload chờ hoặc đang upload.
  u64 cache_hits=0; ///< Request trùng dùng lại công việc.
  u64 evictions=0; ///< Số lần eviction.
  u64 failures=0; ///< Số lần loader/admission/upload lỗi.
  f32 upload_ms=0; ///< Thời gian pump upload trên main thread.
  bool gpu_budget_blocked=false; ///< Budget bị vượt vì resource pinned hoặc chưa đủ idle.
};
/// Cấu hình streaming theo context; các budget phải dương và finite.
void streaming_configure(context &,const stream_config &);
/// Request texture qua CPU loader hiện có; trả placeholder hồng khi chưa ready.
stream_handle texture_request(context &,const char *path,int priority=0);
/// Không gian lọc mip: màu sRGB, normal tái chuẩn hóa, hoặc dữ liệu tuyến tính.
enum class stream_texture_kind {color,normal,data};
/// Tải mip thô trước, refine dần; chỉ sampler đọc mip đã được khởi tạo.
stream_handle texture_request_mips(context &,const char *path,int priority=0,stream_texture_kind kind=stream_texture_kind::color);
/// Đổi mip/LOD đích; resource coarse hiện có tiếp tục render khi refine.
void streaming_detail(context &,stream_handle,u32 level);
/// Request glTF/GLB hoặc OBJ/MTL, bao gồm material images, skeleton và morph nếu có.
stream_handle model_request(context &,const char *path,int priority=0,bool pbr=false);
/// Chuẩn bị LOD qua meshoptimizer hiện có, upload coarse trước, giữ CPU mesh gốc.
stream_handle model_request_lods(context &,const char *path,const model_lod_desc &,int priority=0,bool pbr=false);
/// Lấy texture handle bình thường và ghi nhận demand; không chứa native GPU handle.
texture_handle streaming_texture(context &,stream_handle);
/// Lấy model handle bình thường và ghi nhận demand; placeholder là tam giác trắng.
model_handle streaming_model(context &,stream_handle);
/// Đọc trạng thái và thông báo lỗi.
stream_info streaming_info(context &,stream_handle);
/// Đọc counters của lần pump mới nhất.
stream_stats streaming_stats(context &);
/// Demand theo scene/camera do game quyết định; priority cao được xử lý trước.
void streaming_touch(context &,stream_handle,int priority=0);
/// Giữ resource khỏi eviction, cần cho ECS/physics/animation lưu handle lâu dài.
void streaming_pin(context &,stream_handle,bool pinned=true);
/// Bỏ một reference; reference cuối hủy job và unload tại frame boundary.
void streaming_release(context &,stream_handle);
/// Hủy request chung; kết quả generation cũ sẽ bị bỏ qua, giữ placeholder.
void streaming_cancel(context &,stream_handle);
/// Retry failed/cancelled/evicted; gọi ngoài frame render.
void streaming_retry(context &,stream_handle);
/// Vòng lặp engine tự pump trước frame; cấm gọi khi đang render.
void streaming_update(context &);
}
