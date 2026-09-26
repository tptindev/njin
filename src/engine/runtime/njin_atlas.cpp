#include "njin_atlas.h"
#include "njin_ctx_impl.h"
#include "njin_log.h"
#include "njin_path.h"
#include "njin_texture.h"
#include <algorithm>
#include <raylib.h>
#include <string>
#include <vector>

namespace njin {
namespace {
atlas_slot *atlas_slot_of(texture_store &store, atlas_handle handle) {
  if (handle.id == 0 || handle.id > store.atlases.size())
    return nullptr;
  atlas_slot &slot = store.atlases[handle.id - 1];
  return slot.alive ? &slot : nullptr;
}

// Finds room for a `w` x `h` block in `page` and takes it. Images go in rows
// (shelves): the block joins the row that wastes the least height, or opens a
// new row below the last one.
bool place(atlas_page &page, i32 size, i32 w, i32 h, i32 &x, i32 &y) {
  atlas_page::shelf *best = nullptr;
  for (atlas_page::shelf &shelf : page.shelves) {
    if (h > shelf.height || shelf.x + w > size)
      continue;
    if (best == nullptr || shelf.height < best->height)
      best = &shelf;
  }
  if (best != nullptr) {
    x = best->x;
    y = best->y;
    best->x += w;
    return true;
  }
  if (page.next_y + h > size)
    return false;
  page.shelves.push_back({.y = page.next_y, .height = h, .x = w});
  x = 0;
  y = page.next_y;
  page.next_y += h;
  return true;
}

bool add_page(atlas_slot &atlas) {
  Image blank = GenImageColor(atlas.size, atlas.size, BLANK);
  const Texture2D texture = LoadTextureFromImage(blank);
  UnloadImage(blank);
  if (!IsTextureValid(texture))
    return false;
  SetTextureFilter(texture, texture_filter_to_raylib(atlas.filter));
  atlas_page page;
  page.texture = texture;
  atlas.pages.push_back(std::move(page));
  return true;
}
} // namespace

atlas_handle atlas_store_create(texture_store &store, const atlas_desc &desc) {
  atlas_slot atlas;
  atlas.alive = true;
  atlas.size = std::max(desc.size, 16);
  atlas.padding = std::max(desc.padding, 0);
  atlas.filter = desc.filter;
  store.atlases.push_back(std::move(atlas));
  return atlas_handle{.id = (u32)store.atlases.size()};
}

texture_handle atlas_store_load(texture_store &store, atlas_handle handle, const char *path) {
  atlas_slot *atlas = atlas_slot_of(store, handle);
  if (atlas == nullptr) {
    NJIN_WARN("atlas: invalid atlas handle");
    return texture_handle{};
  }
  if (path == nullptr) {
    NJIN_WARN("atlas: path is null");
    return texture_handle{};
  }
  const std::string resolved = asset_path(path);
  if (!FileExists(resolved.c_str())) {
    NJIN_WARN("atlas: file not found: %s", path);
    return texture_handle{};
  }
  Image image = LoadImage(resolved.c_str());
  if (image.data == nullptr) {
    NJIN_WARN("atlas: failed to load: %s", path);
    return texture_handle{};
  }
  ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

  const i32 pad = atlas->padding;
  const i32 w = image.width;
  const i32 h = image.height;
  const i32 block_w = w + 2 * pad;
  const i32 block_h = h + 2 * pad;
  if (block_w > atlas->size || block_h > atlas->size) {
    NJIN_WARN("atlas: %s (%dx%d) does not fit a %d px page, loaded as its own texture", path, w, h,
              atlas->size);
    UnloadImage(image);
    return texture_store_load(store, path);
  }

  // Room on an existing page, else on a new one.
  i32 x = 0;
  i32 y = 0;
  atlas_page *page = nullptr;
  for (atlas_page &candidate : atlas->pages) {
    if (place(candidate, atlas->size, block_w, block_h, x, y)) {
      page = &candidate;
      break;
    }
  }
  if (page == nullptr) {
    if (!add_page(*atlas) ||
        !place(atlas->pages.back(), atlas->size, block_w, block_h, x, y)) {
      NJIN_WARN("atlas: cannot add a page, %s loaded as its own texture", path);
      UnloadImage(image);
      return texture_store_load(store, path);
    }
    page = &atlas->pages.back();
  }

  // The image with its edge pixels repeated `pad` times all round, so a
  // filtered or rotated sample at the border reads its own colour, not the
  // neighbour's.
  const Color *src = static_cast<const Color *>(image.data);
  std::vector<Color> block((usize)block_w * (usize)block_h);
  for (i32 row = 0; row < block_h; row++) {
    const i32 sy = std::clamp(row - pad, 0, h - 1);
    for (i32 col = 0; col < block_w; col++) {
      const i32 sx = std::clamp(col - pad, 0, w - 1);
      block[(usize)row * (usize)block_w + (usize)col] = src[(usize)sy * (usize)w + (usize)sx];
    }
  }
  UpdateTextureRec(page->texture,
                   Rectangle{(f32)x, (f32)y, (f32)block_w, (f32)block_h}, block.data());
  UnloadImage(image);

  texture_slot slot;
  slot.texture = page->texture;
  slot.alive = true;
  slot.filter = atlas->filter;
  slot.packed = true;
  slot.area = Rectangle{(f32)(x + pad), (f32)(y + pad), (f32)w, (f32)h};
  store.slots.push_back(std::move(slot));
  const u32 id = (u32)store.slots.size();
  atlas->images.push_back(id);
  return texture_handle{.id = id};
}

void atlas_store_destroy(texture_store &store, atlas_handle handle) {
  atlas_slot *atlas = atlas_slot_of(store, handle);
  if (atlas == nullptr)
    return;
  for (const u32 id : atlas->images)
    texture_store_unload(store, texture_handle{.id = id});
  for (atlas_page &page : atlas->pages)
    UnloadTexture(page.texture);
  *atlas = atlas_slot{};
}

atlas_handle atlas_create(njin_ctx &ctx, const atlas_desc &desc) {
  return atlas_store_create(ctx.texture, desc);
}

texture_handle atlas_load(njin_ctx &ctx, atlas_handle atlas, const char *path) {
  return atlas_store_load(ctx.texture, atlas, path);
}

void atlas_destroy(njin_ctx &ctx, atlas_handle atlas) { atlas_store_destroy(ctx.texture, atlas); }
} // namespace njin
