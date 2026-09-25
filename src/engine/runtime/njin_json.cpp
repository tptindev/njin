#include "njin_json.h"
#include "njin_file.h"
#include "njin_log.h"
#include "njin_path.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace njin {
namespace {
// Nesting deeper than this is rejected instead of overflowing the stack.
constexpr i32 max_nesting = 128;

struct parser {
  std::string_view text;
  usize at = 0;
  std::string error;

  bool fail(const char *what) {
    if (error.empty())
      error = std::string(what) + " at byte " + std::to_string(at);
    return false;
  }

  void skip_space() {
    while (at < text.size() && (text[at] == ' ' || text[at] == '\t' ||
                                text[at] == '\n' || text[at] == '\r'))
      at++;
  }

  bool literal(std::string_view word) {
    if (text.substr(at, word.size()) != word)
      return fail("unexpected token");
    at += word.size();
    return true;
  }

  static void put_utf8(std::string &out, u32 cp) {
    if (cp < 0x80) {
      out.push_back((char)cp);
    } else if (cp < 0x800) {
      out.push_back((char)(0xC0 | (cp >> 6)));
      out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back((char)(0xE0 | (cp >> 12)));
      out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
      out.push_back((char)(0xF0 | (cp >> 18)));
      out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back((char)(0x80 | (cp & 0x3F)));
    }
  }

  bool hex4(u32 &out) {
    if (at + 4 > text.size())
      return fail("truncated \\u escape");
    out = 0;
    for (i32 i = 0; i < 4; i++) {
      const char c = text[at++];
      out <<= 4;
      if (c >= '0' && c <= '9')
        out |= (u32)(c - '0');
      else if (c >= 'a' && c <= 'f')
        out |= (u32)(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F')
        out |= (u32)(c - 'A' + 10);
      else
        return fail("bad \\u escape");
    }
    return true;
  }

  bool string(std::string &out) {
    at++; // opening quote
    while (at < text.size()) {
      const char c = text[at++];
      if (c == '"')
        return true;
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (at >= text.size())
        break;
      const char e = text[at++];
      switch (e) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'u': {
        u32 cp = 0;
        if (!hex4(cp))
          return false;
        // A surrogate pair encodes one code point above U+FFFF.
        if (cp >= 0xD800 && cp <= 0xDBFF && text.substr(at, 2) == "\\u") {
          at += 2;
          u32 low = 0;
          if (!hex4(low))
            return false;
          cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        }
        put_utf8(out, cp);
        break;
      }
      default:
        return fail("bad escape");
      }
    }
    return fail("unterminated string");
  }

  bool number(json_value &out) {
    const usize start = at;
    while (at < text.size() &&
           (text[at] == '-' || text[at] == '+' || text[at] == '.' ||
            text[at] == 'e' || text[at] == 'E' ||
            (text[at] >= '0' && text[at] <= '9')))
      at++;
    const std::string digits(text.substr(start, at - start));
    char *end = nullptr;
    out.num = std::strtod(digits.c_str(), &end);
    if (digits.empty() || end != digits.c_str() + digits.size())
      return fail("bad number");
    out.kind = json_value::number;
    return true;
  }

  bool value(json_value &out, i32 depth) {
    if (depth > max_nesting)
      return fail("nesting too deep");
    skip_space();
    if (at >= text.size())
      return fail("unexpected end");
    const char c = text[at];
    if (c == '{') {
      out.kind = json_value::object;
      at++;
      skip_space();
      if (at < text.size() && text[at] == '}') {
        at++;
        return true;
      }
      while (true) {
        skip_space();
        if (at >= text.size() || text[at] != '"')
          return fail("expected member name");
        std::string key;
        if (!string(key))
          return false;
        skip_space();
        if (at >= text.size() || text[at] != ':')
          return fail("expected ':'");
        at++;
        out.members.emplace_back(std::move(key), json_value{});
        if (!value(out.members.back().second, depth + 1))
          return false;
        skip_space();
        if (at < text.size() && text[at] == ',') {
          at++;
          continue;
        }
        if (at < text.size() && text[at] == '}') {
          at++;
          return true;
        }
        return fail("expected ',' or '}'");
      }
    }
    if (c == '[') {
      out.kind = json_value::array;
      at++;
      skip_space();
      if (at < text.size() && text[at] == ']') {
        at++;
        return true;
      }
      while (true) {
        out.items.emplace_back();
        if (!value(out.items.back(), depth + 1))
          return false;
        skip_space();
        if (at < text.size() && text[at] == ',') {
          at++;
          continue;
        }
        if (at < text.size() && text[at] == ']') {
          at++;
          return true;
        }
        return fail("expected ',' or ']'");
      }
    }
    if (c == '"') {
      out.kind = json_value::string;
      return string(out.str);
    }
    if (c == 't') {
      out.kind = json_value::boolean;
      out.b = true;
      return literal("true");
    }
    if (c == 'f') {
      out.kind = json_value::boolean;
      return literal("false");
    }
    if (c == 'n') {
      out.kind = json_value::null;
      return literal("null");
    }
    if (c != '-' && (c < '0' || c > '9'))
      return fail("unexpected character");
    return number(out);
  }
};
} // namespace

