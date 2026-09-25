#pragma once
#include "_math.h"
#include "_types.h"
#include <array>
#include <vector>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_anim
/// @{

/// Thứ tự chạy các frame của một clip, giống tùy chọn "Animation Direction"
/// của tag trong Aseprite.
enum anim_direction {
  anim_forward,           ///< Từ frame đầu đến frame cuối.
  anim_reverse,           ///< Từ frame cuối về frame đầu.
  anim_ping_pong,         ///< Đi rồi về: 0 1 2 3 2 1, rồi lặp.
  anim_ping_pong_reverse, ///< Về rồi đi: 3 2 1 0 1 2, rồi lặp.
};

/// Mô tả một clip (một đoạn animation có tên) trong sprite sheet, dùng với
/// anim_sheet_add_clip().
struct anim_clip_desc {
  const char *name = nullptr; ///< Tên clip, duy nhất trong sheet.
  i32 from = 0;               ///< Frame đầu, tính từ 0.
  i32 to = 0;                 ///< Frame cuối, **gồm cả frame này**.
  anim_direction direction = anim_forward; ///< Thứ tự chạy.
  /// Số lượt chạy rồi dừng ở frame cuối. 0 là lặp mãi.
  i32 repeat = 0;
};

/// Nạp sprite sheet và các clip từ file JSON do Aseprite xuất ra.
///
/// Trong Aseprite: *File > Export Sprite Sheet*, tab *Output* bật **JSON
/// Data** và **Tags** (chọn "Array" hoặc "Hash" đều được), tab *Borders* tắt
/// **Trim**. Hoặc dòng lệnh:
/// @code{.sh}
/// aseprite -b player.aseprite --sheet player.png --data player.json --list-tags --format json-array
/// @endcode
///
/// Mỗi tag thành một clip cùng tên, giữ hướng chạy và số lần lặp của tag.
/// Thời lượng từng frame lấy đúng như trong Aseprite. Không có tag nào thì có
/// một clip tên `"default"` gồm mọi frame. Ảnh (`meta.image`) được tìm cạnh
/// file JSON và thuộc về sheet: unload sheet thì ảnh cũng được giải phóng.
/// @param ctx Context của engine.
/// @param json_path Đường dẫn file JSON.
/// @return Handle của sheet, hoặc handle id 0 nếu file thiếu hoặc lỗi (có ghi log).
anim_sheet_handle anim_sheet_load(njin_ctx &ctx, const char *json_path);

/// Tạo sprite sheet từ một ảnh chia lưới đều, không có clip nào.
///
/// Frame xếp theo hàng từ trái sang phải rồi từ trên xuống, đánh số từ 0, như
/// njin::sprite_anim. Thêm clip bằng anim_sheet_add_clip(). Ảnh **không**
/// thuộc về sheet: game tự unload nó.
/// @param ctx Context của engine.
/// @param texture Ảnh sprite sheet.
/// @param frame_size Kích thước một frame, pixel.
/// @param fps Số frame mỗi giây, dùng cho mọi frame.
/// @return Handle của sheet, hoặc handle id 0 nếu ảnh hoặc kích thước không hợp lệ.
anim_sheet_handle anim_sheet_grid(njin_ctx &ctx, texture_handle texture,
                                  vec2 frame_size, f32 fps);

/// Thêm một clip vào sheet.
/// @param ctx Context của engine.
/// @param sheet Sheet.
/// @param desc Mô tả clip.
/// @return `false` nếu sheet không hợp lệ, trùng tên, hoặc frame ngoài sheet.
bool anim_sheet_add_clip(njin_ctx &ctx, anim_sheet_handle sheet,
                         const anim_clip_desc &desc);

/// Giải phóng sheet, và ảnh của nó nếu sheet nạp từ Aseprite.
/// @param ctx Context của engine.
/// @param sheet Sheet. Handle không hợp lệ bị bỏ qua.
void anim_sheet_unload(njin_ctx &ctx, anim_sheet_handle sheet);

/// Tìm clip theo tên.
/// @param ctx Context của engine.
/// @param sheet Sheet.
/// @param name Tên clip.
/// @return Số thứ tự của clip, hoặc -1 nếu không có.
i32 anim_clip_find(const njin_ctx &ctx, anim_sheet_handle sheet,
                   const char *name);

/// Thời lượng một lượt chạy của clip, giây. Dùng để canh thời gian của đòn
/// đánh hay hiệu ứng theo animation.
/// @param ctx Context của engine.
/// @param sheet Sheet.
/// @param name Tên clip.
/// @return Tổng thời lượng các frame, hoặc 0 nếu không có clip.
f32 anim_clip_duration(const njin_ctx &ctx, anim_sheet_handle sheet,
                       const char *name);

