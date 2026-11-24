// The rmlui feature (njin_rml.h): RmlUi 6.3 documents as game UI, drawn by Njin's
// renderer. As sdl3-proj does it: RmlUi's SDL3 platform layer (input from the window's
// SDL events, clipboard, IME, cursors) and a render interface of Njin's own. The render
// interface follows RmlUi's reference backend (Backends/RmlUi_Renderer_GL3.cpp at the
// pinned tag): premultiplied colours blended ONE, ONE_MINUS_SRC_ALPHA, the transform
// projection * SetTransform's matrix with the geometry's translation added to its
// vertices, scissor in the context's pixels. Clip masks, layers, filters and shaders are
// not implemented (RmlUi falls back to the scissor and draws without the effect).
//
// The RmlUi context is in the window's framebuffer pixels (RmlUi's SDL layer scales the
// mouse by the pixel density); Njin's scissor is in logical pixels, so it is divided back.
#include "njin_rml.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_features.h"
#include "njin_file.h"
#include "njin_image_io.h"
#include "njin_input.h"
#include "njin_log.h"
#include "njin_renderer.h"
#include "njin_sdl_platform.h"
#include "njin_window.h"
#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <RmlUi_Platform_SDL.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace njin {

namespace {

// Documents, stylesheets, fonts and images through Njin's file lookup.
class rml_files final : public Rml::FileInterface {
public:
  Rml::FileHandle Open(const Rml::String &path) override {
    auto file = std::make_unique<file_data>();
    if (!file_read(path.c_str(), file->bytes))
      return 0;
    return Rml::FileHandle(file.release());
  }
  void Close(Rml::FileHandle h) override { delete reinterpret_cast<file_data *>(h); }
  size_t Read(void *buffer, size_t size, Rml::FileHandle h) override {
    file_data &f = *reinterpret_cast<file_data *>(h);
    const size_t n = std::min(size, f.bytes.size() - f.pos);
    std::memcpy(buffer, f.bytes.data() + f.pos, n);
    f.pos += n;
    return n;
  }
  bool Seek(Rml::FileHandle h, long offset, int origin) override {
    file_data &f = *reinterpret_cast<file_data *>(h);
    const long base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? long(f.pos) : long(f.bytes.size());
    const long p = base + offset;
    if (p < 0 || size_t(p) > f.bytes.size())
      return false;
    f.pos = size_t(p);
    return true;
  }
  size_t Tell(Rml::FileHandle h) override { return reinterpret_cast<file_data *>(h)->pos; }
  size_t Length(Rml::FileHandle h) override { return reinterpret_cast<file_data *>(h)->bytes.size(); }

private:
  struct file_data {
    std::string bytes;
    size_t pos = 0;
  };
};

// Geometry stays on the CPU, already as Njin vertices (premultiplied colours), and is
// sent with each draw: UI geometry is small and RmlUi recompiles what changes.
class rml_renderer final : public Rml::RenderInterface {
public:
  context *ctx = nullptr;
  bool drawing = false;      // inside Context::Render()
  Rml::Matrix4f projection;  // framebuffer pixels to clip space
  float density = 1.0f;      // framebuffer pixels per logical pixel (Njin's scissor)
  RenderTextureHandle white; // for geometry without a texture

  Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override {
    auto *g = new geometry();
    g->vertices.reserve(vertices.size());
    for (const Rml::Vertex &v : vertices) {
      RenderVertex o{v.position.x, v.position.y, 0, v.tex_coord.x, v.tex_coord.y};
      o.color = {v.colour.red / 255.0f, v.colour.green / 255.0f, v.colour.blue / 255.0f, v.colour.alpha / 255.0f};
      g->vertices.push_back(o);
    }
    g->indices.assign(indices.begin(), indices.end());
    return Rml::CompiledGeometryHandle(g);
  }

