#pragma once
#include "_math.h"
#include "_types.h"
#include <entt/entity/fwd.hpp>
#include <functional>
#include <initializer_list>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace njin {
struct context;

/// @addtogroup grp_script
/// @{

/// Một giá trị đi qua lại giữa C++ và Lua: nil, bool, số, chuỗi, vec2, vec3 hay
/// entity. Số của Lua (nguyên hay thực) đều thành `f64`; entity là số nguyên bên Lua.
using script_value = std::variant<std::monostate, bool, f64, std::string, vec2, vec3, entt::entity>;

/// Tùy chọn của máy Lua, cho script_init().
struct script_desc {
  /// Cho script đọc ghi file và chạy file khác (`io`, `dofile`, `loadfile`,
  /// `os.remove`...). Mặc định tắt: script chỉ chạy được trong engine, không đụng
  /// tới máy người chơi. Chỉ bật cho công cụ tin cậy (bản dựng, trình sửa màn).
  /// `os.execute`, `io.popen` và nạp thư viện C (`package.loadlib`) không bao giờ có.
  bool allow_io = false;
};

/// Tạo máy Lua với tùy chọn `desc`. Không bắt buộc: lần dùng đầu của mọi hàm
/// script_* tự tạo máy với tùy chọn mặc định. Gọi lần hai thì bỏ qua (máy đã có).
/// @param ctx Context của engine.
/// @param desc Tùy chọn.
void script_init(context &ctx, const script_desc &desc = {});

/// Chạy một file Lua (đường dẫn như mọi tài nguyên: cạnh file chạy hay thư mục
/// làm việc). Hàm và biến toàn cục nó tạo dùng được bằng script_call() và
/// script_get_global(). Lỗi (cú pháp hay lúc chạy) được ghi log kèm file và dòng,
/// không làm game dừng.
/// @param ctx Context của engine.
/// @param path File `.lua`.
/// @return `true` nếu chạy hết không lỗi.
bool script_run_file(context &ctx, const char *path);

/// Chạy một đoạn mã Lua. Như script_run_file().
/// @param ctx Context của engine.
/// @param code Mã Lua.
/// @param name Tên hiện trong thông báo lỗi.
/// @return `true` nếu chạy hết không lỗi.
bool script_run_string(context &ctx, const char *code, const char *name = "string");

/// Kết quả của script_call().
struct script_result {
  bool ok = false;      ///< Hàm có và chạy không lỗi.
  script_value value{}; ///< Giá trị trả về đầu tiên (nil nếu không trả gì).
};

/// Gọi một hàm Lua toàn cục theo tên. Tên có dấu chấm (`"enemy.spawn"`) tìm trong
/// bảng. Không có hàm hay hàm lỗi thì ghi log và `ok` là `false`.
/// @param ctx Context của engine.
/// @param function Tên hàm.
/// @param args Tham số.
/// @return Kết quả.
script_result script_call(context &ctx, const char *function, std::initializer_list<script_value> args = {});

/// Như bản trên, với tham số trong một mảng.
/// @param ctx Context của engine.
/// @param function Tên hàm.
/// @param args Tham số.
/// @return Kết quả.
script_result script_call(context &ctx, const char *function, std::span<const script_value> args);

/// Hàm của game gọi được từ Lua, dạng chung: nhận mọi tham số, trả một giá trị.
using script_fn = std::function<script_value(context &, std::span<const script_value>)>;

/// Đăng ký một hàm C++ dưới tên `name` (toàn cục bên Lua; có dấu chấm thì đặt
/// trong bảng, tạo bảng khi cần). Lỗi C++ ném ra trong hàm thành lỗi Lua có file và
/// dòng của chỗ gọi.
/// @param ctx Context của engine.
/// @param name Tên bên Lua.
/// @param fn Hàm.
void script_register(context &ctx, const char *name, script_fn fn);

namespace script_detail {
template <class> inline constexpr bool always_false = false;

/// Đổi một script_value sang kiểu tham số của hàm game. Sai kiểu thì là giá trị mặc định.
template <class T> std::remove_cvref_t<T> to(const script_value &v) {
  using U = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<U, bool>) {
    if (const bool *b = std::get_if<bool>(&v))
      return *b;
    if (const f64 *d = std::get_if<f64>(&v))
      return *d != 0.0;
    return false;
  } else if constexpr (std::is_same_v<U, entt::entity>) {
    if (const entt::entity *e = std::get_if<entt::entity>(&v))
      return *e;
    if (const f64 *d = std::get_if<f64>(&v))
      return entt::entity((u32)*d);
    return entt::entity(~u32{0});
  } else if constexpr (std::is_arithmetic_v<U>) {
    if (const f64 *d = std::get_if<f64>(&v))
      return (U)*d;
    if (const bool *b = std::get_if<bool>(&v))
      return (U)(*b ? 1 : 0);
    return U{};
  } else if constexpr (std::is_same_v<U, std::string> || std::is_same_v<U, vec2> || std::is_same_v<U, vec3>) {
    if (const U *p = std::get_if<U>(&v))
      return *p;
    return U{};
  } else if constexpr (std::is_same_v<U, script_value>) {
    return v;
  } else {
    static_assert(always_false<U>, "script_register: kiểu tham số không hỗ trợ");
  }
}

