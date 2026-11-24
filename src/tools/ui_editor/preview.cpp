// The Viewport shows the layout as the game draws it: the real njin UI code
// (ui_begin, ui_button, ...) runs against the editor's context and draws into a
// Njin render target, which the Viewport window shows. Only the selection and
// drag gizmos on top are ImGui.
#include "preview.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "modules/ui.h"
#include "njin_input_impl.h"
#include "njin_view.h"
#include "njin_draw_text.h"
#include "njin_ecs.h"
#include "njin_render.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include "njin_renderer.h"
#include "njin_sdl_platform.h"

namespace ui_editor {

namespace {
njin::RenderTargetHandle g_target{};
unsigned g_target_w = 0, g_target_h = 0;

// The label as the engine shows it: everything before "##".
std::string shown(const std::string &label) {
  const size_t hidden = label.find("##");
  return hidden == std::string::npos ? label : label.substr(0, hidden);
}

void ensure_target(njin::context &ctx, int w, int h) {
  if (g_target && g_target_w == (unsigned)w && g_target_h == (unsigned)h)
    return;
  if (g_target)
    njin::renderer_destroy_target(ctx, g_target);
  g_target = njin::renderer_create_target(ctx, (unsigned)w, (unsigned)h);
  g_target_w = (unsigned)w;
  g_target_h = (unsigned)h;
}

// Rewrites the input the window gave (SdlPlatform's input hook, before any system
// runs) to what the Viewport should get: nothing while editing, and in play mode the
// mouse only while it is over the image and the keys only while the Viewport has
// focus. Typed text stays with ImGui's fields.
void feed_input(editor_app &app, int k, njin::input_store &input) {
  const njin::input_frame real = input.cur;
  input.cur = njin::input_frame{};
  input.text_count = 0;
  input.backspaces = 0;
  std::fill(std::begin(input.key_hidden), std::end(input.key_hidden), false);
  std::fill(std::begin(input.mouse_hidden), std::end(input.mouse_hidden), false);
  input.cur.mouse_pos = {-100000.0f, -100000.0f};
  if (!app.play_mode)
    return;
  if (app.view_focused) {
    for (const njin::key_code key : {njin::key_up, njin::key_down, njin::key_left, njin::key_right,
                                     njin::key_enter, njin::key_space, njin::key_escape,
                                     njin::key_backspace})
      input.cur.keys[key] = real.keys[key];
  }
  if (app.view_hovered) {
    const vec2 m = real.mouse_pos;
    const vec2 design{(m.x - app.view_image_min.x) / app.zoom, (m.y - app.view_image_min.y) / app.zoom};
    input.cur.mouse_pos = design * (float)k;
    input.cur.mouse_delta = input.cur.mouse_pos - input.prev.mouse_pos;
    for (int b = 0; b < njin::mouse_button_count; b++)
      input.cur.mouse[b] = real.mouse[b];
  }
}

// Image widgets carry a path; the texture is loaded once and shared.
void bind_textures(editor_app &app, ui_layout &layout) {
  for (auto &p : layout.panels) {
    for (auto &w : p.widgets) {
      if (w.kind != ui_widget_kind::image || w.texture_path.empty()) {
        w.texture = {};
        continue;
      }
      auto it = app.textures.find(w.texture_path);
      if (it == app.textures.end()) {
        njin::texture_handle t = njin::texture_load(*app.ctx, w.texture_path.c_str());
        if (t.id == 0 && !app.file_path.empty()) {
          // Relative to the layout file, as a game would keep it next to its assets.
          const std::string dir = std::filesystem::path(app.file_path).parent_path().string();
          t = njin::texture_load(*app.ctx, (dir + "/" + w.texture_path).c_str());
        }
        it = app.textures.emplace(w.texture_path, t).first;
      }
      w.texture = it->second;
    }
  }
}

// Works out where each widget went, the same way the engine's place() does, from
// the panel rectangles the engine recorded this frame. Everything in `k` space
// until the end, where it is divided back to design pixels.
void measure_boxes(editor_app &app, const ui_layout &layout, const njin::ui_style &style, int k) {
  app.boxes.clear();
  const auto &rects = njin::ctx_ref<njin::ui_state>(*app.ctx)->panels;
  size_t next = 0;
  const float kf = (float)k;
  auto to_design = [kf](rect r) { return rect{r.pos / kf, r.size / kf}; };

  for (size_t pi = 0; pi < layout.panels.size(); ++pi) {
    const ui_panel_data &p = layout.panels[pi];
    if (!p.visible)
      continue;
    if (next >= rects.size())
      return;
    const rect area = rects[next++];
    panel_box box;
    box.node = node_ref::make_panel((int)pi);
    box.area = to_design(area);

    // The engine shrinks a panel taller than the screen: recover the factor
    // from its width, which is scaled the same way.
    const float base_w = (p.width > 0.0f ? p.width : style.width) * style.scale;
    const float fit = base_w > 0.0f ? area.size.x / base_w : 1.0f;
    auto sc = [&](float v) { return v * style.scale * fit; };
    const float pad = sc(style.padding), gap = sc(style.spacing);
    const float inner = area.size.x - 2.0f * pad;
    const float font = sc(style.font_size);
    float cursor = area.pos.y + pad;
    if (!p.title.empty()) {
      const vec2 m = njin::text_measure(*app.ctx, p.title.c_str(), font * 1.3f, style.font);
      cursor += m.y + gap * 1.6f;
    }
    int row_cols = 0, row_index = 0;
    float row_y = 0.0f;
    auto place = [&](float h) {
      if (row_cols > 0) {
        const float w = (inner - gap * (float)(row_cols - 1)) / (float)row_cols;
        const rect r{{area.pos.x + pad + (w + gap) * (float)row_index, row_y}, {w, h}};
        if (++row_index >= row_cols) {
          row_cols = 0;
          cursor = row_y + h + gap;
        }
        return r;
      }
      const rect r{{area.pos.x + pad, cursor}, {inner, h}};
      cursor += h + gap;
      return r;
    };

    for (size_t wi = 0; wi < p.widgets.size(); ++wi) {
      const ui_widget_data &w = p.widgets[wi];
      rect r{{area.pos.x + pad, cursor}, {inner, 0.0f}};
      switch (w.kind) {
      case ui_widget_kind::row:
        if (w.columns > 0) {
          row_cols = w.columns;
          row_index = 0;
          row_y = cursor;
        }
        r.size.y = 0.0f;
        break;
      case ui_widget_kind::space:
        r.size.y = sc(w.height);
        cursor += r.size.y;
        break;
      case ui_widget_kind::label:
      case ui_widget_kind::keybind: // ui_draw_panel shows a keybind as a label
        r = place(njin::text_measure(*app.ctx, shown(w.label).c_str(), font, style.font).y);
        break;
      case ui_widget_kind::progress:
        r = place(sc(style.widget_height) * 0.6f);
        break;
      case ui_widget_kind::circle: {
        const float d = std::max(sc(w.diameter), 1.0f);
        const rect slot = place(d);
        const float side = std::min(d, slot.size.x);
        r = rect{{slot.pos.x + (slot.size.x - side) * 0.5f, slot.pos.y + (d - side) * 0.5f}, {side, side}};
        break;
      }
      case ui_widget_kind::image:
        if (w.texture.id != 0) {
          const rect slot = place(w.size.y * style.scale);
          r = rect{{slot.pos.x + (slot.size.x - w.size.x * style.scale) * 0.5f, slot.pos.y},
                   w.size * style.scale};
        }
        break;
      case ui_widget_kind::choice:
        if (!w.options.empty())
          r = place(sc(style.widget_height));
        break;
      default:
        r = place(sc(style.widget_height));
        break;
      }
      box.widgets.push_back({(int)wi, to_design(r)});
    }
    app.boxes.push_back(std::move(box));
  }

  for (size_t i = 0; i < layout.popups.size(); ++i) {
    if (!layout.popups[i].open)
      continue;
    if (next >= rects.size())
      return;
    panel_box box;
    box.node = node_ref::make_popup((int)i);
    box.area = to_design(rects[next++]);
    app.boxes.push_back(std::move(box));
  }
}
} // namespace

void preview_install(editor_app &app) {
  if (njin::SdlPlatform *platform = njin::sdl_platform(*app.ctx))
    platform->set_input_hook([&app](njin::input_store &input) { feed_input(app, app.preview_k, input); });
}

void preview_begin_frame(editor_app &app) {
  if (!app.play_mode)
    njin::ctx_ref<njin::ui_state>(*app.ctx)->focus = 0; // no focus highlight while editing
}

void preview_render(editor_app &app) {
  njin::context &ctx = *app.ctx;
  ui_layout &layout = app.shown_layout();

  // Rendered at k times the design size when zoomed in, so text is baked at the
  // size it is shown and stays sharp. Everything the engine scales goes through
  // style.scale; the panel offsets do not, so they are scaled by hand.
  const vec2 res = layout.design_resolution;
  int k = std::clamp((int)std::ceil(app.zoom - 0.01f), 1, 4);
  while (k > 1 && (res.x * (float)k > 4096.0f || res.y * (float)k > 4096.0f))
    k--;
  if (k != app.preview_k) {
    // The engine sizes a panel from its height last frame: forget the old ones.
    njin::ctx_ref<njin::ui_state>(ctx)->heights.clear();
    njin::ctx_ref<njin::ui_state>(ctx)->natural.clear();
  }
  app.preview_k = k;
  ensure_target(ctx, (int)(res.x * (float)k), (int)(res.y * (float)k));

  // screen_size() answers the virtual size while one is set; view.drawing
  // stays off, so text is drawn at once into the texture and not queued.
  njin::ctx_ref<njin::view_state>(ctx)->size = res * (float)k;

  njin::ui_style style = layout.custom_style ? layout.style
                         : app.preview_style == 1 ? njin::ui_pixel_style()
                                                  : njin::ui_default_style();
  const bool saved_custom = layout.custom_style;
  const njin::ui_style saved_style = layout.style;
  std::vector<vec2> saved_offsets;
  for (auto &p : layout.panels) {
    saved_offsets.push_back(p.offset);
    p.offset = p.offset * (float)k;
  }
  layout.custom_style = true;
  layout.style = style;
  layout.style.scale *= (float)k;
  bind_textures(app, layout);

  njin::renderer_begin_target(ctx, g_target, {0, 0, 0, 0});
  njin::ui_draw_layout(ctx, layout, [&](const njin::ui_layout_event &ev) {
    if (!app.play_mode)
      return;
    char msg[256];
    switch (ev.kind) {
    case njin::ui_layout_event::button_clicked:
      std::snprintf(msg, sizeof msg, "[%s] button_clicked: %s", ev.panel_id, ev.widget_id);
      break;
    case njin::ui_layout_event::value_changed:
      std::snprintf(msg, sizeof msg, "[%s] value_changed: %s (bool %d, float %.3f, int %d)",
                    ev.panel_id, ev.widget_id, ev.bool_val ? 1 : 0, ev.float_val, ev.int_val);
      break;
    case njin::ui_layout_event::popup_dismissed:
      std::snprintf(msg, sizeof msg, "popup_dismissed: %s -> nút %d", ev.widget_id, ev.int_val);
      break;
    default:
      return;
    }
    app.log(msg);
  });
  njin::renderer_end_target(ctx);

  measure_boxes(app, layout, layout.style, k);

  layout.custom_style = saved_custom;
  layout.style = saved_style;
  for (size_t i = 0; i < layout.panels.size(); ++i)
    layout.panels[i].offset = saved_offsets[i];
  njin::ctx_ref<njin::view_state>(ctx)->size = {};
}

njin::RenderTextureHandle preview_texture(editor_app &app) {
  return g_target ? njin::renderer_target_texture(*app.ctx, g_target) : njin::RenderTextureHandle{};
}

void preview_shutdown(editor_app &app) {
  if (njin::SdlPlatform *platform = njin::sdl_platform(*app.ctx))
    platform->set_input_hook({});
  if (g_target)
    njin::renderer_destroy_target(*app.ctx, g_target);
  g_target = {};
}

} // namespace ui_editor
