#pragma once
#include "_types.h"
#include <entt/entity/fwd.hpp>

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

/// @addtogroup grp_sound3d
/// @{

/// Tai nghe của thế giới 3D: mọi tiếng 3D được nghe từ đây.
///
/// Mặc định nó đi theo camera của lần begin_3d() vẽ ra màn hình gần nhất (vị
/// trí, hướng nhìn, hướng lên), và vận tốc được đo từ quãng camera đi mỗi frame.
/// Đặt tay bằng audio_set_listener3d() khi tai không ở camera (góc nhìn thứ ba
/// mà muốn nghe từ nhân vật).
struct audio_listener3d {
  vec3 position{0.0f, 0.0f, 0.0f}; ///< Vị trí tai.
  vec3 forward{0.0f, 0.0f, -1.0f}; ///< Hướng nhìn. Không cần độ dài 1.
  vec3 up{0.0f, 1.0f, 0.0f};       ///< Hướng lên. Không cần độ dài 1.
  vec3 velocity{0.0f, 0.0f, 0.0f}; ///< Vận tốc, đơn vị mỗi giây, cho hiệu ứng Doppler.
};

/// Đặt tai nghe bằng tay. Từ lúc này tai thôi đi theo camera, cho đến khi gọi
/// audio_listener3d_follow_camera(). Giá trị không hữu hạn (NaN, vô cực) bị bỏ qua.
/// @param ctx Context của engine.
/// @param listener Tai nghe. Vận tốc 0 thì không có Doppler do tai di chuyển.
void audio_set_listener3d(context &ctx, const audio_listener3d &listener);

/// Cho tai nghe đi theo camera của begin_3d() (mặc định), hoặc thôi đi theo và
/// đứng yên ở chỗ hiện tại.
/// @param ctx Context của engine.
/// @param follow `true` để đi theo camera.
void audio_listener3d_follow_camera(context &ctx, bool follow = true);

/// Tai nghe đang dùng, kể cả khi nó đi theo camera.
/// @param ctx Context của engine.
/// @return Tai nghe.
audio_listener3d audio_listener3d_get(const context &ctx);

/// Tốc độ âm thanh, đơn vị thế giới mỗi giây, cho hiệu ứng Doppler. Mặc định 343
/// (mét mỗi giây, khi một đơn vị là một mét). Nhỏ hơn thì Doppler rõ hơn.
/// @param ctx Context của engine.
/// @param units_per_second Tốc độ. Không dương thì bị bỏ qua.
void audio_set_speed_of_sound(context &ctx, f32 units_per_second);

/// Cách âm lượng giảm theo khoảng cách, từ `min_distance` (đủ to) đến
/// `max_distance` (im).
enum audio_rolloff {
  /// Giảm theo 1/khoảng cách, như ngoài đời: nhanh lúc gần, chậm lúc xa. Ở 10%
  /// cuối trước `max_distance` nhỏ dần về 0 để không tắt đột ngột. Mặc định.
  rolloff_inverse,
  /// Giảm đều từ 1 ở `min_distance` đến 0 ở `max_distance`. Dễ đoán, hợp với game
  /// cần biết chắc tiếng nghe được tới đâu.
  rolloff_linear,
  /// Giảm theo lũy thừa của khoảng cách, `(d / min_distance)^-rolloff_factor`:
  /// tắt nhanh hơn inverse. Cùng nhỏ dần ở 10% cuối như inverse.
  rolloff_exponential,
};

