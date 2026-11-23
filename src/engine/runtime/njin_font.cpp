#include "njin_font.h"
#include "njin_image_io.h"
#include "njin_log.h"
#include "njin_path.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_view.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <stdexcept>

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

// A font collection (.ttc: msyh.ttc, the Japanese and Chinese fonts on
// Windows) holds several fonts after a header, and raylib reads a font only
// from the start of the file. The first font's table directory is moved to
// the start in place of the header: its tables' offsets count from the start
// of the file, so they still point at the same bytes. False when the bytes
// are a collection that cannot be read that way.
bool first_of_collection(std::vector<unsigned char> &bytes) {
  const auto be32 = [&](size_t at) {
    return (u32)bytes[at] << 24 | (u32)bytes[at + 1] << 16 | (u32)bytes[at + 2] << 8 | (u32)bytes[at + 3];
  };
  if (bytes.size() < 16 || std::memcmp(bytes.data(), "ttcf", 4) != 0)
    return true; // a single font
  const size_t dir = be32(12);
  if (dir + 12 > bytes.size())
    return false;
  const size_t tables = (size_t)bytes[dir + 4] << 8 | bytes[dir + 5];
  const size_t length = 12 + tables * 16;
  if (dir + length > bytes.size())
    return false;
  // Every table must lie past the bytes the directory is moved over.
  for (size_t t = 0; t < tables; t++)
    if (be32(dir + 12 + t * 16 + 8) < dir + length)
      return false;
  std::memmove(bytes.data(), bytes.data() + dir, length);
  return true;
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
    const int cp = font.glyphs[i].value;
    int advance = 0;
    stbtt_GetCodepointHMetrics(&info, cp, &advance, nullptr);
    if (font.glyphs[i].advanceX > 0) {
      if (advance > 0)
        font.glyphs[i].advanceX = (int)std::lround((float)advance * scale);
    } else if (advance == 0 && stbtt_FindGlyphIndex(&info, cp) != 0 && font.recs[i].width > 0.0f) {
      // A mark drawn over the letter before it (Thai vowels and tones, a
      // combining accent): raylib steps a glyph with no advance by its whole
      // width, which pushed every mark off its letter. One pixel is the least
      // it can step.
      font.glyphs[i].advanceX = 1;
    }
  }
}

