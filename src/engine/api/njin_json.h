#pragma once
#include "_types.h"
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace njin {
/// @addtogroup grp_json
/// @{

/// Một giá trị JSON: null, bool, số, chuỗi, mảng hoặc object. Dùng cho save
/// game, file cấu hình, và thuộc tính đọc từ Tiled, LDtk.
///
/// Đọc thì an toàn với dữ liệu thiếu: `doc["player"]["hp"]` trên một khóa
/// không có trả về giá trị null chứ không lỗi, và `number_or()` trả về giá
/// trị dự phòng. Ghi thì dựng bằng set() và push():
/// @code
/// njin::json_value save = njin::json_value::make_object();
/// save.set("level", 3).set("hp", 7.5).set("name", "An");
/// njin::json_value inventory = njin::json_value::make_array();
/// inventory.push("sword").push("potion");
/// save.set("inventory", std::move(inventory));
/// njin::json_save(njin::save_path(ctx, "save.json").c_str(), save);
///
/// njin::json_value loaded;
/// if (njin::json_load(njin::save_path(ctx, "save.json").c_str(), loaded))
///   level = loaded["level"].int_or(1);
/// @endcode
///
/// Thành viên của object giữ đúng thứ tự trong file (và thứ tự set()), nên
/// ghi lại một file vừa đọc không làm xáo trộn nó.
struct json_value {
  /// Loại giá trị.
  enum kind_t {
    null,    ///< Không có giá trị. Cũng là kết quả khi đọc khóa không tồn tại.
    boolean, ///< `true` hoặc `false`, trong `b`.
    number,  ///< Số, trong `num`.
    string,  ///< Chuỗi UTF-8, trong `str`.
    array,   ///< Mảng, trong `items`.
    object,  ///< Object, trong `members`.
  };
  kind_t kind = null; ///< Loại của giá trị này.
  bool b = false;     ///< Giá trị khi `kind == boolean`.
  f64 num = 0.0;      ///< Giá trị khi `kind == number`.
  std::string str;    ///< Giá trị khi `kind == string`.
  std::vector<json_value> items; ///< Phần tử khi `kind == array`.
  std::vector<std::pair<std::string, json_value>> members; ///< Thành viên khi `kind == object`, theo thứ tự.

  json_value() = default;
  /// Giá trị bool. @param v Giá trị.
  json_value(bool v) : kind(boolean), b(v) {}
  /// Giá trị số. @param v Giá trị.
  json_value(i32 v) : kind(number), num(v) {}
  /// Giá trị số. @param v Giá trị.
  json_value(i64 v) : kind(number), num((f64)v) {}
  /// Giá trị số. @param v Giá trị.
  json_value(u32 v) : kind(number), num(v) {}
  /// Giá trị số. @param v Giá trị.
  json_value(f32 v) : kind(number), num(v) {}
  /// Giá trị số. @param v Giá trị.
  json_value(f64 v) : kind(number), num(v) {}
  /// Chuỗi. null trở thành giá trị null. @param v Giá trị.
  json_value(const char *v) : kind(v != nullptr ? string : null), str(v != nullptr ? v : "") {}
  /// Chuỗi. @param v Giá trị.
  json_value(std::string v) : kind(string), str(std::move(v)) {}

  /// Một object rỗng. @return Object không có thành viên.
  static json_value make_object() {
    json_value v;
    v.kind = kind_t::object;
    return v;
  }
  /// Một mảng rỗng. @return Mảng không có phần tử.
  static json_value make_array() {
    json_value v;
    v.kind = kind_t::array;
    return v;
  }

  /// Có đúng loại `k` không. @param k Loại. @return `true` nếu đúng.
  bool is(kind_t k) const { return kind == k; }

  /// Thành viên có tên `key`. Đọc khóa không có, hoặc đọc trên giá trị không
  /// phải object, trả về một giá trị null dùng chung (không bao giờ lỗi).
  /// @param key Tên thành viên.
  /// @return Thành viên, hoặc giá trị null.
  const json_value &operator[](std::string_view key) const;

  /// Phần tử thứ `index` của mảng, hoặc giá trị null nếu ngoài mảng.
  /// @param index Vị trí, từ 0.
  /// @return Phần tử, hoặc giá trị null.
  const json_value &operator[](usize index) const;

  /// Thành viên có tên `key` để sửa, hoặc null nếu không có.
  /// @param key Tên thành viên.
  /// @return Con trỏ tới thành viên, hoặc null.
  json_value *find(std::string_view key);

  /// Có thành viên tên `key` không. @param key Tên. @return `true` nếu có.
  bool has(std::string_view key) const;

  /// Số phần tử (mảng) hoặc số thành viên (object), 0 với loại khác.
  /// @return Kích thước.
  usize size() const;

  /// Đặt thành viên `key` (thay nếu đã có, thêm vào cuối nếu chưa). Giá trị
  /// null (chưa phải object) tự trở thành object.
  /// @param key Tên thành viên.
  /// @param value Giá trị.
  /// @return Chính object này, để gọi nối tiếp.
  json_value &set(std::string_view key, json_value value);

  /// Thêm phần tử vào cuối mảng. Giá trị null tự trở thành mảng.
  /// @param value Giá trị.
  /// @return Chính mảng này, để gọi nối tiếp.
  json_value &push(json_value value);

  /// Số, hoặc `fallback` nếu không phải số. @param fallback Giá trị dự phòng. @return Số.
  f64 number_or(f64 fallback) const { return kind == number ? num : fallback; }
  /// Số thực 32 bit, hoặc `fallback`. @param fallback Giá trị dự phòng. @return Số.
  f32 f32_or(f32 fallback) const { return kind == number ? (f32)num : fallback; }
  /// Số nguyên (làm tròn về 0), hoặc `fallback`. @param fallback Giá trị dự phòng. @return Số.
  i32 int_or(i32 fallback) const { return kind == number ? (i32)num : fallback; }
  /// Bool, hoặc `fallback` nếu không phải bool. @param fallback Giá trị dự phòng. @return Bool.
  bool bool_or(bool fallback) const { return kind == boolean ? b : fallback; }
  /// Chuỗi, hoặc `fallback` nếu không phải chuỗi. Con trỏ sống cùng giá trị này.
  /// @param fallback Giá trị dự phòng. @return Chuỗi.
  const char *string_or(const char *fallback) const {
    return kind == string ? str.c_str() : fallback;
  }
};

/// Đọc JSON từ chuỗi.
/// @param text Nội dung JSON (UTF-8, có thể có BOM).
/// @param out Nhận giá trị đọc được.
/// @param error Nhận thông báo lỗi kèm vị trí byte nếu hỏng. Có thể null.
/// @return `false` nếu JSON không hợp lệ.
bool json_parse(std::string_view text, json_value &out, std::string *error = nullptr);

/// Viết giá trị ra chuỗi JSON.
///
/// Số nguyên được viết không có phần thập phân; số thực giữ đủ chữ số để
/// đọc lại đúng giá trị. NaN và vô cực (JSON không có) được viết là `null`.
/// @param value Giá trị.
/// @param pretty Xuống dòng và thụt lề cho dễ đọc; `false` là một dòng gọn.
/// @return Chuỗi JSON.
std::string json_dump(const json_value &value, bool pretty = true);

/// Đọc và phân tích một file JSON. Đường dẫn tìm như mọi hàm nạp tài nguyên.
/// Lỗi (thiếu file, JSON hỏng) được ghi log kèm vị trí.
/// @param path Đường dẫn file.
/// @param out Nhận giá trị đọc được.
/// @return `false` nếu không đọc được.
bool json_load(const char *path, json_value &out);

/// Ghi một giá trị ra file JSON, tạo thư mục cha nếu cần. Ghi qua một file tạm
/// rồi đổi tên, nên mất điện giữa chừng không làm hỏng file cũ.
/// @param path Đường dẫn file, thường từ save_path().
/// @param value Giá trị.
/// @param pretty Xuống dòng và thụt lề.
/// @return `false` nếu không ghi được.
bool json_save(const char *path, const json_value &value, bool pretty = true);
/// @}
} // namespace njin
