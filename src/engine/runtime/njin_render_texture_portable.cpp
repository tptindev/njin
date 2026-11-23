#include "njin_render_texture_portable.h"
#include "njin_texture.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_view.h"
#include "njin_draw_portable.h"
#include <algorithm>
#include <filesystem>
#include <stdexcept>
namespace njin {
render_texture_handle portable_render_texture_load(context &ctx,u32 width,u32 height) {
  if(!width||!height) return {};
  auto &store=ctx_ref<render_texture_store>(ctx);const auto target=renderer_create_target(ctx,width,height);
  render_texture_slot slot;slot.alive=true;slot.portable=target;slot.target.texture.width=int(width);slot.target.texture.height=int(height);
  try {store->slots.push_back(slot);} catch(...) {renderer_destroy_target(ctx,target);throw;}
  return {u32(store->slots.size())};
}
bool portable_render_texture_begin(const context &constant,render_texture_handle handle,const rgba *clear) {
  auto &ctx=const_cast<context&>(constant);auto &store=ctx_ref<render_texture_store>(ctx);
  const auto *slot=render_texture_slot_of(store,handle);if(!slot) return false;
  auto &r=ctx_ref<window_guard>(ctx).renderer_resources;
  if(r.world3d_open) throw std::logic_error("njin render texture: cannot change target inside a 3D pass");
  if(r.active_target.id==slot->portable.id||std::find_if(store->portable_stack.begin(),store->portable_stack.end(),[&](auto h){return h.id==slot->portable.id;})!=store->portable_stack.end())
    throw std::logic_error("njin render texture: recursive target pass");
  const auto parent=r.active_target;store->portable_stack.push_back(parent);
  if(parent) renderer_end_target(ctx);
  try {
    if(clear) renderer_begin_target(ctx,slot->portable,{clear->r,clear->g,clear->b,clear->a});
    else if(r.targets.at(slot->portable.id).initialized) renderer_resume_target(ctx,slot->portable);
    else renderer_begin_target(ctx,slot->portable);
  } catch(...) {store->portable_stack.pop_back();if(parent) renderer_resume_target(ctx,parent);throw;}
  ctx_ref<view_state>(ctx)->offscreen_depth++;return true;
}
void portable_render_texture_end(const context &constant) {
  auto &ctx=const_cast<context&>(constant);auto &store=ctx_ref<render_texture_store>(ctx);auto &r=ctx_ref<window_guard>(ctx).renderer_resources;
  if(r.world3d_open) throw std::logic_error("njin render texture: end the 3D pass first");
  if(store->portable_stack.empty()) throw std::logic_error("njin render texture: end without begin");
  const auto parent=store->portable_stack.back();renderer_end_target(ctx);store->portable_stack.pop_back();
  if(parent) renderer_resume_target(ctx,parent);
  ctx_ref<view_state>(ctx)->offscreen_depth=std::max(0,ctx_ref<view_state>(ctx)->offscreen_depth-1);
}
void portable_render_texture_unload(context &ctx,render_texture_handle handle) {
  auto &store=ctx_ref<render_texture_store>(ctx);const auto *slot=render_texture_slot_of(store,handle);if(!slot) return;
  renderer_destroy_target(ctx,slot->portable);store->slots.at(handle.id-1)={};
}
bool portable_render_texture_save(context &ctx,render_texture_handle handle,const char *path) {
  const auto *slot=render_texture_slot_of(ctx_ref<render_texture_store>(ctx),handle);if(!slot||!path||!*path) return false;
  auto image=renderer_read_target(ctx,slot->portable);
  if(image.format!=RenderImageFormat::RGBA8) throw std::runtime_error("njin render texture: expected RGBA8 color readback");
  std::error_code ec;const auto file=std::filesystem::path(reinterpret_cast<const char8_t*>(path));
  if(file.has_parent_path()) std::filesystem::create_directories(file.parent_path(),ec);
  if(ec) return false;
  Image view{image.pixels.data(),int(image.width),int(image.height),1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
  return ExportImage(view,path);
}
void portable_render_texture_draw(const context &ctx,render_texture_handle handle,vec2 pos,rgba tint) {
  const auto *slot=render_texture_slot_of(ctx_ref<render_texture_store>(ctx),handle);if(!slot) return;
  draw_portable_texture(ctx,renderer_target_texture(const_cast<context&>(ctx),slot->portable),{float(slot->target.texture.width),float(slot->target.texture.height)},
    {.pos=pos,.tint=tint},slot->filter!=filter_nearest,ctx_ref<view_state>(ctx)->world_depth>0);
}
}
