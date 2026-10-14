#include "njin_font.h"
#include "njin_log.h"
#include "njin_path.h"
#include <algorithm>
#include <cmath>
#include <string>

// stb_truetype for the fonts' own metrics (round_advances); raylib's copy is
// static to it, so this one is too.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "external/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace njin {
namespace {
// Below this a glyph is a smudge, and above it text does not go.
constexpr i32 min_px = 6;
constexpr i32 max_px = 256;
// An atlas is a few hundred glyphs: a small texture. Past this many for one
// font the nearest size already baked is used instead of baking another.
constexpr usize max_atlases = 24;
constexpr i32 probe_px = 16;

// Codepoints baked into every atlas. Vietnamese needs more than Latin-1:
// đ/Đ live in Latin Extended-A, ơ/ư in Extended-B, and the letters carrying two
// marks (ấ, ẫ, ộ...) in the Latin Extended Additional block. A glyph the font
// lacks is skipped by raylib, not faked.
const std::vector<int> &font_codepoints() {
  static const std::vector<int> cps = [] {
    std::vector<int> out;
    const auto add = [&](int lo, int hi) {
      for (int c = lo; c <= hi; c++)
        out.push_back(c);
    };
    add(0x20, 0x7E);     // ASCII
    add(0xA0, 0xFF);     // Latin-1 Supplement
    add(0x100, 0x24F);   // Latin Extended-A and -B
    add(0x300, 0x323);   // combining marks, for text typed in decomposed form
    add(0x1EA0, 0x1EF9); // Vietnamese
    add(0x2010, 0x2027); // dashes, quotes, bullet, ellipsis
    add(0x20AB, 0x20AC); // đồng and euro signs
    add(0x2190, 0x2195); // arrows: left, up, right, down, both ways
    add(0x2212, 0x2212); // the minus sign, as a "−" zoom button writes it
    return out;
  }();
  return cps;
}

font_slot *slot_of(font_store &store, font_handle handle) {
  if (handle.id == 0 || handle.id > store.slots.size())
    return nullptr;
  font_slot &slot = store.slots[handle.id - 1];
  return slot.alive ? &slot : nullptr;
}

// raylib cuts each glyph's advance down to whole pixels (rtext.c,
// LoadFontData): half a pixel lost a letter on average, so text came out
// cramped, and a narrow space (Be Vietnam Pro's, a fifth of an em) all but
// vanished at the small sizes text is read at. The advances are worked out
// again from the font's own metrics, rounded. A glyph raylib gave none (one
// the font lacks) keeps none.
void round_advances(const font_slot &slot, Font &font, i32 px) {
  stbtt_fontinfo info;
  if (stbtt_InitFont(&info, slot.bytes, stbtt_GetFontOffsetForIndex(slot.bytes, 0)) == 0)
    return;
  const float scale = stbtt_ScaleForPixelHeight(&info, (float)px);
  for (int i = 0; i < font.glyphCount; ++i) {
    if (font.glyphs[i].advanceX <= 0)
      continue;
    int advance = 0;
    stbtt_GetCodepointHMetrics(&info, font.glyphs[i].value, &advance, nullptr);
    if (advance > 0)
      font.glyphs[i].advanceX = (int)std::lround((float)advance * scale);
  }
}

// Bakes one atlas; null when the bytes are not a font raylib can read.
const Font *bake(font_slot &slot, i32 px) {
  const std::vector<int> &cps = font_codepoints();
  Font font{};
  if (slot.pixel) {
    // FONT_BITMAP rasterizes without anti-aliasing: every texel of a glyph is
    // fully on or off, which is the pixel-font look at the size it was made for.
    int count = 0;
    GlyphInfo *glyphs = LoadFontData(slot.bytes, slot.length, px, cps.data(), (int)cps.size(),
                                     FONT_BITMAP, &count);
    if (glyphs == nullptr || count <= 0)
      return nullptr;
    constexpr int padding = 4;
    Rectangle *recs = nullptr;
    const Image atlas = GenImageFontAtlas(glyphs, &recs, count, px, padding, 0);
    font.baseSize = px;
    font.glyphCount = count;
    font.glyphPadding = padding;
    font.glyphs = glyphs;
    font.recs = recs;
    font.texture = LoadTextureFromImage(atlas);
    UnloadImage(atlas);
  } else {
    font = LoadFontFromMemory(".ttf", slot.bytes, slot.length, px,
                              const_cast<int *>(cps.data()), (int)cps.size());
  }
  // raylib hands back its built-in font when it cannot read the bytes, and
  // that one is its own: unloading it would take the default font down.
  if (!IsFontValid(font) || font.glyphCount <= 0 ||
      font.texture.id == GetFontDefault().texture.id)
    return nullptr;
  round_advances(slot, font, px);
  // Drawn at the size it was baked at, so filtering only smooths the fraction
  // of a pixel a position may still carry. The pixel style has none to smooth.
  SetTextureFilter(font.texture, slot.pixel ? TEXTURE_FILTER_POINT : TEXTURE_FILTER_BILINEAR);
  return &slot.atlases.emplace(px, font).first->second;
}

const Font *atlas_of(font_slot &slot, i32 px) {
  const auto found = slot.atlases.find(px);
  if (found != slot.atlases.end())
    return &found->second;
  if (slot.atlases.size() >= max_atlases) {
    auto nearest = slot.atlases.begin();
    for (auto it = slot.atlases.begin(); it != slot.atlases.end(); ++it)
      if (std::abs(it->first - px) < std::abs(nearest->first - px))
        nearest = it;
    return &nearest->second;
  }
  return bake(slot, px);
}
} // namespace

