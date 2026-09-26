#include "njin_i18n.h"
#include "njin_ctx_impl.h"
#include "njin_json.h"
#include "njin_log.h"
#include "njin_path.h"
#include <algorithm>

namespace njin {
namespace {
i18n_table *find_table(i18n_store &store, const std::string &lang) {
  for (i18n_table &t : store.tables)
    if (t.lang == lang)
      return &t;
  return nullptr;
}

const i18n_table *find_table(const i18n_store &store, const std::string &lang) {
  return find_table(const_cast<i18n_store &>(store), lang);
}

void flatten(const json_value &v, const std::string &prefix, i18n_table &out) {
  for (const auto &[key, value] : v.members) {
    const std::string full = prefix.empty() ? key : prefix + "." + key;
    if (value.is(json_value::object))
      flatten(value, full, out);
    else if (value.is(json_value::string))
      out.strings[full] = value.str;
    else if (value.is(json_value::number) || value.is(json_value::boolean))
      out.strings[full] = json_dump(value, false);
  }
}

const std::string *lookup(const i18n_store &store, const char *key) {
  for (const std::string *lang : {&store.current, &store.fallback}) {
    if (const i18n_table *t = find_table(store, *lang)) {
      const auto it = t->strings.find(key);
      if (it != t->strings.end())
        return &it->second;
    }
  }
  return nullptr;
}
} // namespace

bool i18n_load(njin_ctx &ctx, const char *lang, const char *path) {
  if (lang == nullptr || path == nullptr)
    return false;
  json_value root;
  if (!json_load(path, root) || !root.is(json_value::object)) {
    NJIN_WARN("i18n: cannot read %s (a JSON object is expected)", path);
    return false;
  }
  i18n_store &store = ctx.i18n;
  i18n_table *t = find_table(store, lang);
  if (t == nullptr) {
    store.tables.push_back(i18n_table{.lang = lang, .strings = {}});
    t = &store.tables.back();
  }
  flatten(root, "", *t);
  if (store.current.empty())
    store.current = lang;
  if (store.fallback.empty())
    store.fallback = lang;
  return true;
}

void i18n_set_language(njin_ctx &ctx, const char *lang) {
  if (lang == nullptr || find_table(ctx.i18n, lang) == nullptr) {
    NJIN_WARN("i18n_set_language: language '%s' is not loaded", lang != nullptr ? lang : "");
    return;
  }
  ctx.i18n.current = lang;
}

const char *i18n_language(const njin_ctx &ctx) { return ctx.i18n.current.c_str(); }

void i18n_set_fallback(njin_ctx &ctx, const char *lang) {
  if (lang != nullptr)
    ctx.i18n.fallback = lang;
}

std::vector<std::string> i18n_languages(const njin_ctx &ctx) {
  std::vector<std::string> out;
  for (const i18n_table &t : ctx.i18n.tables)
    out.push_back(t.lang);
  return out;
}

const char *i18n_language_name(const njin_ctx &ctx, const char *lang) {
  if (lang == nullptr)
    return "";
  if (const i18n_table *t = find_table(ctx.i18n, lang)) {
    const auto it = t->strings.find("_name");
    if (it != t->strings.end())
      return it->second.c_str();
    return t->lang.c_str();
  }
  return lang;
}

const char *tr(const njin_ctx &ctx, const char *key) {
  if (key == nullptr)
    return "";
  if (const std::string *s = lookup(ctx.i18n, key))
    return s->c_str();
  if (!ctx.i18n.tables.empty() && ctx.i18n.missing.insert(key).second)
    NJIN_WARN("i18n: no string for '%s' in '%s'", key, ctx.i18n.current.c_str());
  return key;
}

std::string trf(const njin_ctx &ctx, const char *key, std::initializer_list<std::string_view> args) {
  const std::string text = tr(ctx, key);
  std::string out;
  out.reserve(text.size());
  for (usize i = 0; i < text.size(); i++) {
    if (text[i] == '{') {
      const usize close = text.find('}', i);
      if (close != std::string::npos && close > i + 1) {
        const std::string num = text.substr(i + 1, close - i - 1);
        if (std::all_of(num.begin(), num.end(), [](char c) { return c >= '0' && c <= '9'; })) {
          const usize n = (usize)std::stoul(num);
          if (n < args.size()) {
            out += *(args.begin() + n);
            i = close;
            continue;
          }
        }
      }
    }
    out.push_back(text[i]);
  }
  return out;
}

bool i18n_has(const njin_ctx &ctx, const char *key) {
  return key != nullptr && lookup(ctx.i18n, key) != nullptr;
}
} // namespace njin
