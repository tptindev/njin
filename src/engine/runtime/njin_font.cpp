#include "njin_font.h"
#include "njin_log.h"
#include "njin_path.h"
#include <string>

namespace njin {
namespace {
// Codepoints baked into every loaded font. Vietnamese needs more than
// Latin-1: đ/Đ live in Latin Extended-A, ơ/ư in Extended-B, and the letters
// carrying two marks (ấ, ẫ, ộ...) in the Latin Extended Additional block.
std::vector<int> font_codepoints() {
  std::vector<int> cps;
  const auto add = [&](int lo, int hi) {
    for (int c = lo; c <= hi; c++)
      cps.push_back(c);
  };
  add(0x20, 0x7E);   // ASCII
  add(0xA0, 0xFF);   // Latin-1 Supplement
  add(0x100, 0x24F); // Latin Extended-A and -B
  add(0x300, 0x323); // combining marks, for text typed in decomposed form
  add(0x1EA0, 0x1EF9); // Vietnamese
  add(0x2010, 0x2027); // dashes, quotes, bullet, ellipsis
  add(0x20AB, 0x20AC); // đồng and euro signs
  return cps;
}

font_slot *slot_of(font_store &store, font_handle handle) {
  if (handle.id == 0 || handle.id > store.slots.size())
    return nullptr;
  font_slot &slot = store.slots[handle.id - 1];
  return slot.alive ? &slot : nullptr;
}
} // namespace

font_store::~font_store() {
  for (usize i = 0; i < slots.size(); i++)
    font_store_unload(*this, font_handle{.id = (u32)(i + 1)});
}

font_handle font_store_load(font_store &store, const char *path, i32 size) {
  if (path == nullptr) {
    NJIN_WARN("font: path is null");
    return font_handle{};
  }
  if (size <= 0) {
    NJIN_WARN("font: invalid size %d for %s", size, path);
    return font_handle{};
  }
  const std::string resolved = asset_path(path);
  if (!FileExists(resolved.c_str())) {
    NJIN_WARN("font: file not found: %s", path);
    return font_handle{};
  }
  const std::vector<int> cps = font_codepoints();
  const Font font =
      LoadFontEx(resolved.c_str(), size, cps.data(), (int)cps.size());
  // LoadFontEx falls back to the default font when the file cannot be parsed;
  // its texture id is then the default font's, which must not be unloaded.
  if (!IsFontValid(font) || font.texture.id == GetFontDefault().texture.id) {
    NJIN_WARN("font: failed to load: %s", path);
    return font_handle{};
  }
  SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
  store.slots.push_back(font_slot{.font = font, .alive = true});
  return font_handle{.id = (u32)store.slots.size()};
}

void font_store_unload(font_store &store, font_handle handle) {
  font_slot *slot = slot_of(store, handle);
  if (slot == nullptr)
    return;
  UnloadFont(slot->font);
  *slot = font_slot{};
}

Font font_store_get(const font_store &store, font_handle handle) {
  if (handle.id != 0 && handle.id <= store.slots.size()) {
    const font_slot &slot = store.slots[handle.id - 1];
    if (slot.alive)
      return slot.font;
  }
  return GetFontDefault();
}

f32 font_store_spacing(font_handle handle, f32 size) {
  return handle.id == 0 ? size / 10.0f : 0.0f;
}
} // namespace njin
