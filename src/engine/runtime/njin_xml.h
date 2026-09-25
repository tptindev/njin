#pragma once

#include "_types.h"
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace njin {
// A small read-only XML document, enough for Tiled's .tmx and .tsx files:
// elements, attributes, text. Comments, the <?xml?> declaration, doctypes
// and processing instructions are skipped; CDATA is kept as text. No
// namespaces or DTD entities (only the five predefined ones and &#...;).
struct xml_node {
  std::string name;
  std::vector<std::pair<std::string, std::string>> attrs;
  std::vector<xml_node> children;
  std::string text; // concatenated character data directly inside this element

  // Attribute value, or `fallback` when missing.
  const char *attr(std::string_view key, const char *fallback = nullptr) const;
  f64 attr_number(std::string_view key, f64 fallback) const;
  // First child element with this name, or nullptr.
  const xml_node *child(std::string_view child_name) const;
};

// Parses `text` into its root element. On failure returns false and writes a
// message with the byte offset into `error`.
bool xml_parse(std::string_view text, xml_node &root, std::string &error);
} // namespace njin