/// Cách nghe một tiếng 3D. Mọi trường sửa được lúc tiếng đang phát bằng
/// voice3d_set_desc().
struct sound3d_desc {
  f32 volume = 1.0f; ///< Nhân vào âm lượng của sound (và kênh của nó). Âm thì là 0.
  f32 pitch = 1.0f;  ///< Cao độ trước Doppler. 1 là như bản ghi.
  f32 min_distance = 1.0f;  ///< Gần hơn thì nghe đủ to.
  f32 max_distance = 40.0f; ///< Xa hơn thì im (và không tốn gì để trộn).
  audio_rolloff rolloff = rolloff_inverse; ///< Cách giảm theo khoảng cách.
  /// Độ dốc của `rolloff_inverse` và `rolloff_exponential`. 1 là như ngoài đời,
  /// 2 là tắt nhanh hơn, 0.5 là vang xa hơn.
  f32 rolloff_factor = 1.0f;
  /// Độ lệch trái phải, 0..1. 1 là tiếng bên phải nghe hẳn ở loa phải; 0 là luôn
  /// ở giữa (chỉ đổi âm lượng). Gần tai hơn `min_distance` thì tự về giữa dần, để
  /// tiếng ngay trên đầu không nhảy từ loa này sang loa kia.
  f32 spread = 1.0f;
  /// Độ mạnh của Doppler: tiếng cao lên khi nguồn và tai lại gần nhau, trầm xuống
  /// khi xa nhau. 0 là tắt, 1 là như ngoài đời.
  f32 doppler = 1.0f;
  /// Hướng phát của một loa có hướng (còi xe, loa phóng thanh), trong thế giới.
  /// `{0, 0, 0}` (mặc định) là phát đều mọi hướng và các trường `cone_*` bị bỏ qua.
  vec3 cone_direction{0.0f, 0.0f, 0.0f};
  f32 cone_inner = 360.0f; ///< Góc toàn phần (độ) của vùng nghe đủ to, quanh `cone_direction`.
  f32 cone_outer = 360.0f; ///< Góc toàn phần (độ) mà ngoài nó chỉ còn `cone_outer_volume`.
  f32 cone_outer_volume = 0.0f; ///< Hệ số âm lượng ngoài `cone_outer`, 0..1.
  /// Bị che: mỗi frame bắn một tia vật lý (physics3d_raycast()) từ tai tới nguồn;
  /// chạm một body ở trước nguồn thì tiếng nhỏ lại còn `occlusion_volume`, chuyển
  /// mượt trong khoảng 0,15 giây. Chỉ đổi âm lượng, không làm tiếng đục đi. Không có
  /// thế giới vật lý thì không bao giờ bị che.
  bool occlusion = false;
  f32 occlusion_volume = 0.35f; ///< Hệ số âm lượng khi bị che, 0..1.
  /// Chỗ chạm cách nguồn trong khoảng này thì không tính là che: body của chính vật
  /// phát tiếng (thân xe đang nổ máy) không che tiếng của nó. Tăng cho vật lớn.
  f32 occlusion_margin = 0.5f;
};

/// Phát một lần `handle` tại `position`, nghe từ tai nghe 3D theo `desc`.
///
/// Mỗi tiếng 3D có giọng riêng nên âm lượng, trái phải và cao độ của nó được tính
/// lại mỗi frame từ vị trí của nó và của tai. Âm lượng cuối cùng còn nhân âm lượng
/// của sound và kênh của nó (sound_set_bus(), audio_set_bus_volume()). Tối đa 64
/// tiếng 3D cùng lúc; quá số đó thì tiếng phát một lần đã chạy lâu nhất bị cắt. Vị
/// trí không hữu hạn (NaN, vô cực) thì không phát.
/// @param ctx Context của engine.
/// @param handle Sound đã nạp.
/// @param position Nơi phát, trong thế giới.
/// @param desc Cách nghe.
/// @return Handle của tiếng, hoặc handle có id 0 nếu không phát được.
voice3d_handle sound_play3d(context &ctx, sound_handle handle, vec3 position, const sound3d_desc &desc = {});

/// Như bản trên, nhưng tiếng đi theo `entity` (vị trí njin::transform3d của nó)
/// cho đến khi phát xong. Entity bị hủy thì tiếng phát nốt ở chỗ cuối cùng.
/// @param ctx Context của engine.
/// @param handle Sound đã nạp.
/// @param entity Entity có njin::transform3d.
/// @param desc Cách nghe.
/// @return Handle của tiếng, hoặc handle có id 0 nếu không phát được.
voice3d_handle sound_play3d(context &ctx, sound_handle handle, entt::entity entity, const sound3d_desc &desc = {});

/// Phát lặp `handle` tại `position` cho đến khi voice3d_stop() (hoặc sound_stop()
/// cho sound đó): tiếng máy nổ, lửa tí tách, thác nước. Như sound_play_loop(), chỗ
/// nối có một quãng lặng cỡ một frame.
/// @param ctx Context của engine.
/// @param handle Sound đã nạp.
/// @param position Nơi phát, trong thế giới.
/// @param desc Cách nghe.
/// @return Handle của tiếng, hoặc handle có id 0 nếu không phát được.
voice3d_handle sound_loop3d(context &ctx, sound_handle handle, vec3 position, const sound3d_desc &desc = {});