/// Phép so sánh của một điều kiện chuyển trạng thái.
enum anim_cmp {
  anim_gt,      ///< Tham số lớn hơn `value`.
  anim_lt,      ///< Tham số nhỏ hơn `value`.
  anim_ge,      ///< Tham số lớn hơn hoặc bằng `value`.
  anim_le,      ///< Tham số nhỏ hơn hoặc bằng `value`.
  anim_eq,      ///< Tham số bằng `value`.
  anim_ne,      ///< Tham số khác `value`.
  anim_true,    ///< Tham số khác 0 (bool đang bật).
  anim_false,   ///< Tham số bằng 0 (bool đang tắt).
  /// Tham số được bật bằng animator_trigger() **trong frame này**. Bị tiêu thụ
  /// khi chuyển trạng thái, và tự tắt cuối frame nếu không dùng đến.
  anim_trigger,
};

/// Một điều kiện của chuyển trạng thái.
struct anim_cond {
  const char *param = nullptr; ///< Tên tham số.
  anim_cmp cmp = anim_true;    ///< Phép so sánh.
  f32 value = 0.0f;            ///< Giá trị so sánh (bỏ qua với true/false/trigger).
};

/// Một trạng thái của máy trạng thái: một clip được chạy khi ở trạng thái đó.
struct anim_state_desc {
  const char *name = nullptr; ///< Tên trạng thái, duy nhất trong graph.
  const char *clip = nullptr; ///< Tên clip trong sheet của graph.
  f32 speed = 1.0f;           ///< Tốc độ chạy clip. 2 là nhanh gấp đôi.
  /// Ghi đè số lượt của clip (xem anim_clip_desc::repeat). -1 là giữ như clip.
  i32 repeat = -1;
};

/// Một chuyển trạng thái: từ `from` sang `to` khi mọi điều kiện đúng.
///
/// Mỗi frame engine xét các chuyển theo **thứ tự khai báo** và dùng cái đầu
/// tiên thỏa mãn, nên đặt chuyển quan trọng (bị đánh, chết) lên trước.
struct anim_transition_desc {
  /// Trạng thái nguồn. null là "từ mọi trạng thái" (trừ chính `to`).
  const char *from = nullptr;
  const char *to = nullptr; ///< Trạng thái đích.
  std::vector<anim_cond> when{}; ///< Các điều kiện, phải đúng tất cả. Rỗng là luôn đúng.
  /// Chỉ chuyển khi clip hiện tại đã chạy hết ít nhất một lượt. Dùng cho đòn
  /// đánh: `{.from = "attack", .to = "idle", .after_finish = true}`.
  bool after_finish = false;
};

/// Mô tả một máy trạng thái animation, dùng với anim_graph_create().
///
/// @code
/// g.player_anim = njin::anim_graph_create(ctx, {
///     .sheet = sheet,
///     .states = {{.name = "idle", .clip = "idle"},
///                {.name = "run", .clip = "run"},
///                {.name = "jump", .clip = "jump", .repeat = 1},
///                {.name = "attack", .clip = "attack", .repeat = 1}},
///     .transitions = {
///         {.to = "attack", .when = {{"attack", njin::anim_trigger}}},
///         {.from = "attack", .to = "idle", .after_finish = true},
///         {.from = "idle", .to = "jump", .when = {{"grounded", njin::anim_false}}},
///         {.from = "run", .to = "jump", .when = {{"grounded", njin::anim_false}}},
///         {.from = "jump", .to = "idle", .when = {{"grounded", njin::anim_true}}},
///         {.from = "idle", .to = "run", .when = {{"speed", njin::anim_gt, 10}}},
///         {.from = "run", .to = "idle", .when = {{"speed", njin::anim_le, 10}}},
///     }});
/// @endcode
struct anim_graph_desc {
  anim_sheet_handle sheet{}; ///< Sheet chứa các clip.
  std::vector<anim_state_desc> states{}; ///< Các trạng thái.
  std::vector<anim_transition_desc> transitions{}; ///< Các chuyển trạng thái.
  const char *start = nullptr; ///< Trạng thái ban đầu. null là trạng thái đầu tiên.
};

/// Số tham số tối đa của một máy trạng thái.
inline constexpr i32 anim_max_params = 16;

/// Tạo một máy trạng thái animation. Nhiều entity dùng chung một graph, mỗi
/// entity có trạng thái và tham số riêng trong njin::animator.
/// @param ctx Context của engine.
/// @param desc Mô tả graph.
/// @return Handle của graph, hoặc handle id 0 nếu có tên clip hoặc trạng thái
/// không tồn tại, hay quá anim_max_params tham số (có ghi log).
anim_graph_handle anim_graph_create(njin_ctx &ctx, const anim_graph_desc &desc);