  void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override {
    if (!drawing || ctx == nullptr)
      return;
    const geometry &g = *reinterpret_cast<const geometry *>(handle);
    if (g.indices.empty())
      return;
    const Rml::Matrix4f mvp = (transformed ? projection * transform : projection) *
                              Rml::Matrix4f::Translate(translation.x, translation.y, 0.0f);
    MeshDraw draw{};
    std::memcpy(draw.transform.data(), mvp.data(), sizeof(float) * 16);
    draw.texture = texture ? RenderTextureHandle{(std::uint64_t)texture} : white;
    draw.linear_sampling = true;
    draw.depth_test = false;
    draw.blend = RenderBlend::Premultiplied;
    if (scissored) {
      draw.scissor_enabled = true;
      const float x0 = std::floor(scissor.Left() / density), y0 = std::floor(scissor.Top() / density);
      const float x1 = std::ceil(scissor.Right() / density), y1 = std::ceil(scissor.Bottom() / density);
      if (x1 <= x0 || y1 <= y0)
        return;
      draw.scissor = {(int)x0, (int)y0, (unsigned)(x1 - x0), (unsigned)(y1 - y0)};
    }
    // Njin takes 16-bit indices: runs of whole triangles whose index range fits.
    size_t first = 0;
    while (first + 3 <= g.indices.size()) {
      int lo = INT32_MAX, hi = 0;
      size_t end = first;
      while (end + 3 <= g.indices.size()) {
        int tlo = lo, thi = hi;
        for (size_t k = 0; k < 3; ++k) {
          tlo = std::min(tlo, g.indices[end + k]);
          thi = std::max(thi, g.indices[end + k]);
        }
        if (thi - tlo >= 65536 && end > first)
          break;
        lo = tlo;
        hi = thi;
        end += 3;
      }
      indices.resize(end - first);
      for (size_t i = first; i < end; ++i)
        indices[i - first] = (std::uint16_t)(g.indices[i] - lo);
      draw.vertices = std::span<const RenderVertex>(g.vertices.data() + lo, size_t(hi - lo + 1));
      draw.indices = indices;
      renderer_draw_mesh(*ctx, draw);
      first = end;
    }
  }

  void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override { delete reinterpret_cast<geometry *>(handle); }

  Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source) override {
    if (ctx == nullptr)
      return 0;
    std::string bytes;
    if (!file_read(source.c_str(), bytes)) {
      NJIN_WARN("rml: image not found: %s", source.c_str());
      return 0;
    }
    RenderImageAsset image;
    try {
      image = image_io_decode({reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()}, RenderImageFormat::RGBA8,
                              source);
    } catch (const std::exception &e) {
      NJIN_WARN("rml: %s", e.what());
      return 0;
    }
    if (image.width == 0 || image.height == 0)
      return 0;
    for (size_t i = 0; i < image.pixels.size(); i += 4) { // RmlUi blends premultiplied colours
      const unsigned a = image.pixels[i + 3];
      for (size_t c = 0; c < 3; ++c)
        image.pixels[i + c] = (std::uint8_t)((image.pixels[i + c] * a + 127) / 255);
    }
    dimensions = {(int)image.width, (int)image.height};
    return Rml::TextureHandle(renderer_create_texture(*ctx, image.view()).id);
  }

  Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override {
    if (ctx == nullptr || dimensions.x <= 0 || dimensions.y <= 0)
      return 0;
    const RenderImage image{(unsigned)dimensions.x, (unsigned)dimensions.y, {source.data(), source.size()}};
    return Rml::TextureHandle(renderer_create_texture(*ctx, image).id);
  }

  void ReleaseTexture(Rml::TextureHandle texture) override {
    if (ctx != nullptr && texture)
      renderer_destroy_texture(*ctx, RenderTextureHandle{(std::uint64_t)texture});
  }

  void EnableScissorRegion(bool enable) override { scissored = enable; }
  void SetScissorRegion(Rml::Rectanglei region) override { scissor = region; }
  void SetTransform(const Rml::Matrix4f *t) override {
    transformed = t != nullptr;
    if (t)
      transform = *t;
  }

private:
  struct geometry {
    std::vector<RenderVertex> vertices;
    std::vector<int> indices;
  };
  std::vector<std::uint16_t> indices;
  bool scissored = false, transformed = false;
  Rml::Rectanglei scissor;
  Rml::Matrix4f transform;
};

// RmlUi's SDL system interface (time, cursors, clipboard, IME), its log in Njin's.
class rml_system final : public SystemInterface_SDL {
public:
  using SystemInterface_SDL::SystemInterface_SDL;
  bool LogMessage(Rml::Log::Type type, const Rml::String &message) override {
    if (type <= Rml::Log::LT_ERROR)
      NJIN_ERROR("rml: %s", message.c_str());
    else if (type == Rml::Log::LT_WARNING)
      NJIN_WARN("rml: %s", message.c_str());
    return true;
  }
};

