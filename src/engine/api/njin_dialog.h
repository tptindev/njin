#pragma once
#include "_math.h"
#include "_types.h"
#include "njin_json.h"
#include "njin_ui.h"
#include <functional>
#include <string>
#include <vector>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_dialog
/// @{

/// Một lựa chọn trả lời trong hộp thoại.
struct dialog_choice {
  std::string text;  ///< Chữ hiện. Bắt đầu bằng `@` là khóa dịch (xem tr()).
  std::string next;  ///< Nút đi tới khi chọn. Rỗng là kết thúc hội thoại.
  std::string event; ///< Tên event gửi khi chọn (njin::dialog_event). Có thể rỗng.
  std::string cond;  ///< Chỉ hiện khi điều kiện này đúng (xem dialog_set_condition()).
};

/// Một câu thoại: ai nói, nói gì, rồi đi đâu.
struct dialog_node {
  std::string id;       ///< Tên nút, để `next` trỏ tới.
  std::string speaker;  ///< Tên người nói. Rỗng là lời dẫn. `@` là khóa dịch.
  std::string text;     ///< Nội dung. `@` là khóa dịch. Tự xuống dòng.
  std::string portrait; ///< Tên chân dung đã đăng ký bằng dialog_portrait(). Có thể rỗng.
  std::string next;     ///< Nút kế tiếp khi không có lựa chọn. Rỗng là hết.
  std::string event;    ///< Event gửi khi câu này hiện ra. Có thể rỗng.
  std::string cond;     ///< Bỏ qua câu này (đi thẳng tới `next`) khi điều kiện sai.
  std::vector<dialog_choice> choices; ///< Các lựa chọn, hiện sau khi chữ chạy xong.
};

/// Một đoạn hội thoại: nhiều nút nối với nhau.
///
/// Thường viết bằng JSON và nạp bằng dialog_load():
/// @code{.json}
/// { "start": "hi", "nodes": [
///   { "id": "hi", "speaker": "Ông lão", "portrait": "oldman",
///     "text": "Đi một mình thì nguy hiểm lắm. Cầm lấy cái này.", "next": "ask" },
///   { "id": "ask", "speaker": "Ông lão", "text": "Con có muốn nghe chuyện không?",
///     "choices": [ { "text": "Có ạ", "next": "story" },
///                  { "text": "Để sau", "event": "refused" } ] },
///   { "id": "story", "text": "...", "event": "got_sword" } ] }
/// @endcode
/// Một nút không có `id` thì mặc định đi tới nút ngay sau nó trong danh sách.
struct dialog_script {
  std::vector<dialog_node> nodes; ///< Các nút.
  std::string start;              ///< Nút bắt đầu. Rỗng là nút đầu tiên.
};

/// Đọc một đoạn hội thoại từ JSON (dạng ở njin::dialog_script).
/// @param json Dữ liệu.
/// @param out Nhận đoạn hội thoại.
/// @return `false` nếu thiếu `nodes`.
bool dialog_parse(const json_value &json, dialog_script &out);

/// Nạp một đoạn hội thoại từ file JSON.
/// @param path Đường dẫn.
/// @param out Nhận đoạn hội thoại.
/// @return `false` nếu không đọc được (có ghi log).
bool dialog_load(const char *path, dialog_script &out);