/// Chạy animation từ một sheet có clip, cho entity có sprite.
///
/// Module anim của engine, trong `phase_post_update`, xét chuyển trạng thái,
/// cho frame chạy theo delta() và ghi `sprite.texture`, `sprite.source`. Hai
/// cách dùng:
/// - **Có graph**: gán `graph`, rồi mỗi frame game chỉ cập nhật tham số bằng
///   animator_set(), animator_set_bool(), animator_trigger(). Graph tự chọn clip.
/// - **Không graph**: gán `sheet`, rồi gọi animator_play() với tên clip.
///
/// Đây là bản đầy đủ của njin::sprite_anim: thời lượng riêng từng frame, clip
/// có tên, chạy ngược và ping-pong. Đừng gắn cả hai lên một entity.
struct animator {
  anim_graph_handle graph{}; ///< Máy trạng thái. Có thì `sheet` bị bỏ qua.
  anim_sheet_handle sheet{}; ///< Sheet, khi không dùng graph.
  f32 speed = 1.0f;          ///< Tốc độ chung. 0 là đứng hình.
  bool playing = true;       ///< `false` để dừng ở frame hiện tại.

  /// @name Trạng thái (do engine quản lý, chỉ đọc)
  /// @{
  i32 state = -1;  ///< Trạng thái hiện tại trong graph, -1 là chưa bắt đầu.
  i32 clip = -1;   ///< Clip đang chạy, -1 là không có.
  i32 frame = -1;  ///< Frame đang hiện trong sheet, -1 là chưa có.
  i32 step = 0;    ///< Vị trí trong thứ tự chạy của clip.
  f32 time = 0.0f; ///< Thời gian đã ở frame hiện tại, giây.
  f32 state_time = 0.0f; ///< Thời gian đã ở trạng thái (hoặc clip) hiện tại, giây.
  i32 loops = 0;   ///< Số lượt clip đã chạy xong.
  i32 repeat = 0;  ///< Số lượt của clip hiện tại, 0 là lặp mãi.
  bool finished = false; ///< Clip không lặp đã chạy hết và dừng ở frame cuối.
  std::array<f32, anim_max_params> params{}; ///< Giá trị các tham số của graph.
  /// @}
};

/// Chuyển ngay sang một clip (không graph) hoặc một trạng thái (có graph).
///
/// Nếu đang chạy đúng nó và chưa hết thì không làm gì, nên gọi mỗi frame
/// được. Với graph, các chuyển trạng thái vẫn được xét từ frame sau.
/// @param ctx Context của engine.
/// @param anim Animator.
/// @param name Tên clip, hoặc tên trạng thái nếu có graph.
/// @param restart Bắt đầu lại kể cả khi đang chạy đúng nó.
/// @return `false` nếu không tìm thấy tên.
bool animator_play(const njin_ctx &ctx, animator &anim, const char *name,
                   bool restart = false);

/// Đặt một tham số số của graph, ví dụ tốc độ chạy.
/// @param ctx Context của engine.
/// @param anim Animator.
/// @param param Tên tham số (tên dùng trong anim_cond).
/// @param value Giá trị.
void animator_set(const njin_ctx &ctx, animator &anim, const char *param,
                  f32 value);

/// Đặt một tham số bool của graph, ví dụ đang đứng trên đất.
/// @param ctx Context của engine.
/// @param anim Animator.
/// @param param Tên tham số.
/// @param value Giá trị.
void animator_set_bool(const njin_ctx &ctx, animator &anim, const char *param,
                       bool value);

/// Bật một trigger của graph cho frame này, ví dụ vừa bấm nút đánh.
/// @param ctx Context của engine.
/// @param anim Animator.
/// @param param Tên tham số (dùng với anim_trigger).
void animator_trigger(const njin_ctx &ctx, animator &anim, const char *param);

/// Đang ở trạng thái (có graph) hoặc clip (không graph) này không.
/// @param ctx Context của engine.
/// @param anim Animator.
/// @param name Tên trạng thái hoặc clip.
/// @return `true` nếu đúng.
bool animator_in(const njin_ctx &ctx, const animator &anim, const char *name);

/// Tên trạng thái (có graph) hoặc clip (không graph) hiện tại.
/// @param ctx Context của engine.
/// @param anim Animator.
/// @return Tên, hoặc chuỗi rỗng nếu chưa có. Không bao giờ null.
const char *animator_current(const njin_ctx &ctx, const animator &anim);
/// @}
} // namespace njin