// rml_on(): calls the game back with its context.
class rml_listener final : public Rml::EventListener {
public:
  rml_listener(context &c, std::function<void(context &)> f) : ctx(&c), on(std::move(f)) {}
  void ProcessEvent(Rml::Event &) override {
    if (on)
      on(*ctx);
  }

private:
  context *ctx;
  std::function<void(context &)> on;
};

} // namespace

// The feature's state (njin_ctx_impl.h). RmlUi is process-wide: one context at a time
// runs it, started on the first rml_* call and shut down with the store.
struct rml_store {
  context *ctx = nullptr;
  rml_files files;
  rml_renderer renderer;
  std::unique_ptr<rml_system> system;
  Rml::Context *rml = nullptr;
  SDL_Window *window = nullptr;
  int listener = 0;
  struct model {
    std::string name;
    Rml::DataModelConstructor constructor;
    Rml::DataModelHandle handle;
  };
  std::vector<model> models;
  std::vector<Rml::ElementDocument *> documents; // handle id - 1; null once closed
  std::vector<std::unique_ptr<rml_listener>> listeners;

  ~rml_store() { shutdown(); }
  void shutdown();
};

namespace {
rml_store *g_running = nullptr; // the store RmlUi was started for

// Starts RmlUi for this context on first use: interfaces, the RmlUi context in the
// window's framebuffer pixels, the debugger, the SDL listener.
rml_store &started(context &ctx) {
  rml_store &s = ctx_ref<rml_store>(ctx);
  if (s.rml != nullptr)
    return s;
  SdlPlatform *platform = sdl_platform(ctx);
  if (platform == nullptr)
    throw std::runtime_error("njin rml: RmlUi needs the SDL3/bgfx renderer (graphics_backend is OpenGL)");
  if (g_running != nullptr && g_running != &s)
    throw std::logic_error("njin rml: RmlUi already runs for another context");
  s.ctx = &ctx;
  s.window = platform->window();
  s.renderer.ctx = &ctx;
  const std::uint8_t white[4] = {255, 255, 255, 255};
  s.renderer.white = renderer_create_texture(ctx, RenderImage{1, 1, white});
  s.system = std::make_unique<rml_system>(s.window);
  Rml::SetFileInterface(&s.files);
  Rml::SetRenderInterface(&s.renderer);
  Rml::SetSystemInterface(s.system.get());
  if (!Rml::Initialise())
    throw std::runtime_error("njin rml: RmlUi failed to start");
  g_running = &s;
  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(s.window, &w, &h);
  s.rml = Rml::CreateContext("njin", Rml::Vector2i(std::max(w, 1), std::max(h, 1)));
  if (s.rml == nullptr)
    throw std::runtime_error("njin rml: RmlUi could not create its context");
  const float scale = SDL_GetWindowDisplayScale(s.window);
  s.rml->SetDensityIndependentPixelRatio(scale > 0.0f ? scale : 1.0f);
  Rml::Debugger::Initialise(s.rml);
  s.listener = platform->add_event_listener([&s](const SDL_Event &event) {
    if (s.rml == nullptr)
      return;
    SDL_Event copy = event;
    RmlSDL::InputEventHandler(s.rml, s.window, copy);
  });
  return s;
}

rml_store *running(const context &ctx) {
  const rml_store &s = ctx_ref<rml_store>(const_cast<context &>(ctx));
  return s.rml != nullptr ? const_cast<rml_store *>(&s) : nullptr;
}

Rml::ElementDocument *document_of(const context &ctx, rml_document_handle handle) {
  rml_store *s = running(ctx);
  if (s == nullptr || handle.id == 0 || handle.id > s->documents.size())
    return nullptr;
  return s->documents[handle.id - 1];
}

rml_store::model *model_of(context &ctx, rml_model_handle handle) {
  rml_store &s = ctx_ref<rml_store>(ctx);
  return handle.id == 0 || handle.id > s.models.size() ? nullptr : &s.models[handle.id - 1];
}

bool any_visible(const rml_store &s) {
  return std::any_of(s.documents.begin(), s.documents.end(), [](Rml::ElementDocument *d) { return d && d->IsVisible(); });
}

// Input the UI took is not the game's: the mouse while it is over (or dragging) a
// document's element, the keys while a text field has focus.
void consume_input(context &ctx) {
  rml_store *s = running(ctx);
  if (s == nullptr || !any_visible(*s))
    return;
  if (s->rml->IsMouseInteracting()) {
    for (int b = 0; b < mouse_button_count; ++b)
      mouse_consume(ctx, (mouse_button)b);
    mouse_wheel_consume(ctx);
  }
  if (Rml::Element *focus = s->rml->GetFocusElement()) {
    const Rml::String &tag = focus->GetTagName();
    if (tag == "input" || tag == "textarea" || tag == "select")
      for (int k = key_none + 1; k < key_count; ++k)
        key_consume(ctx, (key_code)k);
  }
}

// After the game's update: models re-read, the context follows the window, layout.
void update_ui(context &ctx) {
  rml_store *s = running(ctx);
  if (s == nullptr)
    return;
  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(s->window, &w, &h);
  if (w > 0 && h > 0 && s->rml->GetDimensions() != Rml::Vector2i(w, h))
    s->rml->SetDimensions({w, h});
  for (rml_store::model &m : s->models)
    if (m.handle)
      m.handle.DirtyAllVariables();
  s->rml->Update();
}

// On top of the world and the engine's UI (phase_post_render).
void render_ui(context &ctx) {
  rml_store *s = running(ctx);
  if (s == nullptr || !any_visible(*s) && !Rml::Debugger::IsVisible())
    return;
  const Rml::Vector2i size = s->rml->GetDimensions();
  const vec2 logical = window_size(ctx);
  s->renderer.density = logical.x > 0.0f ? (float)size.x / logical.x : 1.0f;
  s->renderer.projection = Rml::Matrix4f::ProjectOrtho(0, (float)size.x, (float)size.y, 0, -10000, 10000);
  s->renderer.drawing = true;
  s->rml->Render();
  s->renderer.drawing = false;
}

void setup(context &ctx) {
  ecs_register(ctx, phase_pre_update, consume_input, "rml_input");
  ecs_register(ctx, phase_post_update, update_ui, "rml_update");
  ecs_register(ctx, phase_post_render, render_ui, "rml_render");
}
} // namespace

