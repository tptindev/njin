#pragma once

#include "_types.h"
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace njin {
// A small read-only JSON document, enough for tool exports (Aseprite sheet
// data). Numbers are doubles; object members keep their file order, which
// Aseprite's "hash" frame layout relies on.
struct json_value {
  enum kind_t { null, boolean, number, string, array, object };
  kind_t kind = null;
  bool b = false;
  f64 num = 0.0;
  std::string str;
  std::vector<json_value> items;                          // array
  std::vector<std::pair<std::string, json_value>> members; // object

  // Member lookup; returns a shared null value when missing or not an object.
  const json_value &operator[](std::string_view key) const;
  bool is(kind_t k) const { return kind == k; }
  f64 number_or(f64 fallback) const { return kind == number ? num : fallback; }
  const char *string_or(const char *fallback) const {
    return kind == string ? str.c_str() : fallback;
  }
};

// Parses `text`. On failure returns false and writes a message with the byte
// offset into `error`.
bool json_parse(std::string_view text, json_value &out, std::string &error);
} // namespace njin