/// Như bản trên, nhưng tiếng đi theo `entity`: tiếng máy gắn vào xe. Entity bị hủy
/// thì tiếng lặp dừng luôn.
/// @param ctx Context của engine.
/// @param handle Sound đã nạp.
/// @param entity Entity có njin::transform3d.
/// @param desc Cách nghe.
/// @return Handle của tiếng, hoặc handle có id 0 nếu không phát được.
voice3d_handle sound_loop3d(context &ctx, sound_handle handle, entt::entity entity, const sound3d_desc &desc = {});

/// Dời tiếng tới `position`, và thôi đi theo entity nếu đang đi theo. Không hữu
/// hạn thì bị bỏ qua.
/// @param ctx Context của engine.
/// @param voice Tiếng.
/// @param position Vị trí mới, trong thế giới.
void voice3d_set_position(context &ctx, voice3d_handle voice, vec3 position);

/// Cho tiếng đi theo `entity`, lệch `offset` so với vị trí của nó (trong thế giới,
/// không xoay theo entity).
/// @param ctx Context của engine.
/// @param voice Tiếng.
/// @param entity Entity có njin::transform3d.
/// @param offset Độ lệch.
void voice3d_attach(context &ctx, voice3d_handle voice, entt::entity entity, vec3 offset = {});

/// Đặt vận tốc của nguồn cho Doppler. Mặc định engine đo vận tốc từ quãng nguồn
/// đi mỗi frame; gọi hàm này thì engine thôi đo và dùng giá trị này (ví dụ lấy từ
/// body3d_velocity()) cho đến khi tiếng dừng.
/// @param ctx Context của engine.
/// @param voice Tiếng.
/// @param velocity Vận tốc, đơn vị mỗi giây.
void voice3d_set_velocity(context &ctx, voice3d_handle voice, vec3 velocity);

/// Đổi cách nghe của tiếng đang phát: âm lượng theo ga của xe, cao độ theo vòng
/// tua máy (vehicle3d_rpm()).
/// @param ctx Context của engine.
/// @param voice Tiếng.
/// @param desc Cách nghe mới.
void voice3d_set_desc(context &ctx, voice3d_handle voice, const sound3d_desc &desc);

/// Cách nghe đang dùng của tiếng. Sửa bản sao rồi voice3d_set_desc().
/// @param ctx Context của engine.
/// @param voice Tiếng.
/// @return Cách nghe, hoặc giá trị mặc định nếu handle không hợp lệ.
sound3d_desc voice3d_desc(const context &ctx, voice3d_handle voice);

/// Dừng tiếng. Handle không còn hợp lệ nữa.
/// @param ctx Context của engine.
/// @param voice Tiếng.
void voice3d_stop(context &ctx, voice3d_handle voice);

/// Tiếng còn đang phát không (tiếng lặp thì cho đến khi dừng).
/// @param ctx Context của engine.
/// @param voice Tiếng.
/// @return `true` nếu còn phát.
bool voice3d_playing(const context &ctx, voice3d_handle voice);

/// Những gì engine tính được cho một tiếng 3D ở lần cập nhật gần nhất, để debug
/// hoặc để vẽ chỉ báo tiếng động trên màn hình.
struct voice3d_mix {
  f32 volume = 0.0f;   ///< Âm lượng đang phát, đã nhân sound, kênh, khoảng cách, nón và vật che.
  f32 pan = 0.0f;      ///< Trái phải, -1 (trái) .. 1 (phải).
  f32 pitch = 1.0f;    ///< Cao độ đang phát, đã nhân Doppler.
  f32 distance = 0.0f; ///< Khoảng cách tới tai.
  bool occluded = false; ///< Tia từ tai tới nguồn có bị chặn không.
};

/// Âm lượng, trái phải và cao độ của tiếng ở lần cập nhật gần nhất.
/// @param ctx Context của engine.
/// @param voice Tiếng.
/// @return Kết quả, hoặc giá trị mặc định nếu handle không hợp lệ.
voice3d_mix voice3d_state(const context &ctx, voice3d_handle voice);
/// @}
} // namespace njin