void rml_store::shutdown() {
  if (rml == nullptr)
    return;
  if (SdlPlatform *platform = ctx != nullptr ? sdl_platform(*ctx) : nullptr)
    platform->remove_event_listener(listener);
  listener = 0;
  // Documents, geometry and textures go back through the interfaces, while the
  // renderer is still alive (the store is freed before the window).
  try {
    Rml::Shutdown();
  } catch (...) {
  }
  if (renderer.white && ctx != nullptr) {
    try {
      renderer_destroy_texture(*ctx, renderer.white);
    } catch (...) {
    }
  }
  renderer.white = {};
  rml = nullptr;
  documents.clear();
  models.clear();
  listeners.clear();
  if (g_running == this)
    g_running = nullptr;
}

void feature_install_rmlui(context &ctx) {
  ctx_ref<rml_store>(ctx).emplace();
  ctx_ref<feature_hooks>(ctx).modules_last.push_back(mod_desc{.name = "njin.rmlui", .setup = setup});
}

bool rml_font_load(context &ctx, const char *path, bool fallback) {
  started(ctx);
  if (path == nullptr || !Rml::LoadFontFace(path, fallback)) {
    NJIN_WARN("rml: cannot load font %s", path ? path : "(null)");
    return false;
  }
  return true;
}

rml_model_handle rml_model(context &ctx, const char *name) {
  rml_store &s = started(ctx);
  const std::string key = name ? name : "";
  for (size_t i = 0; i < s.models.size(); ++i)
    if (s.models[i].name == key)
      return {u32(i + 1)};
  rml_store::model m;
  m.name = key;
  m.constructor = s.rml->CreateDataModel(key);
  if (!m.constructor) {
    NJIN_WARN("rml: cannot create data model '%s'", key.c_str());
    return {};
  }
  m.handle = m.constructor.GetModelHandle();
  s.models.push_back(std::move(m));
  return {u32(s.models.size())};
}

namespace {
template <class Get, class Set, class ToVariant, class FromVariant>
void bind(context &ctx, rml_model_handle model, const char *name, Get get, Set set, ToVariant to, FromVariant from) {
  rml_store::model *m = model_of(ctx, model);
  if (m == nullptr || name == nullptr || !get)
    return;
  Rml::DataSetFunc setter;
  if (set)
    setter = [set, from](const Rml::Variant &v) { set(from(v)); };
  if (!m->constructor.BindFunc(name, [get, to](Rml::Variant &v) { v = to(get()); }, setter))
    NJIN_WARN("rml: cannot bind '%s' in model '%s'", name, m->name.c_str());
}
} // namespace

