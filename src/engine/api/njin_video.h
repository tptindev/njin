#pragma once
#include "_math.h"
#include "_types.h"
#include "njin_audio.h"

namespace njin {
struct context;

/// @addtogroup grp_video
/// @{

/// Cách mở một video bằng video_open().
///
/// **Chỉ đọc MPEG-1** (file `.mpg`: hình MPEG-1, tiếng MP2), giải mã bằng pl_mpeg.
/// Đổi video khác sang dạng này bằng ffmpeg:
/// @code{.sh}
/// ffmpeg -i in.mp4 -c:v mpeg1video -q:v 4 -c:a mp2 -b:a 192k -f mpeg out.mpg
/// @endcode
/// (`-q:v` từ 2 là đẹp nhất đến 31; 4 là cân bằng giữa chất lượng và cỡ file).
struct video_desc {
  const char *path = nullptr; ///< File `.mpg`, tìm như mọi asset.
  bool loop = false;          ///< Hết thì phát lại từ đầu.
  bool play = true;           ///< Phát ngay khi mở. `false` là mở rồi đứng ở hình đầu.
  bool audio = true;          ///< Phát tiếng (nếu file có tiếng và máy có thiết bị âm thanh).
  audio_bus bus = bus_music;  ///< Kênh âm lượng của tiếng (audio_set_bus_volume()).
  f32 volume = 1.0f;          ///< Âm lượng riêng, 0..1, nhân với kênh và `bus_master`.
};

/// Mở một video. Engine giải mã nó trong `phase_post_update` theo thời gian thật
/// (không theo time_set_scale(), vẫn chạy khi game pause), hình và tiếng khớp
/// nhau. Mỗi frame hình mới được chép vào texture của video (video_texture()).
/// @param ctx Context của engine.
/// @param desc Cách mở.
/// @return Handle, hoặc handle không hợp lệ nếu file thiếu hay không phải MPEG-1
/// (cảnh báo nói vì sao).
video_handle video_open(context &ctx, const video_desc &desc);

/// Đóng video, giải phóng texture và tiếng của nó. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Video.
void video_close(context &ctx, video_handle handle);

/// Phát tiếp (hoặc phát lại từ đầu nếu đã hết).
/// @param ctx Context của engine.
/// @param handle Video.
void video_play(context &ctx, video_handle handle);

/// Tạm dừng; hình đứng ở frame hiện tại.
/// @param ctx Context của engine.
/// @param handle Video.
void video_pause(context &ctx, video_handle handle);

/// Video có đang phát không.
/// @param ctx Context của engine.
/// @param handle Video.
/// @return `true` nếu đang phát.
bool video_playing(const context &ctx, video_handle handle);

/// Video đã phát hết chưa (không lặp và đã đến cuối): lúc chuyển cảnh sau cutscene.
/// @param ctx Context của engine.
/// @param handle Video.
/// @return `true` nếu đã hết.
bool video_finished(const context &ctx, video_handle handle);

/// Nhảy tới giây `seconds`. Hình hiện ngay là frame gần nhất trước chỗ đó.
/// @param ctx Context của engine.
/// @param handle Video.
/// @param seconds Thời điểm, giây, kẹp trong 0..video_duration().
void video_seek(context &ctx, video_handle handle, f32 seconds);

/// Đang ở giây thứ mấy.
/// @param ctx Context của engine.
/// @param handle Video.
/// @return Thời điểm, giây.
f32 video_time(const context &ctx, video_handle handle);

/// Video dài bao nhiêu giây.
/// @param ctx Context của engine.
/// @param handle Video.
/// @return Thời lượng, giây.
f32 video_duration(const context &ctx, video_handle handle);

/// Bật tắt phát lặp.
/// @param ctx Context của engine.
/// @param handle Video.
/// @param loop Lặp.
void video_set_loop(context &ctx, video_handle handle, bool loop);

/// Đặt âm lượng riêng của video, 0..1.
/// @param ctx Context của engine.
/// @param handle Video.
/// @param volume Âm lượng.
void video_set_volume(context &ctx, video_handle handle, f32 volume);

/// Cỡ hình, pixel.
/// @param ctx Context của engine.
/// @param handle Video.
/// @return Rộng và cao.
vec2 video_size(const context &ctx, video_handle handle);

/// Số frame hình mỗi giây của file.
/// @param ctx Context của engine.
/// @param handle Video.
/// @return Frame mỗi giây.
f32 video_framerate(const context &ctx, video_handle handle);

/// Video có phát tiếng không: file có tiếng, `video_desc::audio` bật và máy có thiết
/// bị âm thanh.
/// @param ctx Context của engine.
/// @param handle Video.
/// @return `true` nếu có tiếng.
bool video_has_audio(const context &ctx, video_handle handle);

/// Số frame hình đã hiện kể từ khi mở (tăng cả khi tua): để biết hình đã đổi chưa.
/// @param ctx Context của engine.
/// @param handle Video.
/// @return Số frame.
i32 video_frame_count(const context &ctx, video_handle handle);

/// Texture chứa frame hình hiện tại. Vẽ nó như mọi texture (texture_draw_ex()), hay
/// dán lên hình 3D (`material3d::texture`, `model_material`): màn hình TV, bảng quảng
/// cáo. Texture thuộc về video: đừng texture_unload() nó; video_close() giải phóng.
/// @param ctx Context của engine.
/// @param handle Video.
/// @return Texture, hoặc handle không hợp lệ nếu video không hợp lệ.
texture_handle video_texture(const context &ctx, video_handle handle);

/// Vẽ frame hiện tại co giãn vào `dest` (2D, như texture_draw_ex()).
/// @param ctx Context của engine.
/// @param handle Video.
/// @param dest Vùng vẽ.
/// @param tint Màu nhân vào hình.
void video_draw(const context &ctx, video_handle handle, rect dest, rgba tint = {1.0f, 1.0f, 1.0f, 1.0f});

/// Vẽ frame hiện tại vừa khít trong `area`, giữ tỉ lệ khung hình: phần thừa hai bên
/// hay trên dưới tô `bars` (dải đen của cutscene toàn màn hình).
/// @param ctx Context của engine.
/// @param handle Video.
/// @param area Vùng, thường là cả màn hình.
/// @param bars Màu dải thừa; trong suốt là không tô.
void video_draw_fit(const context &ctx, video_handle handle, rect area, rgba bars = {0.0f, 0.0f, 0.0f, 1.0f});
/// @}
} // namespace njin