// Bakes one atlas; null when the bytes are not a font raylib can read.
const Font *bake(font_slot &slot, i32 px) {
  std::vector<int> cps = font_codepoints();
  cps.insert(cps.end(), slot.extra.begin(), slot.extra.end());
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
void free_portable_metrics(PortableFontAtlas &atlas) {
  std::free(atlas.metrics.glyphs); std::free(atlas.metrics.recs); atlas.metrics={};
}

// The bgfx path's glyph atlas without raylib: raylib's LoadFontData (FONT_DEFAULT and
// FONT_BITMAP) and GenImageFontAtlas (packMethod 0), raylib 5.x src/rtext.c (zlib,
// THIRD_PARTY_NOTICES.md), translated step for step so the atlas and the metrics come
// out the same: glyph bitmaps from stb_truetype at the pixel height, offsets lifted by
// the ascent, spaces as blank cells their advance wide, rows `padding` apart, a white 3 x
// 3 corner and grey+alpha as RGBA8 (white, coverage in alpha). The glyphs and recs are
// malloc'd (free_portable_metrics); a glyph's image carries its size only.
} // namespace
bool font_bake_atlas(const unsigned char *bytes,bool pixel,i32 px,const std::vector<int> &cps,Font &font,std::vector<unsigned char> &rgba) {
  stbtt_fontinfo info;
  if(!bytes||!stbtt_InitFont(&info,bytes,0)) return false;
  const float scale=stbtt_ScaleForPixelHeight(&info,(float)px);
  int ascent=0,descent=0,line_gap=0; stbtt_GetFontVMetrics(&info,&ascent,&descent,&line_gap);
  int count=0;
  for(int cp:cps) if(stbtt_FindGlyphIndex(&info,cp)>0) ++count;
  if(count<=0) return false;
  font.glyphs=static_cast<GlyphInfo*>(std::calloc(size_t(count),sizeof(GlyphInfo)));
  font.recs=static_cast<Rectangle*>(std::malloc(size_t(count)*sizeof(Rectangle)));
  if(!font.glyphs||!font.recs) throw std::bad_alloc();
  font.glyphCount=count;
  std::vector<std::vector<unsigned char>> bitmaps(static_cast<size_t>(count));
  int k=0;
  for(int cp:cps) {
    if(stbtt_FindGlyphIndex(&info,cp)<=0) continue;
    GlyphInfo &g=font.glyphs[k];auto &bitmap=bitmaps[size_t(k)];
    g.value=cp;
    int w=0,h=0;
    unsigned char *data=stbtt_GetCodepointBitmap(&info,scale,scale,cp,&w,&h,&g.offsetX,&g.offsetY);
    if(data) {
      stbtt_GetCodepointHMetrics(&info,cp,&g.advanceX,nullptr);
      g.advanceX=(int)((float)g.advanceX*scale);
      g.image.width=w;g.image.height=h;
      bitmap.assign(data,data+size_t(w)*h);
      stbtt_FreeBitmap(data,nullptr);
      g.offsetY+=(int)((float)ascent*scale);
    }
    if(cp==0x20||cp==0x3000) {
      stbtt_GetCodepointHMetrics(&info,cp,&g.advanceX,nullptr);
      g.advanceX=(int)((float)g.advanceX*scale);
      if(g.advanceX>0) {g.image.width=g.advanceX;g.image.height=px;bitmap.assign(size_t(g.advanceX)*px,0);}
      else {g.advanceX=0;g.image.width=0;g.image.height=px;bitmap.clear();}
    }
    if(pixel) for(auto &v:bitmap) v=v<80?0:255; // FONT_BITMAP_ALPHA_THRESHOLD
    g.image.data=nullptr;g.image.mipmaps=1;g.image.format=PIXELFORMAT_UNCOMPRESSED_GRAYSCALE;
    ++k;
  }
  const int padding=font.glyphPadding;
  int total_width=0;
  for(int i=0;i<count;++i) total_width+=font.glyphs[i].image.width+2*padding;
  const int padded=px+2*padding;
  const float area=total_width*padded*1.2f;
  const int size=(int)powf(2,ceilf(logf(sqrtf(area))/logf(2)));
  int aw=size,ah=area<((size*size)/2)?size/2:size;
  std::vector<unsigned char> atlas(size_t(aw)*ah,0);
  int ox=padding,oy=padding;
  for(int i=0;i<count;++i) {
    const auto &g=font.glyphs[i];const auto &bitmap=bitmaps[size_t(i)];
    if(ox>=aw-g.image.width-2*padding) {
      ox=padding;oy+=px+2*padding;
      if(oy>ah-px-padding) {ah*=2;atlas.resize(size_t(aw)*ah,0);}
    }
    for(int y=0;y<g.image.height;++y) for(int x=0;x<g.image.width;++x) {
      const int dx=ox+x,dy=oy+y;
      if(dx>=0&&dx<aw&&dy>=0&&dy<ah&&!bitmap.empty()) atlas[size_t(dy)*aw+dx]=bitmap[size_t(y)*g.image.width+x];
    }
    font.recs[i]={(float)ox,(float)oy,(float)g.image.width,(float)g.image.height};
    ox+=g.image.width+2*padding;
  }
  if(aw>=3&&ah>=3) for(int i=0,at=aw*ah-1;i<3;++i,at-=aw) atlas[size_t(at)]=atlas[size_t(at-1)]=atlas[size_t(at-2)]=255;
  rgba.resize(atlas.size()*4);
  for(size_t i=0;i<atlas.size();++i) {rgba[i*4]=rgba[i*4+1]=rgba[i*4+2]=255;rgba[i*4+3]=atlas[i];}
  font.texture.width=aw;font.texture.height=ah; // metadata only; id remains zero
  return true;
}
namespace {
void release_portable(context *ctx,font_slot &slot) {
  if(!ctx) return;
  auto &store=ctx_mut<font_store>(*ctx);
  for(const auto &q:ctx_ref<view_state>(*ctx)->text_layer) {
    auto *pending=q.portable_fallback?&store->fallback:slot_of(store,q.font);
    if(pending==&slot) throw std::logic_error("njin font: font is referenced by deferred text");
  }
  const auto &window=ctx_ref<window_guard>(*ctx);
  for(const auto &[px,atlas]:slot.portable_atlases)
    if(window.graphics->texture_in_use(atlas.texture)||window.renderer_resources.pending_3d_textures.contains(atlas.texture.id))
      throw std::logic_error("njin font: atlas is referenced by submitted draws");
  while(!slot.portable_atlases.empty()) {
    auto it=slot.portable_atlases.begin();
    renderer_destroy_texture(*ctx,it->second.texture);
    free_portable_metrics(it->second); slot.portable_atlases.erase(it);
  }
}
const PortableFontAtlas *portable_atlas_of(context &ctx,font_slot &slot,i32 px) {
  if(auto found=slot.portable_atlases.find(px);found!=slot.portable_atlases.end()) return &found->second;
  if(slot.portable_atlases.size()>=max_atlases) {
    auto nearest=slot.portable_atlases.begin();
    for(auto it=slot.portable_atlases.begin();it!=slot.portable_atlases.end();++it)
      if(std::abs(it->first-px)<std::abs(nearest->first-px)) nearest=it;
    return &nearest->second;
  }
  std::vector<int> cps=font_codepoints(); cps.insert(cps.end(),slot.extra.begin(),slot.extra.end());
  PortableFontAtlas atlas;
  auto &font=atlas.metrics;
  font.baseSize=px; font.glyphPadding=4;
  std::vector<unsigned char> rgba;
  try {
    if(!font_bake_atlas(slot.bytes,slot.pixel,px,cps,font,rgba)) { free_portable_metrics(atlas); return nullptr; }
    round_advances(slot,font,px);
    atlas.texture=renderer_create_texture(ctx,{(unsigned)font.texture.width,(unsigned)font.texture.height,rgba});
    try { return &slot.portable_atlases.emplace(px,atlas).first->second; }
    catch(...) { renderer_destroy_texture(ctx,atlas.texture); throw; }
  } catch(...) { free_portable_metrics(atlas); throw; }
}
} // namespace

