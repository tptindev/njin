#pragma once

#include "_types.h"
#include <map>
#include <raylib.h>
#include <vector>

namespace njin {
// The default font, JetBrains Mono, compiled in (see cmake/embed_file.cmake).
extern const unsigned char font_default_ttf[];
extern const unsigned long long font_default_ttf_size;

// One typeface: its file, plus one glyph atlas per pixel size it has been drawn
// at. Text is a few dozen pixels tall at most, and a single atlas scaled to
// that loses every stroke thinner than a pixel, however it is filtered; a glyph
// baked at the size it is drawn at lands on whole texels instead. Atlases are
// baked on first use, so this needs the GL context.
struct font_slot {
  std::vector<unsigned char> owned; // the file, for a font loaded from disk
  const unsigned char *bytes = nullptr;
  i32 length = 0;
  std::map<i32, Font> atlases;
  bool alive = false;

  void unload_atlases();
};

// Same rules as the other stores: handle id 0 means "the default font", id N
// maps to slots[N - 1], and slots are never reused. A handle that is invalid or
// unloaded also draws with the default font.
struct font_store {
  font_slot fallback;
  std::vector<font_slot> slots;

  // Frees every atlas, so it must run while the GL context is still alive
  // (before CloseWindow).
  font_store();
  ~font_store();
  font_store(const font_store &) = delete;
  font_store &operator=(const font_store &) = delete;
};

// Whole pixels: what makes the atlas match the draw. A request for 11.4 is
// drawn at 11, because a truer height is not worth the stroke it costs.
i32 font_px(f32 size);

// Reads the file and bakes `size` (or 16 when `size` <= 0) once, so a font
// that cannot be parsed fails here and not at the first draw.
font_handle font_store_load(font_store &store, const char *path, i32 size);
void font_store_unload(font_store &store, font_handle handle);

// The atlas to draw `handle` with at `px` pixels (see font_px). Never null once
// the default font itself is fine. Past a cap on atlases per font (a size
// that is animated would bake one per frame) the nearest baked size is used.
const Font *font_store_atlas(font_store &store, font_handle handle, i32 px);
} // namespace njin
