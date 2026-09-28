#pragma once
#include "_types.h"
#include "njin_draw.h"

namespace njin {
struct njin_ctx;

/// @addtogroup grp_texture
/// @{

/// How to create an atlas, see atlas_create().
struct atlas_desc {
  /// Side of each atlas page, in pixels. An image larger than this side (including the border) does not
  /// fit in the atlas and is loaded as a standalone texture.
  i32 size = 2048;
  /// Empty border around each image, in pixels: the outermost pixels are copied into the border so
  /// the image does not bleed into its neighbor when scaled, shrunk or rotated. 0 means no border.
  i32 padding = 1;
  /// Sampling of the whole atlas. Every image in the atlas shares one filter.
  texture_filter filter = filter_linear;
};

/// Creates an atlas: a place that packs many small images into one large texture.
///
/// Raylib merges consecutive draw commands that share a texture into one. When sprites are sorted
/// by layer or by y and use many different textures, draw commands are broken
/// at every texture change; images in the same atlas are the same texture, so those commands merge back
/// into one. See the estimated draw call count in njin_inspector.
///
/// Images loaded with atlas_load() yield an ordinary texture_handle, usable anywhere
/// that takes a texture_handle: sprites, tilemaps, particles, UI, texture_draw().
/// @param ctx Engine context.
/// @param desc Page size, border and filter.
/// @return Handle of the atlas.
atlas_handle atlas_create(njin_ctx &ctx, const atlas_desc &desc = {});

/// Loads an image file and packs it into the atlas.
///
/// The atlas adds a new page when the old page is full. An image can be packed into the atlas only if
/// it fits on one page; a larger image is loaded like texture_load() and a warning is written
/// to the log.
///
/// Unlike texture_load():
/// - texture_size() returns the size of the image, not of the whole page.
/// - A game's own shaders (shader_begin()) sample `texture0` with coordinates over
///   the whole atlas page, not over the image, so a shader that reads the image's 0..1 coordinates
///   will be wrong. For such shaders, load the image with texture_load().
/// - texture_set_filter() changes the filter of the whole page.
/// - Hot reload does not apply.
/// - texture_unload() drops the handle but does not reclaim the space in the atlas.
/// @param ctx Engine context.
/// @param atlas Atlas to pack into.
/// @param path Path of the image file.
/// @return Handle of the texture, or a handle with id 0 if the file is missing or cannot be decoded.
texture_handle atlas_load(njin_ctx &ctx, atlas_handle atlas, const char *path);

/// Destroys the atlas: frees the pages. Every texture_handle taken from this atlas becomes
/// invalid. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param atlas Atlas to destroy.
void atlas_destroy(njin_ctx &ctx, atlas_handle atlas);
/// @}
} // namespace njin