/// Giao diện của hộp thoại. Lấy bản mặc định bằng dialog_default_style().
///
/// Hộp, chữ và lựa chọn vẽ bằng `ui_look` như UI, nên dùng được ảnh 9-slice
/// và shader riêng.
struct dialog_style {
  font_handle font{};       ///< Phông chữ. Mặc định là phông của UI.
  f32 font_size = 26.0f;    ///< Cỡ chữ, pixel.
  f32 scale = 1.0f;         ///< Nhân mọi kích thước.
  i32 lines = 3;            ///< Số dòng chữ của hộp.
  f32 margin = 24.0f;       ///< Khoảng từ mép màn hình.
  f32 padding = 18.0f;      ///< Khoảng từ mép hộp vào chữ.
  f32 max_width = 1100.0f;  ///< Chiều rộng tối đa của hộp.
  bool top = false;         ///< Đặt hộp ở trên màn hình thay vì dưới.
  f32 chars_per_second = 45.0f; ///< Tốc độ chữ chạy. 0 là hiện ngay cả câu.
  ui_look box{};            ///< Nền hộp (`normal`) và màu chữ (`text`).
  ui_look name{};           ///< Thẻ tên người nói: nền (`normal`) và màu chữ (`text`).
  ui_look choice{};         ///< Lựa chọn: `normal`, `focused` và màu chữ tương ứng.
  vec2 portrait_size{0.0f, 0.0f}; ///< Kích thước chân dung. 0 là vừa chiều cao chữ.
  sound_handle sound_blip{};   ///< Phát khi chữ chạy (mỗi vài ký tự). Có thể để trống.
  sound_handle sound_next{};   ///< Phát khi sang câu hoặc chọn.
  /// Dừng thời gian của game (time_set_paused()) khi hộp thoại đang mở.
  bool pause_game = false;
  /// Action cũng dùng để sang câu, ngoài Enter, Space, nút A và chuột trái.
  action_handle advance{};
};

/// Giao diện mặc định: hộp tối ở dưới, cùng tông với UI mặc định.
/// @return Style mặc định.
dialog_style dialog_default_style();

/// Đặt giao diện cho hộp thoại. @param ctx Context của engine. @param style Style.
void dialog_set_style(njin_ctx &ctx, const dialog_style &style);

/// Giao diện hiện tại. @param ctx Context của engine. @return Style.
dialog_style dialog_get_style(const njin_ctx &ctx);

/// Đăng ký một chân dung để các câu thoại gọi tên (`"portrait": "oldman"`).
/// @param ctx Context của engine.
/// @param name Tên.
/// @param texture Ảnh.
/// @param source Vùng trong ảnh. Kích thước 0 là cả ảnh.
void dialog_portrait(njin_ctx &ctx, const char *name, texture_handle texture, rect source = {});

/// Hàm kiểm tra điều kiện `cond` của câu thoại và lựa chọn: nhận chuỗi điều
/// kiện, trả về đúng hay sai. Game tự quyết định cú pháp, ví dụ tên một cờ
/// trong save game (`"has_key"`), có `!` ở đầu để phủ định.
/// @param ctx Context của engine.
/// @param fn Hàm kiểm tra. Để trống thì mọi điều kiện đều đúng.
void dialog_set_condition(njin_ctx &ctx, std::function<bool(njin_ctx &, const std::string &)> fn);

/// Bắt đầu một đoạn hội thoại. Hộp thoại được engine vẽ và điều khiển cho tới
/// khi hết; game chỉ cần nghe event.
///
/// Trong lúc mở, hộp thoại nhận Enter, Space, nút A, chuột trái (và mũi tên
/// khi có lựa chọn), nên game không thấy các phím đó. Phím di chuyển của game
/// thì không bị chặn: kiểm tra dialog_active() hoặc bật dialog_style::pause_game.
/// @param ctx Context của engine.
/// @param script Đoạn hội thoại. Được chép lại, nên có thể hủy ngay sau đó.
/// @param start Nút bắt đầu, hoặc null cho `script.start`.
void dialog_start(njin_ctx &ctx, const dialog_script &script, const char *start = nullptr);

/// Nói một câu lẻ, không cần kịch bản: biển báo, đồ vật.
/// @param ctx Context của engine.
/// @param speaker Người nói, có thể null.
/// @param text Nội dung.
/// @param portrait Tên chân dung, có thể null.
void dialog_say(njin_ctx &ctx, const char *speaker, const char *text, const char *portrait = nullptr);

/// Hộp thoại có đang mở không. @param ctx Context của engine. @return `true` nếu đang mở.
bool dialog_active(const njin_ctx &ctx);

/// Đóng hộp thoại ngay (gửi njin::dialog_ended). @param ctx Context của engine.
void dialog_stop(njin_ctx &ctx);

/// Event: một câu thoại hoặc lựa chọn có `event` vừa được kích hoạt.
struct dialog_event {
  std::string name; ///< Tên event trong kịch bản.
  std::string node; ///< Nút phát ra nó.
};

/// Event: hội thoại vừa kết thúc.
struct dialog_ended {
  std::string last_node; ///< Nút cuối cùng đã hiện.
};
/// @}
} // namespace njin