void font_slot::unload_atlases() {
  for (auto &[px, font] : atlases)
    UnloadFont(font);
  atlases.clear();
  for(auto &[px,atlas]:portable_atlases) free_portable_metrics(atlas);
  portable_atlases.clear();
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

font_handle font_store_load(font_store &store, const char *path, i32 size, bool pixel,context *portable_ctx) {
  if (path == nullptr) {
    NJIN_WARN("font: path is null");
    return font_handle{};
  }
  const std::string resolved = asset_path(path);
  std::error_code missing;
  if (!std::filesystem::is_regular_file(std::filesystem::path(reinterpret_cast<const char8_t *>(resolved.c_str())), missing)) {
    NJIN_WARN("font: file not found: %s", path);
    return font_handle{};
  }
  font_slot slot;
  if (!image_io_read_file(resolved.c_str(), slot.owned)) {
    NJIN_WARN("font: cannot read: %s", path);
    return font_handle{};
  }
  const i32 length = (i32)slot.owned.size();
  if (!first_of_collection(slot.owned)) {
    NJIN_WARN("font: cannot read the font collection: %s", path);
    return font_handle{};
  }
  slot.bytes = slot.owned.data();
  slot.length = length;
  slot.alive = true;
  slot.pixel = pixel;
  // The vector's buffer does not move when the slot does, so `bytes` stays.
  store.slots.push_back(std::move(slot));
  font_slot &kept = store.slots.back();
  if (portable_ctx ? portable_atlas_of(*portable_ctx,kept,size>0 ? font_px((f32)size):probe_px)==nullptr : atlas_of(kept,size>0 ? font_px((f32)size):probe_px)==nullptr) {
    NJIN_WARN("font: failed to load: %s", path);
    store.slots.pop_back();
    return font_handle{};
  }
  return font_handle{.id = (u32)store.slots.size()};
}

void font_store_unload(font_store &store, font_handle handle,context *portable_ctx) {
  font_slot *slot = slot_of(store, handle);
  if (slot == nullptr)
    return;
  release_portable(portable_ctx,*slot);
  slot->unload_atlases();
  *slot = font_slot{};
}

void font_store_set_pixel(font_store &store, font_handle handle, bool pixel,context *portable_ctx) {
  font_slot *slot = handle.id == 0 ? &store.fallback : slot_of(store, handle);
  if (slot == nullptr || slot->pixel == pixel)
    return;
  release_portable(portable_ctx,*slot);
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

void font_store_add_chars(font_store &store, font_handle handle, const char *text,context *portable_ctx) {
  font_slot *slot = handle.id == 0 ? &store.fallback : slot_of(store, handle);
  if (slot == nullptr || text == nullptr)
    return;
  const std::vector<int> &common = font_codepoints();
  std::vector<int> added;
  int count = 0;
  int *cps = LoadCodepoints(text, &count);
  for (int i = 0; i < count; i++) {
    const int c = cps[i];
    if (c < 0x20 || std::binary_search(common.begin(), common.end(), c) ||
        std::binary_search(slot->extra.begin(), slot->extra.end(), c))
      continue;
    added.push_back(c);
  }
  UnloadCodepoints(cps);
  if (added.empty())
    return;
  release_portable(portable_ctx,*slot);
  slot->extra.insert(slot->extra.end(), added.begin(), added.end());
  std::sort(slot->extra.begin(), slot->extra.end());
  slot->extra.erase(std::unique(slot->extra.begin(), slot->extra.end()), slot->extra.end());
  // Baked again, with them, the next time each size is drawn.
  slot->unload_atlases();
}

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
const PortableFontAtlas *font_store_portable_atlas(context &ctx,font_store &store,font_handle handle,i32 px,bool fallback_only) {
  auto *slot=fallback_only?&store.fallback:slot_of(store,handle);
  if(!slot) slot=slot_of(store,store.chosen);
  if(!slot) slot=&store.fallback;
  auto *atlas=portable_atlas_of(ctx,*slot,px);
  if(!atlas && slot!=&store.fallback) atlas=portable_atlas_of(ctx,store.fallback,px);
  return atlas;
}
vec2 font_measure_portable(const Font &font,const char *text) {
  if(!text || !*text) return {};
  float width=0,max_width=0,height=(float)font.baseSize;
  for(const char *p=text;*p;) {
    int bytes=0; const int cp=GetCodepointNext(p,&bytes); p+=std::max(bytes,1);
    if(cp=='\n') { max_width=std::max(max_width,width); width=0; height+=font.baseSize+2; }
    else {
      const int index=GetGlyphIndex(font,cp);
      width+=font.glyphs[index].advanceX>0 ? font.glyphs[index].advanceX : font.recs[index].width+font.glyphs[index].offsetX;
    }
  }
  return {std::max(width,max_width),height};
}
} // namespace njin
