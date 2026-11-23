#pragma once
#include "njin_internal_only.h"
#include "njin_renderer.h"
#include "_types.h"
namespace njin {
struct context;
render_texture_handle portable_render_texture_load(context &,u32,u32);
bool portable_render_texture_begin(const context &,render_texture_handle,const rgba *clear=nullptr);
void portable_render_texture_end(const context &);
void portable_render_texture_unload(context &,render_texture_handle);
bool portable_render_texture_save(context &,render_texture_handle,const char *);
void portable_render_texture_draw(const context &,render_texture_handle,vec2,rgba);
}