namespace {
const json_value &missing() {
  static const json_value value{};
  return value;
}

void dump_string(std::string &out, const std::string &s) {
  out.push_back('"');
  for (const char ch : s) {
    const unsigned char c = (unsigned char)ch;
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\b': out += "\\b"; break;
    case '\f': out += "\\f"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (c < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof buf, "\\u%04x", c);
        out += buf;
      } else {
        out.push_back(ch); // UTF-8 passes through unchanged
      }
    }
  }
  out.push_back('"');
}

void dump_number(std::string &out, f64 v) {
  if (!std::isfinite(v)) {
    out += "null";
    return;
  }
  char buf[32];
  // Whole numbers in the exactly representable range print without a
  // fraction, so counters and ids read back as the integers they were.
  if (v == std::floor(v) && std::abs(v) < 9007199254740992.0) {
    std::snprintf(buf, sizeof buf, "%.0f", v);
  } else {
    // The shortest form that reads back as the same double: 0.1, not
    // 0.10000000000000001.
    for (i32 digits = 15; digits <= 17; digits++) {
      std::snprintf(buf, sizeof buf, "%.*g", digits, v);
      if (std::strtod(buf, nullptr) == v)
        break;
    }
  }
  out += buf;
}

void dump(std::string &out, const json_value &v, bool pretty, i32 depth) {
  const auto newline = [&](i32 d) {
    if (!pretty)
      return;
    out.push_back('\n');
    out.append((usize)d * 2, ' ');
  };
  switch (v.kind) {
  case json_value::null: out += "null"; return;
  case json_value::boolean: out += v.b ? "true" : "false"; return;
  case json_value::number: dump_number(out, v.num); return;
  case json_value::string: dump_string(out, v.str); return;
  case json_value::array:
    if (v.items.empty()) {
      out += "[]";
      return;
    }
    out.push_back('[');
    for (usize i = 0; i < v.items.size(); i++) {
      if (i > 0)
        out.push_back(',');
      newline(depth + 1);
      dump(out, v.items[i], pretty, depth + 1);
    }
    newline(depth);
    out.push_back(']');
    return;
  case json_value::object:
    if (v.members.empty()) {
      out += "{}";
      return;
    }
    out.push_back('{');
    for (usize i = 0; i < v.members.size(); i++) {
      if (i > 0)
        out.push_back(',');
      newline(depth + 1);
      dump_string(out, v.members[i].first);
      out += pretty ? ": " : ":";
      dump(out, v.members[i].second, pretty, depth + 1);
    }
    newline(depth);
    out.push_back('}');
    return;
  }
}
} // namespace

const json_value &json_value::operator[](std::string_view key) const {
  if (kind != object)
    return missing();
  for (const auto &[name, value] : members) {
    if (name == key)
      return value;
  }
  return missing();
}

const json_value &json_value::operator[](usize index) const {
  return kind == array && index < items.size() ? items[index] : missing();
}

json_value *json_value::find(std::string_view key) {
  if (kind != object)
    return nullptr;
  for (auto &[name, value] : members) {
    if (name == key)
      return &value;
  }
  return nullptr;
}

bool json_value::has(std::string_view key) const {
  return const_cast<json_value *>(this)->find(key) != nullptr;
}

usize json_value::size() const {
  return kind == array ? items.size() : kind == object ? members.size() : 0;
}

json_value &json_value::set(std::string_view key, json_value value) {
  if (kind == null)
    kind = object;
  if (kind != object) {
    NJIN_WARN("json: set('%.*s') on a value that is not an object", (int)key.size(), key.data());
    return *this;
  }
  if (json_value *existing = find(key))
    *existing = std::move(value);
  else
    members.emplace_back(std::string(key), std::move(value));
  return *this;
}

json_value &json_value::push(json_value value) {
  if (kind == null)
    kind = array;
  if (kind != array) {
    NJIN_WARN("json: push on a value that is not an array");
    return *this;
  }
  items.push_back(std::move(value));
  return *this;
}

bool json_parse(std::string_view text, json_value &out, std::string *error) {
  parser p;
  p.text = text;
  // A UTF-8 byte order mark is legal in files even if not in JSON proper.
  if (text.substr(0, 3) == "\xEF\xBB\xBF")
    p.at = 3;
  out = json_value{};
  bool ok = p.value(out, 0);
  if (ok) {
    p.skip_space();
    if (p.at != text.size())
      ok = p.fail("trailing characters");
  }
  if (!ok && error != nullptr)
    *error = p.error;
  return ok;
}

std::string json_dump(const json_value &value, bool pretty) {
  std::string out;
  dump(out, value, pretty, 0);
  if (pretty)
    out.push_back('\n');
  return out;
}

bool json_load(const char *path, json_value &out) {
  if (path == nullptr) {
    NJIN_WARN("json_load: path is null");
    return false;
  }
  std::string text;
  if (!file_read(asset_path(path).c_str(), text)) {
    NJIN_WARN("json_load: cannot read %s", path);
    return false;
  }
  std::string error;
  if (!json_parse(text, out, &error)) {
    NJIN_WARN("json_load: %s: %s", path, error.c_str());
    return false;
  }
  return true;
}

bool json_save(const char *path, const json_value &value, bool pretty) {
  if (path == nullptr) {
    NJIN_WARN("json_save: path is null");
    return false;
  }
  return file_write(path, json_dump(value, pretty));
}
} // namespace njin
