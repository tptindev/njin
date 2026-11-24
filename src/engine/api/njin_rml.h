#pragma once
// Thuộc feature rmlui: game cần bật nó bằng njin_use(<game> FEATURES rmlui)
// (cmake/njin.cmake), hoặc link njin::rt.
#if !defined(NJIN_ENGINE_INTERNAL) && !defined(NJIN_FEATURE_RMLUI)
#error "njin_rml.h is part of the njin feature 'rmlui', which this game does not use: add it with njin_use(<game> FEATURES rmlui)"
#endif
#include "_types.h"
#include <functional>
#include <string>

namespace njin {
struct context;

/// @addtogroup grp_rml
/// @{

/// Một tài liệu RmlUi đã nạp (file `.rml`), trả về từ rml_document_load().
struct rml_document_handle {
  u32 id = 0; ///< 0 là không hợp lệ.
  /// Handle có hợp lệ không.
  explicit operator bool() const { return id != 0; }
};

/// Một data model: các giá trị và sự kiện mà tài liệu gắn vào bằng
/// `data-model="tên"`, `{{biến}}`, `data-if`, `data-event-click`...
struct rml_model_handle {
  u32 id = 0; ///< 0 là không hợp lệ.
  /// Handle có hợp lệ không.
  explicit operator bool() const { return id != 0; }
};

/// Nạp một font cho RmlUi (`.ttf`, `.otf`), tìm như mọi asset. Tài liệu dùng nó
/// bằng `font-family` trong RCSS, theo tên họ font ghi trong file. Font dùng
/// chung cho mọi tài liệu và nạp được trước cả khi tài liệu nào mở.
/// @param ctx Context của engine.
/// @param path File font.
/// @param fallback Dùng font này cho các ký tự font của tài liệu không có (ký
/// hiệu, emoji, chữ của ngôn ngữ khác).
/// @return `false` nếu file thiếu hay không đọc được (cảnh báo nói vì sao).
bool rml_font_load(context &ctx, const char *path, bool fallback = false);

/// Tạo (hoặc lấy lại) data model tên `name`. Tạo nó và gắn mọi giá trị **trước**
/// khi nạp tài liệu dùng nó: tài liệu đọc model lúc nạp.
///
/// Engine đọc lại mọi getter mỗi frame, nên `{{biến}}` luôn theo đúng trạng thái
/// của game mà game không phải báo khi đổi.
/// @code
/// auto hud = njin::rml_model(ctx, "hud");
/// njin::rml_bind_int(ctx, hud, "coins", [&] { return game.coins; });
/// njin::rml_bind_event(ctx, hud, "resume", [&](njin::context &c) { game.paused = false; });
/// auto doc = njin::rml_document_load(ctx, "ui/hud.rml");
/// @endcode
/// @param ctx Context của engine.
/// @param name Tên model, đúng như `data-model` trong tài liệu.
/// @return Handle của model.
rml_model_handle rml_model(context &ctx, const char *name);

/// Gắn một giá trị đúng/sai vào model: `data-if="name"`, `data-checked`...
/// @param ctx Context của engine.
/// @param model Model (rml_model()).
/// @param name Tên biến trong tài liệu.
/// @param get Đọc giá trị, gọi mỗi frame.
/// @param set Ghi giá trị khi tài liệu đổi nó (ô đánh dấu có `data-checked`).
/// Để trống là chỉ đọc.
void rml_bind_bool(context &ctx, rml_model_handle model, const char *name, std::function<bool()> get,
                   std::function<void(bool)> set = {});

/// Gắn một số nguyên vào model: `{{name}}`, `data-value`...
/// @param ctx Context của engine.
/// @param model Model (rml_model()).
/// @param name Tên biến trong tài liệu.
/// @param get Đọc giá trị, gọi mỗi frame.
/// @param set Ghi giá trị khi tài liệu đổi nó. Để trống là chỉ đọc.
void rml_bind_int(context &ctx, rml_model_handle model, const char *name, std::function<i64()> get,
                  std::function<void(i64)> set = {});

/// Gắn một số thực vào model: `{{name}}`, thanh trượt có `data-value`...
/// @param ctx Context của engine.
/// @param model Model (rml_model()).
/// @param name Tên biến trong tài liệu.
/// @param get Đọc giá trị, gọi mỗi frame.
/// @param set Ghi giá trị khi tài liệu đổi nó. Để trống là chỉ đọc.
void rml_bind_float(context &ctx, rml_model_handle model, const char *name, std::function<f64()> get,
                    std::function<void(f64)> set = {});

/// Gắn một chuỗi (UTF-8) vào model: `{{name}}`, ô nhập chữ có `data-value`...
/// @param ctx Context của engine.
/// @param model Model (rml_model()).
/// @param name Tên biến trong tài liệu.
/// @param get Đọc giá trị, gọi mỗi frame.
/// @param set Ghi giá trị khi tài liệu đổi nó. Để trống là chỉ đọc.
void rml_bind_string(context &ctx, rml_model_handle model, const char *name, std::function<std::string()> get,
                     std::function<void(const std::string &)> set = {});

/// Gắn một sự kiện vào model: `data-event-click="name"` gọi `on` khi phần tử
/// được bấm (hoặc sự kiện khác theo tên thuộc tính).
/// @param ctx Context của engine.
/// @param model Model (rml_model()).
/// @param name Tên sự kiện trong tài liệu.
/// @param on Gọi khi sự kiện xảy ra, trong frame đó, trước `phase_post_update`.
void rml_bind_event(context &ctx, rml_model_handle model, const char *name, std::function<void(context &)> on);

/// Nạp một tài liệu (`.rml`) và hiện nó. Stylesheet (`.rcss`), ảnh và template
/// nó trỏ tới được tìm tương đối với file tài liệu.
///
/// RmlUi vẽ trên cùng, trên thế giới và UI của engine, theo pixel của cửa sổ
/// (không theo window_set_virtual_size()); đơn vị `dp` của RCSS theo độ phân giải
/// màn hình. Chuột hay phím mà tài liệu dùng (bấm vào nút, gõ vào ô nhập) không
/// tới game: mouse_pressed() và key_pressed() trả về `false` cho chúng.
///
/// Cần renderer SDL3/bgfx (mặc định); trên renderer OpenGL cũ thì báo lỗi.
/// @param ctx Context của engine.
/// @param path File `.rml`.
/// @return Handle, hoặc handle không hợp lệ nếu file thiếu hay sai (cảnh báo nói vì sao).
rml_document_handle rml_document_load(context &ctx, const char *path);

/// Hiện hay ẩn tài liệu. Tài liệu ẩn không vẽ và không nhận chuột, phím.
/// @param ctx Context của engine.
/// @param document Tài liệu.
/// @param visible `true` là hiện.
void rml_document_show(context &ctx, rml_document_handle document, bool visible = true);

/// Tài liệu có đang hiện không.
/// @param ctx Context của engine.
/// @param document Tài liệu.
/// @return `true` nếu tài liệu hợp lệ và đang hiện.
bool rml_document_visible(const context &ctx, rml_document_handle document);

/// Đóng tài liệu. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param document Tài liệu.
void rml_document_close(context &ctx, rml_document_handle document);

/// Gọi `on` khi sự kiện `event` (`"click"`, `"change"`, `"mouseover"`...) xảy ra
/// ở phần tử có `id="element_id"` của tài liệu, hay ở phần tử con của nó.
/// @param ctx Context của engine.
/// @param document Tài liệu.
/// @param element_id `id` của phần tử.
/// @param event Tên sự kiện của RmlUi.
/// @param on Gọi khi sự kiện xảy ra.
/// @return `false` nếu tài liệu không có phần tử đó.
bool rml_on(context &ctx, rml_document_handle document, const char *element_id, const char *event,
            std::function<void(context &)> on);

/// Thay nội dung của phần tử có `id="element_id"` bằng đoạn RML `rml` (chữ hay
/// phần tử con). Data model thường tiện hơn cho giá trị đổi mỗi frame.
/// @param ctx Context của engine.
/// @param document Tài liệu.
/// @param element_id `id` của phần tử.
/// @param rml Nội dung mới.
/// @return `false` nếu tài liệu không có phần tử đó.
bool rml_set_inner(context &ctx, rml_document_handle document, const char *element_id, const char *rml);

/// Chuột đang ở trên một phần tử của tài liệu đang hiện (hay đang kéo nó), tức là
/// lúc này chuột thuộc về UI chứ không thuộc về game.
/// @param ctx Context của engine.
/// @return `true` nếu chuột đang thuộc về UI.
bool rml_wants_mouse(const context &ctx);

/// Bật hay tắt bộ gỡ lỗi trực quan của RmlUi: cây phần tử, style, sự kiện.
/// @param ctx Context của engine.
/// @param visible `true` là hiện.
void rml_debugger(context &ctx, bool visible);

/// @}
} // namespace njin