void font_slot::unload_atlases() {
  for (auto &[px, font] : atlases)
    UnloadFont(font);
  atlases.clear();
}

font_store::font_store() {
  fallback.bytes = font_default_ttf;
  fallback.length = (i32)font_default_ttf_size;
  fallback.alive = true;
}

font_store::~font_store() {
  fallback.unload_atlases();
  for (font_slot &slot : slots)
    slot.unload_atlases();
}

i32 font_px(f32 size) { return std::clamp((i32)std::lround(size), min_px, max_px); }

font_handle font_store_load(font_store &store, const char *path, i32 size, bool pixel) {
  if (path == nullptr) {
    NJIN_WARN("font: path is null");
    return font_handle{};
  }
  const std::string resolved = asset_path(path);
  if (!FileExists(resolved.c_str())) {
    NJIN_WARN("font: file not found: %s", path);
    return font_handle{};
  }
  int length = 0;
  unsigned char *data = LoadFileData(resolved.c_str(), &length);
  if (data == nullptr || length <= 0) {
    NJIN_WARN("font: cannot read: %s", path);
    return font_handle{};
  }
  font_slot slot;
  slot.owned.assign(data, data + length);
  UnloadFileData(data);
  slot.bytes = slot.owned.data();
  slot.length = length;
  slot.alive = true;
  slot.pixel = pixel;
  // The vector's buffer does not move when the slot does, so `bytes` stays.
  store.slots.push_back(std::move(slot));
  font_slot &kept = store.slots.back();
  if (atlas_of(kept, size > 0 ? font_px((f32)size) : probe_px) == nullptr) {
    NJIN_WARN("font: failed to load: %s", path);
    store.slots.pop_back();
    return font_handle{};
  }
  return font_handle{.id = (u32)store.slots.size()};
}

void font_store_unload(font_store &store, font_handle handle) {
  font_slot *slot = slot_of(store, handle);
  if (slot == nullptr)
    return;
  slot->unload_atlases();
  *slot = font_slot{};
}

void font_store_set_pixel(font_store &store, font_handle handle, bool pixel) {
  font_slot *slot = handle.id == 0 ? &store.fallback : slot_of(store, handle);
  if (slot == nullptr || slot->pixel == pixel)
    return;
  slot->pixel = pixel;
  slot->unload_atlases();
}

bool font_store_is_pixel(const font_store &store, font_handle handle) {
  if (handle.id != 0 && handle.id <= store.slots.size() && store.slots[handle.id - 1].alive)
    return store.slots[handle.id - 1].pixel;
  if (const font_handle c = store.chosen; c.id != 0 && c.id <= store.slots.size() && store.slots[c.id - 1].alive)
    return store.slots[c.id - 1].pixel;
  return store.fallback.pixel;
}

void font_store_set_default(font_store &store, font_handle handle) { store.chosen = handle; }

const Font *font_store_atlas(font_store &store, font_handle handle, i32 px) {
  font_slot *slot = slot_of(store, handle);
  if (slot == nullptr)
    slot = slot_of(store, store.chosen); // the game's default, if it chose one
  if (slot == nullptr)
    slot = &store.fallback;
  const Font *font = atlas_of(*slot, px);
  // A slot whose bytes stopped parsing cannot happen after load succeeded, but
  // the default font is always there to fall back on.
  if (font == nullptr && slot != &store.fallback)
    font = atlas_of(store.fallback, px);
  return font;
}
} // namespace njin
