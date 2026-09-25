#pragma once

#include "_types.h"
#include <raylib.h>
#include <vector>

namespace njin {
// Same rules as the other stores: handle id 0 means "the default font", id N
// maps to slots[N - 1], and slots are never reused.
struct font_slot {
  Font font{};
  bool alive = false;
};

// Owns the glyph atlases of every live slot. The destructor frees them, so it
// must run while the GL context is still alive (before CloseWindow).
struct font_store {
  std::vector<font_slot> slots;

  font_store() = default;
  ~font_store();
  font_store(const font_store &) = delete;
  font_store &operator=(const font_store &) = delete;
};

// Loads ASCII, Latin-1, Latin Extended-A/B and the Vietnamese block.
font_handle font_store_load(font_store &store, const char *path, i32 size);
void font_store_unload(font_store &store, font_handle handle);

// The font to draw with: the slot's font, or raylib's default font for id 0
// and for handles that are invalid or unloaded.
Font font_store_get(const font_store &store, font_handle handle);
// Spacing between glyphs at `size`: raylib's default font is a bitmap font
// drawn with a gap of size / 10, the way DrawText does it; loaded fonts carry
// their own advance and need none.
f32 font_store_spacing(font_handle handle, f32 size);
} // namespace njin
