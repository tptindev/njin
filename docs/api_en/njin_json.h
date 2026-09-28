#pragma once
#include "_types.h"
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace njin {
/// @addtogroup grp_json
/// @{

/// A JSON value: null, bool, number, string, array or object. Used for save
/// games, config files, and properties read from Tiled and LDtk.
///
/// Reading is safe against missing data: `doc["player"]["hp"]` on a key that
/// does not exist returns a null value rather than an error, and `number_or()`
/// returns a fallback value. For writing, build with set() and push():
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
/// Object members keep the exact order of the file (and the order of set()), so
/// writing back a file you just read does not shuffle it.
struct json_value {
  /// Kind of value.
  enum kind_t {
    null,    ///< No value. Also the result of reading a nonexistent key.
    boolean, ///< `true` or `false`, in `b`.
    number,  ///< A number, in `num`.
    string,  ///< A UTF-8 string, in `str`.
    array,   ///< An array, in `items`.
    object,  ///< An object, in `members`.
  };
  kind_t kind = null; ///< Kind of this value.
  bool b = false;     ///< Value when `kind == boolean`.
  f64 num = 0.0;      ///< Value when `kind == number`.
  std::string str;    ///< Value when `kind == string`.
  std::vector<json_value> items; ///< Elements when `kind == array`.
  std::vector<std::pair<std::string, json_value>> members; ///< Members when `kind == object`, in order.

  json_value() = default;
  /// A bool value. @param v The value.
  json_value(bool v) : kind(boolean), b(v) {}
  /// A number value. @param v The value.
  json_value(i32 v) : kind(number), num(v) {}
  /// A number value. @param v The value.
  json_value(i64 v) : kind(number), num((f64)v) {}
  /// A number value. @param v The value.
  json_value(u32 v) : kind(number), num(v) {}
  /// A number value. @param v The value.
  json_value(f32 v) : kind(number), num(v) {}
  /// A number value. @param v The value.
  json_value(f64 v) : kind(number), num(v) {}
  /// A string. A null pointer becomes a null value. @param v The value.
  json_value(const char *v) : kind(v != nullptr ? string : null), str(v != nullptr ? v : "") {}
  /// A string. @param v The value.
  json_value(std::string v) : kind(string), str(std::move(v)) {}

  /// An empty object. @return An object with no members.
  static json_value make_object() {
    json_value v;
    v.kind = kind_t::object;
    return v;
  }
  /// An empty array. @return An array with no elements.
  static json_value make_array() {
    json_value v;
    v.kind = kind_t::array;
    return v;
  }

  /// Whether the kind is exactly `k`. @param k The kind. @return `true` if it is.
  bool is(kind_t k) const { return kind == k; }

  /// The member named `key`. Reading a missing key, or reading on a value that
  /// is not an object, returns a shared null value (never an error).
  /// @param key Member name.
  /// @return The member, or a null value.
  const json_value &operator[](std::string_view key) const;

  /// Element number `index` of the array, or a null value if out of range.
  /// @param index Position, from 0.
  /// @return The element, or a null value.
  const json_value &operator[](usize index) const;

  /// The member named `key`, for modification, or null if there is none.
  /// @param key Member name.
  /// @return Pointer to the member, or null.
  json_value *find(std::string_view key);

  /// Whether a member named `key` exists. @param key The name. @return `true` if it does.
  bool has(std::string_view key) const;

  /// Number of elements (array) or number of members (object), 0 for other kinds.
  /// @return The size.
  usize size() const;

  /// Sets member `key` (replaces it if present, appends at the end if not). A
  /// null value (not yet an object) automatically becomes an object.
  /// @param key Member name.
  /// @param value The value.
  /// @return This same object, for chaining.
  json_value &set(std::string_view key, json_value value);

  /// Appends an element to the end of the array. A null value automatically becomes an array.
  /// @param value The value.
  /// @return This same array, for chaining.
  json_value &push(json_value value);

  /// The number, or `fallback` if it is not a number. @param fallback Fallback value. @return The number.
  f64 number_or(f64 fallback) const { return kind == number ? num : fallback; }
  /// The 32-bit float, or `fallback`. @param fallback Fallback value. @return The number.
  f32 f32_or(f32 fallback) const { return kind == number ? (f32)num : fallback; }
  /// The integer (truncated toward 0), or `fallback`. @param fallback Fallback value. @return The number.
  i32 int_or(i32 fallback) const { return kind == number ? (i32)num : fallback; }
  /// The bool, or `fallback` if it is not a bool. @param fallback Fallback value. @return The bool.
  bool bool_or(bool fallback) const { return kind == boolean ? b : fallback; }
  /// The string, or `fallback` if it is not a string. The pointer lives as long as this value.
  /// @param fallback Fallback value. @return The string.
  const char *string_or(const char *fallback) const {
    return kind == string ? str.c_str() : fallback;
  }
};

/// Parses JSON from a string.
/// @param text JSON content (UTF-8, may have a BOM).
/// @param out Receives the parsed value.
/// @param error Receives an error message with the byte position if it is malformed. May be null.
/// @return `false` if the JSON is invalid.
bool json_parse(std::string_view text, json_value &out, std::string *error = nullptr);

/// Writes a value out as a JSON string.
///
/// Integers are written without a decimal part; floating-point numbers keep
/// enough digits to be read back to the exact value. NaN and infinity (which
/// JSON does not have) are written as `null`.
/// @param value The value.
/// @param pretty Adds line breaks and indentation for readability; `false` is a compact single line.
/// @return The JSON string.
std::string json_dump(const json_value &value, bool pretty = true);

/// Reads and parses a JSON file. The path is looked up like every resource-loading function.
/// Errors (missing file, malformed JSON) are logged with the position.
/// @param path File path.
/// @param out Receives the parsed value.
/// @return `false` if it could not be read.
bool json_load(const char *path, json_value &out);

/// Writes a value to a JSON file, creating the parent directory if needed. Writes
/// through a temporary file and then renames it, so a power loss midway does not
/// corrupt the old file.
/// @param path File path, usually from save_path().
/// @param value The value.
/// @param pretty Adds line breaks and indentation.
/// @return `false` if it could not be written.
bool json_save(const char *path, const json_value &value, bool pretty = true);
/// @}
} // namespace njin