/// Đổi giá trị trả về của hàm game thành script_value.
template <class T> script_value from(T &&value) {
  using U = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<U, script_value>)
    return std::forward<T>(value);
  else if constexpr (std::is_same_v<U, bool> || std::is_same_v<U, vec2> || std::is_same_v<U, vec3> ||
                     std::is_same_v<U, entt::entity> || std::is_same_v<U, std::string>)
    return script_value{std::forward<T>(value)};
  else if constexpr (std::is_arithmetic_v<U>)
    return script_value{(f64)value};
  else if constexpr (std::is_convertible_v<U, const char *>)
    return script_value{std::string(value)};
  else
    static_assert(always_false<U>, "script_register: kiểu trả về không hỗ trợ");
}

inline const script_value &arg(std::span<const script_value> args, usize i) {
  static const script_value none{};
  return i < args.size() ? args[i] : none;
}

template <class R, class... A, usize... I>
script_value call(const std::function<R(context &, A...)> &f, context &ctx, std::span<const script_value> args,
                  std::index_sequence<I...>) {
  if constexpr (std::is_void_v<R>) {
    f(ctx, to<A>(arg(args, I))...);
    return {};
  } else {
    return from(f(ctx, to<A>(arg(args, I))...));
  }
}

template <class R, class... A, usize... I>
script_value call(const std::function<R(A...)> &f, std::span<const script_value> args, std::index_sequence<I...>) {
  if constexpr (std::is_void_v<R>) {
    f(to<A>(arg(args, I))...);
    return {};
  } else {
    return from(f(to<A>(arg(args, I))...));
  }
}

template <class R, class... A> script_fn wrap(std::function<R(context &, A...)> f) {
  return [f = std::move(f)](context &ctx, std::span<const script_value> args) {
    return call(f, ctx, args, std::index_sequence_for<A...>{});
  };
}

template <class R, class... A> script_fn wrap(std::function<R(A...)> f) {
  return [f = std::move(f)](context &, std::span<const script_value> args) {
    return call(f, args, std::index_sequence_for<A...>{});
  };
}
} // namespace script_detail

/// Đăng ký một hàm C++ với tham số và kiểu trả về thường: `bool`, số, `std::string`,
/// vec2, vec3, `entt::entity` hay script_value, có thể nhận `context &` ở đầu. Tham
/// số Lua thiếu hay sai kiểu thành giá trị mặc định của kiểu đó.
/// @code
/// njin::script_register(ctx, "add_score", [](njin::context &c, int points) { score += points; });
/// njin::script_register(ctx, "distance", [](njin::vec2 a, njin::vec2 b) { return njin::length(b - a); });
/// @endcode
/// @param ctx Context của engine.
/// @param name Tên bên Lua.
/// @param fn Hàm, lambda hay con trỏ hàm (không phải lambda generic).
template <class F>
  requires(!std::is_convertible_v<F, script_fn>)
void script_register(context &ctx, const char *name, F &&fn) {
  script_register(ctx, name, script_detail::wrap(std::function{std::forward<F>(fn)}));
}

/// Đặt một biến toàn cục bên Lua (tên có dấu chấm đặt trong bảng).
/// @param ctx Context của engine.
/// @param name Tên.
/// @param value Giá trị.
void script_set_global(context &ctx, const char *name, const script_value &value);

/// Đọc một biến toàn cục bên Lua. Bảng, hàm và giá trị không đổi được thì là nil.
/// @param ctx Context của engine.
/// @param name Tên (có dấu chấm thì tìm trong bảng).
/// @return Giá trị.
script_value script_get_global(context &ctx, const char *name);

/// Gắn một script vào entity. File phải trả về một bảng (như một lớp); mỗi entity
/// có một bảng `self` riêng mà các hàm của bảng đó nhận làm tham số đầu:
///
/// - `on_start(self)`: một lần, ở lần cập nhật đầu tiên sau khi gắn.
/// - `on_update(self, dt)`: mỗi frame, trong `phase_update` (dt là delta()).
/// - `on_render(self)`: mỗi frame, trong `phase_render`, để vẽ (njin.draw_*).
/// - `on_destroy(self)`: khi entity bị hủy hay script bị gỡ. Đừng hủy entity khác
///   ở đây.
/// - `on_reload(self)`: sau khi file được nạp lại (hot reload).
///
/// `self.entity` là entity. Mọi trường game ghi vào `self` được giữ qua hot reload:
/// file nạp lại chỉ thay các hàm. Nhiều entity dùng chung một file thì file chỉ nạp
/// một lần. Gắn lần hai thì script cũ được gỡ trước (có `on_destroy`).
/// @param ctx Context của engine.
/// @param entity Entity.
/// @param path File `.lua`.
/// @return `true` nếu file nạp được và trả về một bảng.
bool script_attach(context &ctx, entt::entity entity, const char *path);

/// Gỡ script khỏi entity (gọi `on_destroy`). Không có script thì bỏ qua.
/// @param ctx Context của engine.
/// @param entity Entity.
void script_detach(context &ctx, entt::entity entity);

/// Entity có script không. @param ctx Context của engine. @param entity Entity.
/// @return `true` nếu có.
bool script_attached(const context &ctx, entt::entity entity);

/// Đọc trường `key` của bảng `self` của script gắn trên entity.
/// @param ctx Context của engine.
/// @param entity Entity.
/// @param key Tên trường.
/// @return Giá trị, nil nếu không có script hay trường.
script_value script_field(context &ctx, entt::entity entity, const char *key);

/// Ghi trường `key` của bảng `self`. Không có script thì bỏ qua.
/// @param ctx Context của engine.
/// @param entity Entity.
/// @param key Tên trường.
/// @param value Giá trị.
void script_set_field(context &ctx, entt::entity entity, const char *key, const script_value &value);

/// Số entity đang có script. @param ctx Context của engine. @return Số entity.
i32 script_count(const context &ctx);
/// @}
} // namespace njin
