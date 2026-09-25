#include "njin_xml.h"
#include <cstdlib>
#include <cstring>

namespace njin {
namespace {
// Nesting deeper than this is rejected instead of overflowing the stack.
constexpr i32 max_nesting = 128;

void put_utf8(std::string &out, u32 cp) {
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

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

bool is_name_char(char c) {
  return c != '\0' && !is_space(c) && c != '=' && c != '>' && c != '/' && c != '<' &&
         c != '"' && c != '\'';
}

struct parser {
  std::string_view text;
  usize at = 0;
  std::string error;

  bool fail(const char *what) {
    if (error.empty())
      error = std::string(what) + " at byte " + std::to_string(at);
    return false;
  }

  bool starts(std::string_view s) const { return text.substr(at, s.size()) == s; }

  void skip_space() {
    while (at < text.size() && is_space(text[at]))
      at++;
  }

  // Skips past `end`, or to the end of the text.
  bool skip_past(std::string_view end) {
    const usize found = text.find(end, at);
    if (found == std::string_view::npos)
      return fail("unterminated construct");
    at = found + end.size();
    return true;
  }

  // Skips comments, declarations and processing instructions.
  bool skip_misc() {
    while (true) {
      skip_space();
      if (starts("<!--")) {
        if (!skip_past("-->"))
          return false;
      } else if (starts("<?")) {
        if (!skip_past("?>"))
          return false;
      } else if (starts("<!")) {
        if (!skip_past(">"))
          return false;
      } else {
        return true;
      }
    }
  }

  // Appends `raw` to `out` with entity and character references decoded.
  void decode(std::string_view raw, std::string &out) {
    for (usize i = 0; i < raw.size(); i++) {
      if (raw[i] != '&') {
        out.push_back(raw[i]);
        continue;
      }
      const usize semi = raw.find(';', i);
      if (semi == std::string_view::npos) {
        out.push_back('&');
        continue;
      }
      const std::string_view ent = raw.substr(i + 1, semi - i - 1);
      if (ent == "lt")
        out.push_back('<');
      else if (ent == "gt")
        out.push_back('>');
      else if (ent == "amp")
        out.push_back('&');
      else if (ent == "quot")
        out.push_back('"');
      else if (ent == "apos")
        out.push_back('\'');
      else if (!ent.empty() && ent[0] == '#') {
        const std::string digits(ent.substr(1));
        const bool hex = !digits.empty() && (digits[0] == 'x' || digits[0] == 'X');
        put_utf8(out, (u32)std::strtoul(digits.c_str() + (hex ? 1 : 0), nullptr, hex ? 16 : 10));
      } else {
        out.append(raw.substr(i, semi - i + 1)); // unknown: keep as written
      }
      i = semi;
    }
  }

  bool name(std::string &out) {
    const usize start = at;
    while (at < text.size() && is_name_char(text[at]))
      at++;
    if (at == start)
      return fail("expected a name");
    out.assign(text.substr(start, at - start));
    return true;
  }

  bool element(xml_node &node, i32 depth) {
    if (depth > max_nesting)
      return fail("nesting too deep");
    if (at >= text.size() || text[at] != '<')
      return fail("expected '<'");
    at++;
    if (!name(node.name))
      return false;
    // Attributes.
    while (true) {
      skip_space();
      if (at >= text.size())
        return fail("unexpected end in tag");
      if (starts("/>")) {
        at += 2;
        return true;
      }
      if (text[at] == '>') {
        at++;
        break;
      }
      std::string key;
      if (!name(key))
        return false;
      skip_space();
      if (at >= text.size() || text[at] != '=')
        return fail("expected '='");
      at++;
      skip_space();
      if (at >= text.size() || (text[at] != '"' && text[at] != '\''))
        return fail("expected a quoted value");
      const char quote = text[at++];
      const usize end = text.find(quote, at);
      if (end == std::string_view::npos)
        return fail("unterminated attribute value");
      std::string value;
      decode(text.substr(at, end - at), value);
      node.attrs.emplace_back(std::move(key), std::move(value));
      at = end + 1;
    }
    // Content.
    while (true) {
      if (at >= text.size())
        return fail("missing closing tag");
      if (starts("</")) {
        at += 2;
        std::string closing;
        if (!name(closing))
          return false;
        if (closing != node.name)
          return fail("mismatched closing tag");
        skip_space();
        if (at >= text.size() || text[at] != '>')
          return fail("expected '>'");
        at++;
        return true;
      }
      if (starts("<!--")) {
        if (!skip_past("-->"))
          return false;
        continue;
      }
      if (starts("<![CDATA[")) {
        at += 9;
        const usize end = text.find("]]>", at);
        if (end == std::string_view::npos)
          return fail("unterminated CDATA");
        node.text.append(text.substr(at, end - at));
        at = end + 3;
        continue;
      }
      if (starts("<?")) {
        if (!skip_past("?>"))
          return false;
        continue;
      }
      if (text[at] == '<') {
        node.children.emplace_back();
        if (!element(node.children.back(), depth + 1))
          return false;
        continue;
      }
      const usize end = text.find('<', at);
      const usize stop = end == std::string_view::npos ? text.size() : end;
      decode(text.substr(at, stop - at), node.text);
      at = stop;
    }
  }
};
} // namespace

const char *xml_node::attr(std::string_view key, const char *fallback) const {
  for (const auto &[k, v] : attrs) {
    if (k == key)
      return v.c_str();
  }
  return fallback;
}

f64 xml_node::attr_number(std::string_view key, f64 fallback) const {
  const char *v = attr(key);
  if (v == nullptr || *v == '\0')
    return fallback;
  char *end = nullptr;
  const f64 n = std::strtod(v, &end);
  return end != v ? n : fallback;
}

const xml_node *xml_node::child(std::string_view child_name) const {
  for (const xml_node &c : children) {
    if (c.name == child_name)
      return &c;
  }
  return nullptr;
}

bool xml_parse(std::string_view text, xml_node &root, std::string &error) {
  parser p;
  p.text = text;
  if (text.substr(0, 3) == "\xEF\xBB\xBF")
    p.at = 3;
  root = xml_node{};
  bool ok = p.skip_misc() && p.element(root, 0);
  if (!ok)
    error = p.error;
  return ok;
}
} // namespace njin
