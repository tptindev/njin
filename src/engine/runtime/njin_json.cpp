#include "njin_json.h"
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
    return number(out);
  }
};
} // namespace

const json_value &json_value::operator[](std::string_view key) const {
  static const json_value missing{};
  if (kind != object)
    return missing;
  for (const auto &[name, value] : members) {
    if (name == key)
      return value;
  }
  return missing;
}

bool json_parse(std::string_view text, json_value &out, std::string &error) {
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
  if (!ok)
    error = p.error;
  return ok;
}
} // namespace njin
