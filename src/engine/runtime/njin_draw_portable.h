#pragma once
#include "njin_internal_only.h"
#include "njin_renderer.h"
#include "njin_draw_texture.h"
#include <optional>
namespace njin {
struct texture_slot;
void draw_portable_slot_texture(const context &,const texture_slot &,const texture_draw_desc &,bool world_space);
void draw_portable_slot_textures(const context &,const texture_slot &,std::span<const texture_draw_desc>,bool world_space,std::optional<RenderBlend> blend=std::nullopt);
void draw_portable_vertices(const context &,RenderTextureHandle,std::span<const RenderVertex>,rgba,
                           bool linear,bool world_space,std::optional<RenderBlend> blend=std::nullopt,RenderMinFilter min_filter=RenderMinFilter::Inherit);
void draw_portable_solid(const context &,std::span<const vec2>,rgba);

void draw_portable_textures(const context &,RenderTextureHandle,vec2 image_size,
                           std::span<const texture_draw_desc>,bool linear,bool world_space,std::optional<RenderBlend> blend=std::nullopt,RenderMinFilter min_filter=RenderMinFilter::Inherit);
void draw_portable_texture(const context &,RenderTextureHandle,vec2 image_size,
                           const texture_draw_desc &,bool linear,bool world_space,std::optional<RenderBlend> blend=std::nullopt,RenderMinFilter min_filter=RenderMinFilter::Inherit);
}
