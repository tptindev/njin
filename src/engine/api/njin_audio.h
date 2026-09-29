#pragma once
#include "_types.h"

namespace njin {
// Opaque, see njin_ctx.h.
struct context;

/// @addtogroup grp_sound
/// @{

/// Kênh trộn âm thanh, như thanh trượt trong menu cài đặt của game.
///
/// Âm lượng thật của một sound là âm lượng riêng của nó nhân âm lượng kênh của
/// nó nhân `bus_master`. Music luôn ở kênh `bus_music`; sound mặc định ở
/// `bus_sfx`, đổi bằng sound_set_bus().
enum audio_bus {
  bus_master, ///< Tổng: nhân vào mọi thứ.
  bus_music,  ///< Nhạc nền (mọi music).
  bus_sfx,    ///< Hiệu ứng trong game. Mặc định của sound.
  bus_ui,     ///< Tiếng giao diện.
  bus_voice,  ///< Lồng tiếng, tiếng thoại.
  audio_bus_count ///< Số kênh. Không phải một kênh thật.
};

/// Đặt âm lượng một kênh, từ 0 trở lên (1 là nguyên). Áp dụng ngay cho cả
/// những âm đang phát. Lưu cùng cài đặt bằng settings_save().
/// @param ctx Context của engine.
/// @param bus Kênh.
/// @param volume Âm lượng. Giá trị âm được coi là 0.
void audio_set_bus_volume(context &ctx, audio_bus bus, f32 volume);

/// Âm lượng một kênh. @param ctx Context của engine. @param bus Kênh.
/// @return Âm lượng, mặc định 1.
f32 audio_bus_volume(const context &ctx, audio_bus bus);

/// Tắt hoặc bật tiếng cả một kênh, không đụng đến âm lượng đã đặt.
/// @param ctx Context của engine.
/// @param bus Kênh.
/// @param muted `true` để tắt tiếng.
void audio_set_bus_muted(context &ctx, audio_bus bus, bool muted);

/// Kênh có đang tắt tiếng không. @param ctx Context của engine. @param bus Kênh.
/// @return `true` nếu đang tắt.
bool audio_bus_muted(const context &ctx, audio_bus bus);

/// Đưa một sound vào kênh khác, ví dụ `bus_ui` cho tiếng nhấp menu.
/// @param ctx Context của engine.
/// @param handle Sound.
/// @param bus Kênh.
void sound_set_bus(context &ctx, sound_handle handle, audio_bus bus);

/// Nạp một âm thanh ngắn vào bộ nhớ.
///
/// Dùng cho hiệu ứng như tiếng bắn, tiếng nhấp. Nhạc nền dài thì dùng music_load().
/// Handle không hợp lệ hoặc đã unload bị mọi hàm sound bỏ qua. Nếu máy không có
/// thiết bị âm thanh thì việc nạp thất bại và ghi log, còn game vẫn chạy.
/// @param ctx Context của engine.
/// @param path Đường dẫn file âm thanh (wav, ogg, mp3, flac...).
/// @return Handle của sound, hoặc handle có id 0 nếu không có thiết bị, file thiếu
/// hoặc không giải mã được.
sound_handle sound_load(context &ctx, const char *path);

/// Tạo một âm thanh từ các mẫu đã có trong bộ nhớ.
///
/// Dữ liệu là mono, số thực 32 bit trong khoảng -1..1. Nó được chép vào bộ đệm
/// riêng của sound và không được giữ lại, nên mảng của bạn có thể bị hủy ngay sau
/// khi hàm trả về. Dùng cho âm thanh game tự sinh ra lúc khởi động.
/// @param ctx Context của engine.
/// @param samples Mảng mẫu.
/// @param count Số mẫu.
/// @param sample_rate Tần số lấy mẫu, ví dụ 44100.
/// @return Handle của sound, hoặc handle có id 0 nếu tham số sai hoặc tạo thất bại.
sound_handle sound_load_samples(context &ctx, const f32 *samples, i32 count,
                                i32 sample_rate);

/// Giải phóng sound. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Sound cần giải phóng.
void sound_unload(context &ctx, sound_handle handle);

/// Đặt âm lượng của sound, từ 0 trở lên (1 là âm lượng gốc).
///
/// Áp dụng ngay cho cả những bản đang phát. Mặc định là 1. Giá trị âm được coi là 0.
/// @param ctx Context của engine.
/// @param handle Sound cần đặt.
/// @param volume Âm lượng.
void sound_set_volume(context &ctx, sound_handle handle, f32 volume);

/// Tắt hoặc bật tiếng sound.
///
/// Tắt tiếng đưa âm lượng đang phát về 0 mà không đụng đến âm lượng đã lưu, nên
/// bật lại thì khôi phục đúng như cũ.
/// @param ctx Context của engine.
/// @param handle Sound cần đặt.
/// @param muted `true` để tắt tiếng.
void sound_set_muted(context &ctx, sound_handle handle, bool muted);

/// Phát sound mà không cắt các bản đang phát.
///
/// Gọi liên tiếp thì các bản chồng lên nhau, tối đa 8 bản cùng lúc. Quá số đó
/// thì bản chạy lâu nhất bị cắt để lấy chỗ. Hợp với âm thanh mà nhiều thứ cùng
/// kích hoạt, để một đám đông được nghe như một đám đông.
/// @param ctx Context của engine.
/// @param handle Sound cần phát.
void sound_play_once(context &ctx, sound_handle handle);

/// Giống sound_play_once() nhưng chỉnh cao độ và độ lớn cho riêng bản này.
///
/// `pitch` 1 là như bản ghi, 2 là cao gấp đôi. `gain` nhân vào âm lượng của
/// sound cho bản này. Cả hai không được lưu lại: lần phát thường sau đó trở về 1
/// và 1. Hợp với một âm thanh dùng cho nhiều vật có kích cỡ khác nhau: một bản ghi
/// dùng cho tất cả, vật lớn hơn nghe to hơn và trầm hơn.
/// @param ctx Context của engine.
/// @param handle Sound cần phát.
/// @param pitch Cao độ. Giá trị rất nhỏ được nâng lên mức tối thiểu.
/// @param gain Hệ số âm lượng cho bản này. Giá trị âm được coi là 0.
void sound_play_once_at(context &ctx, sound_handle handle, f32 pitch, f32 gain);

/// Cắt mọi bản đang phát rồi phát lại từ đầu, nên lúc nào cũng chỉ nghe một bản.
///
/// Hợp với tiếng nhấp giao diện hoặc tiếng cảnh báo: bấm dồn thì nghe rõ từng
/// lần thay vì chồng thành một đống.
/// @param ctx Context của engine.
/// @param handle Sound cần phát.
void sound_play_restart(context &ctx, sound_handle handle);

/// Đánh dấu sound là lặp lại và phát nếu nó chưa phát.
///
/// Module âm thanh của engine phát lại sound mỗi khi thấy nó vừa kết thúc, nên
/// đây là lời gọi duy nhất cần để giữ nó chạy. Vì được phát lại sau khi kết thúc,
/// chỗ nối có một quãng lặng cỡ một frame. Với nhạc nền dài, dùng music_load()
/// để không có quãng lặng. Gọi sound_stop() để dừng.
/// @param ctx Context của engine.
/// @param handle Sound cần phát lặp.
void sound_play_loop(context &ctx, sound_handle handle);

/// Phát sound như sound_play_once(), kèm vị trí trong thế giới.
///
/// Âm lượng giảm dần theo khoảng cách tới điểm camera đang nhìn, và âm thanh
/// lệch sang loa trái hoặc phải theo vị trí trên màn hình. Xem
/// audio_set_range().
/// @param ctx Context của engine.
/// @param handle Sound cần phát.
/// @param world_pos Nơi phát ra tiếng, trong thế giới.
void sound_play_at(context &ctx, sound_handle handle, vec2 world_pos);

/// Khoảng cách nghe được của sound_play_at().
///
/// Gần hơn `full_until` thì nghe đủ âm lượng, xa hơn `silent_from` thì im
/// lặng, ở giữa thì nhỏ dần đều. Mặc định là 200 và 1200 đơn vị thế giới.
/// @param ctx Context của engine.
/// @param full_until Khoảng cách bắt đầu nhỏ dần.
/// @param silent_from Khoảng cách im lặng hẳn. Nhỏ hơn `full_until` thì được
/// nâng bằng `full_until`.
void audio_set_range(context &ctx, f32 full_until, f32 silent_from);

/// Dừng mọi bản đang phát của sound và bỏ chế độ lặp.
/// @param ctx Context của engine.
/// @param handle Sound cần dừng.
void sound_stop(context &ctx, sound_handle handle);
/// @}

/// @addtogroup grp_music
/// @{

/// Nạp một bản nhạc để stream từ đĩa.
///
/// Nhạc không nằm hết trong bộ nhớ mà được giải mã từng đoạn, hợp với nhạc nền
/// hoặc âm thanh môi trường dài. Nó lặp ngay trong bộ giải mã nên không có quãng
/// lặng ở chỗ nối. Module âm thanh của engine cấp dữ liệu cho stream mỗi frame.
/// @param ctx Context của engine.
/// @param path Đường dẫn file nhạc (ogg, mp3, wav, flac...).
/// @return Handle của music, hoặc handle có id 0 nếu không có thiết bị, file thiếu
/// hoặc không giải mã được.
music_handle music_load(context &ctx, const char *path);

/// Giải phóng music. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Music cần giải phóng.
void music_unload(context &ctx, music_handle handle);

/// Đặt âm lượng của music, từ 0 trở lên (1 là âm lượng gốc). Mặc định là 1.
/// @param ctx Context của engine.
/// @param handle Music cần đặt.
/// @param volume Âm lượng. Giá trị âm được coi là 0.
void music_set_volume(context &ctx, music_handle handle, f32 volume);

/// Tắt hoặc bật tiếng music, không đụng đến âm lượng đã lưu.
/// @param ctx Context của engine.
/// @param handle Music cần đặt.
/// @param muted `true` để tắt tiếng.
void music_set_muted(context &ctx, music_handle handle, bool muted);

/// Bật hoặc tắt chế độ lặp. Mặc định là bật. Có hiệu lực ngay cả giữa bài.
/// @param ctx Context của engine.
/// @param handle Music cần đặt.
/// @param looping `true` để lặp.
void music_set_looping(context &ctx, music_handle handle, bool looping);

/// Phát từ đầu, kể cả khi đang tạm dừng hoặc đang phát.
/// @param ctx Context của engine.
/// @param handle Music cần phát.
void music_play(context &ctx, music_handle handle);

/// Dừng và đưa vị trí về đầu bài.
/// @param ctx Context của engine.
/// @param handle Music cần dừng.
void music_stop(context &ctx, music_handle handle);

/// Tạm dừng nhưng giữ nguyên vị trí. Dùng music_resume() để phát tiếp.
/// @param ctx Context của engine.
/// @param handle Music cần tạm dừng.
void music_pause(context &ctx, music_handle handle);

/// Phát tiếp từ chỗ music_pause() đã dừng.
/// @param ctx Context của engine.
/// @param handle Music cần phát tiếp.
void music_resume(context &ctx, music_handle handle);

/// Music có đang phát không (không tính lúc tạm dừng).
/// @param ctx Context của engine.
/// @param handle Music.
/// @return `true` nếu đang phát.
bool music_playing(context &ctx, music_handle handle);

/// Phát từ đầu, to dần từ im lặng trong `seconds` giây. Đang phát thì chỉ to
/// dần lên mức đầy từ mức hiện tại.
/// @param ctx Context của engine.
/// @param handle Music.
/// @param seconds Thời gian to dần, giây (giờ thật).
void music_fade_in(context &ctx, music_handle handle, f32 seconds);

/// Nhỏ dần rồi dừng.
/// @param ctx Context của engine.
/// @param handle Music.
/// @param seconds Thời gian nhỏ dần, giây (giờ thật).
void music_fade_out(context &ctx, music_handle handle, f32 seconds);

/// Chuyển nhạc: mọi music khác đang phát nhỏ dần rồi dừng, trong lúc `handle`
/// to dần. Gọi khi vào màn mới, khi gặp trùm. `handle` đang phát rồi thì nó
/// tiếp tục, không bị phát lại từ đầu.
/// @code
/// njin::music_crossfade(ctx, g.boss_theme, 1.5f);
/// @endcode
/// @param ctx Context của engine.
/// @param handle Music cần chuyển sang. Handle id 0 thì chỉ tắt dần mọi nhạc.
/// @param seconds Thời gian chuyển, giây (giờ thật).
void music_crossfade(context &ctx, music_handle handle, f32 seconds);
/// @}
} // namespace njin