void rml_bind_bool(context &ctx, rml_model_handle model, const char *name, std::function<bool()> get,
                   std::function<void(bool)> set) {
  bind(ctx, model, name, get, set, [](bool v) { return v; }, [](const Rml::Variant &v) { return v.Get<bool>(); });
}
void rml_bind_int(context &ctx, rml_model_handle model, const char *name, std::function<i64()> get,
                  std::function<void(i64)> set) {
  bind(ctx, model, name, get, set, [](i64 v) { return (int64_t)v; }, [](const Rml::Variant &v) { return (i64)v.Get<int64_t>(); });
}
void rml_bind_float(context &ctx, rml_model_handle model, const char *name, std::function<f64()> get,
                    std::function<void(f64)> set) {
  bind(ctx, model, name, get, set, [](f64 v) { return (double)v; }, [](const Rml::Variant &v) { return (f64)v.Get<double>(); });
}
void rml_bind_string(context &ctx, rml_model_handle model, const char *name, std::function<std::string()> get,
                     std::function<void(const std::string &)> set) {
  bind(ctx, model, name, get, set, [](const std::string &v) { return Rml::String(v); },
       [](const Rml::Variant &v) { return std::string(v.Get<Rml::String>()); });
}

void rml_bind_event(context &ctx, rml_model_handle model, const char *name, std::function<void(context &)> on) {
  rml_store::model *m = model_of(ctx, model);
  if (m == nullptr || name == nullptr || !on)
    return;
  context *c = &ctx;
  if (!m->constructor.BindEventCallback(name, [c, on](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { on(*c); }))
    NJIN_WARN("rml: cannot bind event '%s' in model '%s'", name, m->name.c_str());
}

rml_document_handle rml_document_load(context &ctx, const char *path) {
  rml_store &s = started(ctx);
  Rml::ElementDocument *d = path ? s.rml->LoadDocument(path) : nullptr;
  if (d == nullptr) {
    NJIN_WARN("rml: cannot load document %s", path ? path : "(null)");
    return {};
  }
  d->Show();
  s.documents.push_back(d);
  return {u32(s.documents.size())};
}

void rml_document_show(context &ctx, rml_document_handle document, bool visible) {
  if (Rml::ElementDocument *d = document_of(ctx, document))
    visible ? d->Show() : d->Hide();
}

bool rml_document_visible(const context &ctx, rml_document_handle document) {
  const Rml::ElementDocument *d = document_of(ctx, document);
  return d != nullptr && d->IsVisible();
}

void rml_document_close(context &ctx, rml_document_handle document) {
  Rml::ElementDocument *d = document_of(ctx, document);
  if (d == nullptr)
    return;
  d->Close();
  ctx_ref<rml_store>(ctx)->documents[document.id - 1] = nullptr;
}

bool rml_on(context &ctx, rml_document_handle document, const char *element_id, const char *event,
            std::function<void(context &)> on) {
  Rml::ElementDocument *d = document_of(ctx, document);
  Rml::Element *e = d && element_id ? d->GetElementById(element_id) : nullptr;
  if (e == nullptr || event == nullptr) {
    NJIN_WARN("rml: no element '%s' for rml_on", element_id ? element_id : "(null)");
    return false;
  }
  rml_store &s = ctx_ref<rml_store>(ctx);
  s.listeners.push_back(std::make_unique<rml_listener>(ctx, std::move(on)));
  e->AddEventListener(event, s.listeners.back().get());
  return true;
}

bool rml_set_inner(context &ctx, rml_document_handle document, const char *element_id, const char *rml) {
  Rml::ElementDocument *d = document_of(ctx, document);
  Rml::Element *e = d && element_id ? d->GetElementById(element_id) : nullptr;
  if (e == nullptr)
    return false;
  e->SetInnerRML(rml ? rml : "");
  return true;
}

bool rml_wants_mouse(const context &ctx) {
  rml_store *s = running(ctx);
  return s != nullptr && any_visible(*s) && s->rml->IsMouseInteracting();
}

void rml_debugger(context &ctx, bool visible) {
  started(ctx);
  Rml::Debugger::SetVisible(visible);
}

} // namespace njin
