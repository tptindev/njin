#include "njin_ui_layout.h"
#include "njin_log.h"
#include <cstdio>
#include <sstream>

namespace njin {

// --- ui_panel_data methods ---

ui_widget_data *ui_panel_data::find_widget(std::string_view widget_id) {
  for (auto &w : widgets) {
    if (w.id == widget_id)
      return &w;
  }
  return nullptr;
}

const ui_widget_data *ui_panel_data::find_widget(std::string_view widget_id) const {
  for (const auto &w : widgets) {
    if (w.id == widget_id)
      return &w;
  }
  return nullptr;
}

// --- ui_layout methods ---

ui_panel_data *ui_layout::find_panel(std::string_view panel_id) {
  for (auto &p : panels) {
    if (p.id == panel_id)
      return &p;
  }
  return nullptr;
}

const ui_panel_data *ui_layout::find_panel(std::string_view panel_id) const {
  for (const auto &p : panels) {
    if (p.id == panel_id)
      return &p;
  }
  return nullptr;
}

ui_widget_data *ui_layout::find_widget(std::string_view widget_id) {
  for (auto &p : panels) {
    if (auto *w = p.find_widget(widget_id))
      return w;
  }
  return nullptr;
}

const ui_widget_data *ui_layout::find_widget(std::string_view widget_id) const {
  for (const auto &p : panels) {
    if (const auto *w = p.find_widget(widget_id))
      return w;
  }
  return nullptr;
}

ui_popup_data *ui_layout::find_popup(std::string_view popup_id) {
  for (auto &pop : popups) {
    if (pop.id == popup_id)
      return &pop;
  }
  return nullptr;
}

const ui_popup_data *ui_layout::find_popup(std::string_view popup_id) const {
  for (const auto &pop : popups) {
    if (pop.id == popup_id)
      return &pop;
  }
  return nullptr;
}

// --- Style Presets ---

ui_style ui_pixel_style() {
  ui_style s = ui_default_style();
  s.font_size = 16.0f;
  s.padding = 10.0f;
  s.spacing = 4.0f;
  s.widget_height = 22.0f;
  s.width = 300.0f;
  s.toast_width = 260.0f;
  s.toast_margin = {10.0f, 10.0f};
  for (ui_look *look : {&s.panel, &s.button, &s.track, &s.fill, &s.knob, &s.toast}) {
    look->normal.roundness = 0.0f;
    look->focused.roundness = 0.0f;
    look->pressed.roundness = 0.0f;
    look->disabled.roundness = 0.0f;
  }
  return s;
}

// --- JSON Parsing & Serializing ---

namespace {
rgba rgba_from_json(const json_value &v, rgba def) {
  if (v.is(json_value::array) && v.size() >= 4) {
    return rgba{v[(usize)0].f32_or(def.r), v[(usize)1].f32_or(def.g),
                v[(usize)2].f32_or(def.b), v[(usize)3].f32_or(def.a)};
  }
  return def;
}

json_value json_from_rgba(const rgba &c) {
  json_value a = json_value::make_array();
  a.push(c.r).push(c.g).push(c.b).push(c.a);
  return a;
}

ui_widget_kind kind_from_string(std::string_view s) {
  if (s == "space") return ui_widget_kind::space;
  if (s == "button") return ui_widget_kind::button;
  if (s == "toggle") return ui_widget_kind::toggle;
  if (s == "slider") return ui_widget_kind::slider;
  if (s == "choice") return ui_widget_kind::choice;
  if (s == "progress") return ui_widget_kind::progress;
  if (s == "image") return ui_widget_kind::image;
  if (s == "row") return ui_widget_kind::row;
  if (s == "keybind") return ui_widget_kind::keybind;
  if (s == "circle") return ui_widget_kind::circle;
  return ui_widget_kind::label;
}

const char *string_from_kind(ui_widget_kind k) {
  switch (k) {
  case ui_widget_kind::space: return "space";
  case ui_widget_kind::button: return "button";
  case ui_widget_kind::toggle: return "toggle";
  case ui_widget_kind::slider: return "slider";
  case ui_widget_kind::choice: return "choice";
  case ui_widget_kind::progress: return "progress";
  case ui_widget_kind::image: return "image";
  case ui_widget_kind::row: return "row";
  case ui_widget_kind::keybind: return "keybind";
  case ui_widget_kind::circle: return "circle";
  default: return "label";
  }
}
} // namespace

bool ui_layout_parse(const json_value &json, ui_layout &out) {
  if (!json.is(json_value::object))
    return false;

  out.version = json["version"].int_or(1);

  if (json["design_resolution"].is(json_value::array) && json["design_resolution"].size() >= 2) {
    out.design_resolution.x = json["design_resolution"][(usize)0].f32_or(1280.0f);
    out.design_resolution.y = json["design_resolution"][(usize)1].f32_or(720.0f);
  }

  // Parse style nếu có
  if (json["style"].is(json_value::object)) {
    const auto &st = json["style"];
    out.custom_style = true;
    if (st["preset"].is(json_value::string)) {
      std::string pr = st["preset"].string_or("");
      if (pr == "pixel")
        out.style = ui_pixel_style();
    }
    out.style.font_size = st["font_size"].f32_or(out.style.font_size);
    out.style.scale = st["scale"].f32_or(out.style.scale);
    out.style.padding = st["padding"].f32_or(out.style.padding);
    out.style.spacing = st["spacing"].f32_or(out.style.spacing);
    out.style.widget_height = st["widget_height"].f32_or(out.style.widget_height);
    out.style.width = st["width"].f32_or(out.style.width);

    if (st["panel"].is(json_value::object)) {
      const auto &pl = st["panel"];
      out.style.panel.normal.color = rgba_from_json(pl["color"], out.style.panel.normal.color);
      out.style.panel.normal.outline = rgba_from_json(pl["outline"], out.style.panel.normal.outline);
      out.style.panel.normal.roundness = pl["roundness"].f32_or(out.style.panel.normal.roundness);
      out.style.panel.text = rgba_from_json(pl["text"], out.style.panel.text);
    }
    if (st["button"].is(json_value::object)) {
      const auto &bl = st["button"];
      out.style.button.normal.color = rgba_from_json(bl["normal"], out.style.button.normal.color);
      out.style.button.focused.color = rgba_from_json(bl["focused"], out.style.button.focused.color);
      out.style.button.pressed.color = rgba_from_json(bl["pressed"], out.style.button.pressed.color);
      out.style.button.normal.roundness = bl["roundness"].f32_or(out.style.button.normal.roundness);
      out.style.button.focused.roundness = out.style.button.normal.roundness;
      out.style.button.pressed.roundness = out.style.button.normal.roundness;
      out.style.button.text = rgba_from_json(bl["text"], out.style.button.text);
    }
    if (st["track"].is(json_value::object)) {
      const auto &tl = st["track"];
      out.style.track.normal.color = rgba_from_json(tl["color"], out.style.track.normal.color);
      out.style.track.normal.roundness = tl["roundness"].f32_or(out.style.track.normal.roundness);
    }
    if (st["fill"].is(json_value::object)) {
      const auto &fl = st["fill"];
      out.style.fill.normal.color = rgba_from_json(fl["color"], out.style.fill.normal.color);
      out.style.fill.normal.roundness = fl["roundness"].f32_or(out.style.fill.normal.roundness);
    }
    if (st["knob"].is(json_value::object)) {
      const auto &kl = st["knob"];
      out.style.knob.normal.color = rgba_from_json(kl["color"], out.style.knob.normal.color);
    }
  } else {
    out.custom_style = false;
  }

  // Parse panels
  out.panels.clear();
  if (json["panels"].is(json_value::array)) {
    for (const auto &pj : json["panels"].items) {
      if (!pj.is(json_value::object))
        continue;
      ui_panel_data p;
      p.id = pj["id"].string_or("panel");
      p.title = pj["title"].string_or("");
      if (pj["anchor"].is(json_value::array) && pj["anchor"].size() >= 2) {
        p.anchor.x = pj["anchor"][(usize)0].f32_or(0.5f);
        p.anchor.y = pj["anchor"][(usize)1].f32_or(0.5f);
      }
      if (pj["pivot"].is(json_value::array) && pj["pivot"].size() >= 2) {
        p.pivot.x = pj["pivot"][(usize)0].f32_or(0.5f);
        p.pivot.y = pj["pivot"][(usize)1].f32_or(0.5f);
      }
      if (pj["offset"].is(json_value::array) && pj["offset"].size() >= 2) {
        p.offset.x = pj["offset"][(usize)0].f32_or(0.0f);
        p.offset.y = pj["offset"][(usize)1].f32_or(0.0f);
      }
      p.width = pj["width"].f32_or(0.0f);
      p.background = pj["background"].bool_or(true);
      p.visible = pj["visible"].bool_or(true);

      // Widgets trong panel
      if (pj["widgets"].is(json_value::array)) {
        for (const auto &wj : pj["widgets"].items) {
          if (!wj.is(json_value::object))
            continue;
          ui_widget_data w;
          w.kind = kind_from_string(wj["type"].string_or("label"));
          w.id = wj["id"].string_or("");
          w.label = wj["label"].string_or(wj["text"].string_or(""));
          w.text = wj["text"].string_or("");
          w.enabled = wj["enabled"].bool_or(true);

          w.bool_val = wj["value"].bool_or(false);
          w.float_val = wj["value"].f32_or(0.0f);
          w.min_val = wj["min"].f32_or(0.0f);
          w.max_val = wj["max"].f32_or(1.0f);
          w.step = wj["step"].f32_or(0.0f);
          w.percent = wj["percent"].bool_or(false);

          w.int_val = wj["index"].int_or(0);
          if (wj["options"].is(json_value::array)) {
            for (const auto &opt : wj["options"].items)
              w.options.push_back(opt.string_or(""));
          }

          w.height = wj["height"].f32_or(10.0f);
          w.columns = wj["columns"].int_or(2);

          w.texture_path = wj["texture"].string_or("");
          if (wj["size"].is(json_value::array) && wj["size"].size() >= 2) {
            w.size.x = wj["size"][(usize)0].f32_or(64.0f);
            w.size.y = wj["size"][(usize)1].f32_or(64.0f);
          }
          if (wj["source"].is(json_value::array) && wj["source"].size() >= 4) {
            w.source.pos.x = wj["source"][(usize)0].f32_or(0.0f);
            w.source.pos.y = wj["source"][(usize)1].f32_or(0.0f);
            w.source.size.x = wj["source"][(usize)2].f32_or(0.0f);
            w.source.size.y = wj["source"][(usize)3].f32_or(0.0f);
          }

          w.diameter = wj["diameter"].f32_or(96.0f);
          w.thickness = wj["thickness"].f32_or(10.0f);
          w.start_angle = wj["start_angle"].f32_or(0.0f);
          w.clockwise = wj["clockwise"].bool_or(true);
          w.round_caps = wj["round_caps"].bool_or(false);
          w.show_track = wj["show_track"].bool_or(true);
          w.track_color = rgba_from_json(wj["track_color"], w.track_color);
          w.fill_color = rgba_from_json(wj["fill_color"], w.fill_color);

          w.action_name = wj["action"].string_or("");
          w.pad = wj["pad"].bool_or(false);

          p.widgets.push_back(std::move(w));
        }
      }
      out.panels.push_back(std::move(p));
    }
  }

  // Parse popups
  out.popups.clear();
  if (json["popups"].is(json_value::array)) {
    for (const auto &popj : json["popups"].items) {
      if (!popj.is(json_value::object))
        continue;
      ui_popup_data pop;
      pop.id = popj["id"].string_or("popup");
      pop.title = popj["title"].string_or("");
      pop.message = popj["message"].string_or("");
      pop.default_button = popj["default_button"].int_or(0);
      pop.cancel_button = popj["cancel_button"].int_or(-1);
      pop.width = popj["width"].f32_or(0.0f);
      pop.open = popj["open"].bool_or(false);
      if (popj["buttons"].is(json_value::array)) {
        pop.buttons.clear();
        for (const auto &btn : popj["buttons"].items)
          pop.buttons.push_back(btn.string_or("OK"));
      }
      out.popups.push_back(std::move(pop));
    }
  }

  return true;
}

json_value ui_layout_to_json(const ui_layout &layout) {
  json_value root = json_value::make_object();
  root.set("version", layout.version);

  json_value res = json_value::make_array();
  res.push(layout.design_resolution.x).push(layout.design_resolution.y);
  root.set("design_resolution", std::move(res));

  if (layout.custom_style) {
    json_value st = json_value::make_object();
    st.set("font_size", layout.style.font_size);
    st.set("scale", layout.style.scale);
    st.set("padding", layout.style.padding);
    st.set("spacing", layout.style.spacing);
    st.set("widget_height", layout.style.widget_height);
    st.set("width", layout.style.width);

    // Panel
    json_value pl = json_value::make_object();
    pl.set("color", json_from_rgba(layout.style.panel.normal.color));
    pl.set("outline", json_from_rgba(layout.style.panel.normal.outline));
    pl.set("roundness", layout.style.panel.normal.roundness);
    pl.set("text", json_from_rgba(layout.style.panel.text));
    st.set("panel", std::move(pl));

    // Button
    json_value bl = json_value::make_object();
    bl.set("normal", json_from_rgba(layout.style.button.normal.color));
    bl.set("focused", json_from_rgba(layout.style.button.focused.color));
    bl.set("pressed", json_from_rgba(layout.style.button.pressed.color));
    bl.set("roundness", layout.style.button.normal.roundness);
    bl.set("text", json_from_rgba(layout.style.button.text));
    st.set("button", std::move(bl));

    // Track, fill, knob
    json_value tl = json_value::make_object();
    tl.set("color", json_from_rgba(layout.style.track.normal.color));
    tl.set("roundness", layout.style.track.normal.roundness);
    st.set("track", std::move(tl));

    json_value fl = json_value::make_object();
    fl.set("color", json_from_rgba(layout.style.fill.normal.color));
    fl.set("roundness", layout.style.fill.normal.roundness);
    st.set("fill", std::move(fl));

    json_value kl = json_value::make_object();
    kl.set("color", json_from_rgba(layout.style.knob.normal.color));
    st.set("knob", std::move(kl));

    root.set("style", std::move(st));
  }

  // Panels
  json_value panels = json_value::make_array();
  for (const auto &p : layout.panels) {
    json_value pj = json_value::make_object();
    pj.set("id", p.id);
    if (!p.title.empty())
      pj.set("title", p.title);

    json_value anch = json_value::make_array();
    anch.push(p.anchor.x).push(p.anchor.y);
    pj.set("anchor", std::move(anch));

    json_value piv = json_value::make_array();
    piv.push(p.pivot.x).push(p.pivot.y);
    pj.set("pivot", std::move(piv));

    if (p.offset.x != 0.0f || p.offset.y != 0.0f) {
      json_value off = json_value::make_array();
      off.push(p.offset.x).push(p.offset.y);
      pj.set("offset", std::move(off));
    }

    if (p.width > 0.0f)
      pj.set("width", p.width);
    if (!p.background)
      pj.set("background", false);
    if (!p.visible)
      pj.set("visible", false);

    json_value widgets = json_value::make_array();
    for (const auto &w : p.widgets) {
      json_value wj = json_value::make_object();
      wj.set("type", string_from_kind(w.kind));
      if (!w.id.empty())
        wj.set("id", w.id);

      switch (w.kind) {
      case ui_widget_kind::label:
        wj.set("text", w.label);
        break;
      case ui_widget_kind::space:
        wj.set("height", w.height);
        break;
      case ui_widget_kind::button:
        wj.set("label", w.label);
        if (!w.enabled)
          wj.set("enabled", false);
        break;
      case ui_widget_kind::toggle:
        wj.set("label", w.label);
        wj.set("value", w.bool_val);
        break;
      case ui_widget_kind::slider:
        wj.set("label", w.label);
        wj.set("value", w.float_val);
        wj.set("min", w.min_val);
        wj.set("max", w.max_val);
        if (w.step > 0.0f)
          wj.set("step", w.step);
        if (w.percent)
          wj.set("percent", true);
        break;
      case ui_widget_kind::choice:
        wj.set("label", w.label);
        wj.set("index", w.int_val);
        {
          json_value opts = json_value::make_array();
          for (const auto &opt : w.options)
            opts.push(opt);
          wj.set("options", std::move(opts));
        }
        break;
      case ui_widget_kind::progress:
        wj.set("value", w.float_val);
        if (!w.text.empty())
          wj.set("text", w.text);
        break;
      case ui_widget_kind::image:
        if (!w.texture_path.empty())
          wj.set("texture", w.texture_path);
        {
          json_value sz = json_value::make_array();
          sz.push(w.size.x).push(w.size.y);
          wj.set("size", std::move(sz));
        }
        if (w.source.size.x > 0.0f && w.source.size.y > 0.0f) {
          json_value src = json_value::make_array();
          src.push(w.source.pos.x).push(w.source.pos.y).push(w.source.size.x).push(w.source.size.y);
          wj.set("source", std::move(src));
        }
        break;
      case ui_widget_kind::row:
        wj.set("columns", w.columns);
        break;
      case ui_widget_kind::circle:
        wj.set("value", w.float_val);
        wj.set("diameter", w.diameter);
        wj.set("thickness", w.thickness);
        if (w.start_angle != 0.0f)
          wj.set("start_angle", w.start_angle);
        if (!w.clockwise)
          wj.set("clockwise", false);
        if (w.round_caps)
          wj.set("round_caps", true);
        if (!w.show_track)
          wj.set("show_track", false);
        if (w.track_color.a > 0.0f)
          wj.set("track_color", json_from_rgba(w.track_color));
        if (w.fill_color.a > 0.0f)
          wj.set("fill_color", json_from_rgba(w.fill_color));
        if (!w.text.empty())
          wj.set("text", w.text);
        if (w.percent)
          wj.set("percent", true);
        break;
      case ui_widget_kind::keybind:
        wj.set("label", w.label);
        if (!w.action_name.empty())
          wj.set("action", w.action_name);
        if (w.pad)
          wj.set("pad", true);
        break;
      }
      widgets.push(std::move(wj));
    }
    pj.set("widgets", std::move(widgets));
    panels.push(std::move(pj));
  }
  root.set("panels", std::move(panels));

  // Popups
  if (!layout.popups.empty()) {
    json_value popups = json_value::make_array();
    for (const auto &pop : layout.popups) {
      json_value popj = json_value::make_object();
      popj.set("id", pop.id);
      if (!pop.title.empty())
        popj.set("title", pop.title);
      if (!pop.message.empty())
        popj.set("message", pop.message);
      if (pop.default_button != 0)
        popj.set("default_button", pop.default_button);
      if (pop.cancel_button != -1)
        popj.set("cancel_button", pop.cancel_button);
      if (pop.width > 0.0f)
        popj.set("width", pop.width);
      if (pop.open)
        popj.set("open", true);
      json_value btns = json_value::make_array();
      for (const auto &btn : pop.buttons)
        btns.push(btn);
      popj.set("buttons", std::move(btns));
      popups.push(std::move(popj));
    }
    root.set("popups", std::move(popups));
  }

  return root;
}

bool ui_layout_load(njin_ctx &ctx, const char *path, ui_layout &out) {
  (void)ctx;
  json_value json;
  if (!json_load(path, json)) {
    NJIN_WARN("ui_layout_load: failed to load JSON file '%s'", path != nullptr ? path : "");
    return false;
  }
  return ui_layout_parse(json, out);
}

bool ui_layout_save(const char *path, const ui_layout &layout, bool pretty) {
  json_value json = ui_layout_to_json(layout);
  return json_save(path, json, pretty);
}

// --- Drawing and Execution ---

bool ui_draw_panel(njin_ctx &ctx, ui_layout &layout, const char *panel_id,
                   const ui_event_callback &on_event) {
  ui_panel_data *p = layout.find_panel(panel_id != nullptr ? panel_id : "");
  if (p == nullptr)
    return false;

  if (layout.custom_style) {
    ui_style_set(ctx, layout.style);
  }

  bool any_interacted = false;

  ui_panel_desc desc;
  desc.id = p->id.c_str();
  desc.title = p->title.empty() ? nullptr : p->title.c_str();
  desc.anchor = p->anchor;
  desc.pivot = p->pivot;
  desc.offset = p->offset;
  desc.width = p->width;
  desc.background = p->background;

  ui_begin(ctx, desc);

  for (auto &w : p->widgets) {
    w.clicked = false;
    w.changed = false;

    switch (w.kind) {
    case ui_widget_kind::label:
      ui_label(ctx, w.label.c_str());
      break;

    case ui_widget_kind::space:
      ui_space(ctx, w.height);
      break;

    case ui_widget_kind::row:
      ui_row(ctx, w.columns);
      break;

    case ui_widget_kind::button: {
      const bool c = ui_button(ctx, w.label.c_str(), w.enabled);
      if (c) {
        w.clicked = true;
        any_interacted = true;
        if (on_event) {
          ui_layout_event ev;
          ev.kind = ui_layout_event::button_clicked;
          ev.panel_id = p->id.c_str();
          ev.widget_id = w.id.c_str();
          on_event(ev);
        }
      }
      break;
    }

    case ui_widget_kind::toggle: {
      const bool c = ui_toggle(ctx, w.label.c_str(), w.bool_val);
      if (c) {
        w.changed = true;
        any_interacted = true;
        if (on_event) {
          ui_layout_event ev;
          ev.kind = ui_layout_event::value_changed;
          ev.panel_id = p->id.c_str();
          ev.widget_id = w.id.c_str();
          ev.bool_val = w.bool_val;
          on_event(ev);
        }
      }
      break;
    }

    case ui_widget_kind::slider: {
      const bool c = ui_slider(ctx, w.label.c_str(), w.float_val, w.min_val, w.max_val, w.step, w.percent);
      if (c) {
        w.changed = true;
        any_interacted = true;
        if (on_event) {
          ui_layout_event ev;
          ev.kind = ui_layout_event::value_changed;
          ev.panel_id = p->id.c_str();
          ev.widget_id = w.id.c_str();
          ev.float_val = w.float_val;
          on_event(ev);
        }
      }
      break;
    }

    case ui_widget_kind::choice: {
      if (!w.options.empty()) {
        const bool c = ui_choice(ctx, w.label.c_str(), w.int_val, w.options);
        if (c) {
          w.changed = true;
          any_interacted = true;
          if (on_event) {
            ui_layout_event ev;
            ev.kind = ui_layout_event::value_changed;
            ev.panel_id = p->id.c_str();
            ev.widget_id = w.id.c_str();
            ev.int_val = w.int_val;
            on_event(ev);
          }
        }
      }
      break;
    }

    case ui_widget_kind::progress:
      ui_progress(ctx, w.float_val, w.text.empty() ? nullptr : w.text.c_str());
      break;

    case ui_widget_kind::circle: {
      ui_circle_desc d;
      d.value = w.float_val;
      d.diameter = w.diameter;
      d.thickness = w.thickness;
      d.start_angle = w.start_angle;
      d.clockwise = w.clockwise;
      d.round_caps = w.round_caps;
      d.show_track = w.show_track;
      d.track = w.track_color;
      d.fill = w.fill_color;
      d.text = w.text.empty() ? nullptr : w.text.c_str();
      d.percent = w.percent;
      ui_progress_circle(ctx, d);
      break;
    }

    case ui_widget_kind::image:
      if (w.texture.id != 0) {
        ui_image(ctx, w.texture, w.size, w.source);
      }
      break;

    case ui_widget_kind::keybind:
      // Keybinds in games typically map to action_handle; game handles rebind specifically
      ui_label(ctx, w.label.c_str());
      break;
    }
  }

  ui_end(ctx);
  return any_interacted;
}

void ui_draw_layout(njin_ctx &ctx, ui_layout &layout, const ui_event_callback &on_event) {
  // Áp dụng custom style nếu có
  if (layout.custom_style) {
    ui_style_set(ctx, layout.style);
  }

  for (auto &p : layout.panels) {
    if (p.visible) {
      ui_draw_panel(ctx, layout, p.id.c_str(), on_event);
    }
  }

  for (auto &pop : layout.popups) {
    if (pop.open) {
      ui_popup_desc desc;
      desc.id = pop.id.c_str();
      desc.title = pop.title.empty() ? nullptr : pop.title.c_str();
      desc.message = pop.message.empty() ? nullptr : pop.message.c_str();
      desc.default_button = pop.default_button;
      desc.cancel_button = pop.cancel_button;
      desc.width = pop.width;
      for (usize i = 0; i < 4; i++)
        desc.buttons[i] = i < pop.buttons.size() ? pop.buttons[i].c_str() : nullptr;

      const i32 pick = ui_popup(ctx, desc, pop.open);
      if (pick >= 0 && on_event) {
        ui_layout_event ev;
        ev.kind = ui_layout_event::popup_dismissed;
        ev.widget_id = pop.id.c_str();
        ev.int_val = pick;
        on_event(ev);
      }
    }
  }
}

// --- Value Getters / Setters ---

bool ui_layout_is_clicked(const ui_layout &layout, const char *widget_id) {
  if (const auto *w = layout.find_widget(widget_id != nullptr ? widget_id : ""))
    return w->clicked;
  return false;
}

bool ui_layout_get_bool(const ui_layout &layout, const char *widget_id, bool fallback) {
  if (const auto *w = layout.find_widget(widget_id != nullptr ? widget_id : ""))
    return w->bool_val;
  return fallback;
}

void ui_layout_set_bool(ui_layout &layout, const char *widget_id, bool value) {
  if (auto *w = layout.find_widget(widget_id != nullptr ? widget_id : ""))
    w->bool_val = value;
}

f32 ui_layout_get_float(const ui_layout &layout, const char *widget_id, f32 fallback) {
  if (const auto *w = layout.find_widget(widget_id != nullptr ? widget_id : ""))
    return w->float_val;
  return fallback;
}

void ui_layout_set_float(ui_layout &layout, const char *widget_id, f32 value) {
  if (auto *w = layout.find_widget(widget_id != nullptr ? widget_id : ""))
    w->float_val = value;
}

i32 ui_layout_get_int(const ui_layout &layout, const char *widget_id, i32 fallback) {
  if (const auto *w = layout.find_widget(widget_id != nullptr ? widget_id : ""))
    return w->int_val;
  return fallback;
}

void ui_layout_set_int(ui_layout &layout, const char *widget_id, i32 value) {
  if (auto *w = layout.find_widget(widget_id != nullptr ? widget_id : ""))
    w->int_val = value;
}

const char *ui_layout_get_text(const ui_layout &layout, const char *widget_id, const char *fallback) {
  if (const auto *w = layout.find_widget(widget_id != nullptr ? widget_id : ""))
    return w->label.c_str();
  return fallback;
}

void ui_layout_set_text(ui_layout &layout, const char *widget_id, const char *text) {
  if (auto *w = layout.find_widget(widget_id != nullptr ? widget_id : ""))
    w->label = text != nullptr ? text : "";
}

// --- Code Generators ---

std::string ui_panel_generate_cpp(const ui_panel_data &p, const char *func_name) {
  std::ostringstream ss;
  ss << "// Auto-generated by njin UI Editor\n";
  ss << "void " << (func_name != nullptr ? func_name : "draw_panel") << "(njin::njin_ctx &ctx) {\n";
  ss << "  njin::ui_begin(ctx, {\n";
  ss << "    .id = \"" << p.id << "\",\n";
  if (!p.title.empty())
    ss << "    .title = \"" << p.title << "\",\n";
  ss << "    .anchor = {" << p.anchor.x << "f, " << p.anchor.y << "f},\n";
  ss << "    .pivot = {" << p.pivot.x << "f, " << p.pivot.y << "f},\n";
  if (p.offset.x != 0.0f || p.offset.y != 0.0f)
    ss << "    .offset = {" << p.offset.x << "f, " << p.offset.y << "f},\n";
  if (p.width > 0.0f)
    ss << "    .width = " << p.width << "f,\n";
  if (!p.background)
    ss << "    .background = false,\n";
  ss << "  });\n\n";

  for (const auto &w : p.widgets) {
    switch (w.kind) {
    case ui_widget_kind::label:
      ss << "  njin::ui_label(ctx, \"" << w.label << "\");\n";
      break;
    case ui_widget_kind::space:
      ss << "  njin::ui_space(ctx, " << w.height << "f);\n";
      break;
    case ui_widget_kind::row:
      ss << "  njin::ui_row(ctx, " << w.columns << ");\n";
      break;
    case ui_widget_kind::button: {
      const std::string name = !w.id.empty() ? w.id : "btn";
      ss << "  if (njin::ui_button(ctx, \"" << w.label << "\")) {\n";
      ss << "    // Action for: " << name << "\n";
      ss << "  }\n";
      break;
    }
    case ui_widget_kind::toggle: {
      const std::string var = !w.id.empty() ? w.id : "toggle_val";
      ss << "  static bool " << var << " = " << (w.bool_val ? "true" : "false") << ";\n";
      ss << "  if (njin::ui_toggle(ctx, \"" << w.label << "\", " << var << ")) {\n";
      ss << "    // Changed: " << var << "\n";
      ss << "  }\n";
      break;
    }
    case ui_widget_kind::slider: {
      const std::string var = !w.id.empty() ? w.id : "slider_val";
      ss << "  static float " << var << " = " << w.float_val << "f;\n";
      ss << "  if (njin::ui_slider(ctx, \"" << w.label << "\", " << var << ", "
         << w.min_val << "f, " << w.max_val << "f, " << w.step << "f, "
         << (w.percent ? "true" : "false") << ")) {\n";
      ss << "    // Changed: " << var << "\n";
      ss << "  }\n";
      break;
    }
    case ui_widget_kind::choice: {
      const std::string var = !w.id.empty() ? w.id : "choice_idx";
      ss << "  static int " << var << " = " << w.int_val << ";\n";
      ss << "  if (njin::ui_choice(ctx, \"" << w.label << "\", " << var << ", {";
      for (usize i = 0; i < w.options.size(); i++) {
        if (i > 0) ss << ", ";
        ss << "\"" << w.options[i] << "\"";
      }
      ss << "})) {\n";
      ss << "    // Changed: " << var << "\n";
      ss << "  }\n";
      break;
    }
    case ui_widget_kind::progress:
      if (!w.text.empty())
        ss << "  njin::ui_progress(ctx, " << w.float_val << "f, \"" << w.text << "\");\n";
      else
        ss << "  njin::ui_progress(ctx, " << w.float_val << "f);\n";
      break;
    case ui_widget_kind::circle: {
      const ui_circle_desc def;
      ss << "  njin::ui_progress_circle(ctx, {.value = " << w.float_val << "f, .diameter = " << w.diameter
         << "f, .thickness = " << w.thickness << "f";
      if (w.start_angle != def.start_angle)
        ss << ", .start_angle = " << w.start_angle << "f";
      if (!w.clockwise)
        ss << ", .clockwise = false";
      if (w.round_caps)
        ss << ", .round_caps = true";
      if (!w.show_track)
        ss << ", .show_track = false";
      const auto color = [&](const char *field, rgba c) {
        if (c.a > 0.0f)
          ss << ", ." << field << " = {" << c.r << "f, " << c.g << "f, " << c.b << "f, " << c.a << "f}";
      };
      color("track", w.track_color);
      color("fill", w.fill_color);
      if (!w.text.empty())
        ss << ", .text = \"" << w.text << "\"";
      else if (w.percent)
        ss << ", .percent = true";
      ss << "});\n";
      break;
    }
    case ui_widget_kind::image:
      ss << "  // njin::ui_image(ctx, texture, {" << w.size.x << "f, " << w.size.y << "f});\n";
      break;
    case ui_widget_kind::keybind:
      ss << "  // njin::ui_keybind(ctx, \"" << w.label << "\", action);\n";
      break;
    }
  }

  ss << "\n  njin::ui_end(ctx);\n";
  ss << "}\n";
  return ss.str();
}

std::string ui_layout_generate_cpp(const ui_layout &layout, const char *func_name) {
  std::ostringstream ss;
  ss << "// Auto-generated by njin UI Editor\n";
  ss << "#include \"njin.h\"\n\n";

  if (layout.custom_style) {
    ss << "// Tùy chọn: Gọi trong setup() để áp dụng Style tùy biến cho UI:\n";
    ss << "void setup_ui_theme(njin::njin_ctx &ctx) {\n";
    ss << "  njin::ui_style s = njin::ui_default_style();\n";
    ss << "  s.font_size = " << layout.style.font_size << "f;\n";
    ss << "  s.scale = " << layout.style.scale << "f;\n";
    ss << "  s.padding = " << layout.style.padding << "f;\n";
    ss << "  s.spacing = " << layout.style.spacing << "f;\n";
    ss << "  s.widget_height = " << layout.style.widget_height << "f;\n";
    ss << "  s.width = " << layout.style.width << "f;\n";
    ss << "  s.panel.normal.color = {" << layout.style.panel.normal.color.r << "f, "
       << layout.style.panel.normal.color.g << "f, " << layout.style.panel.normal.color.b << "f, "
       << layout.style.panel.normal.color.a << "f};\n";
    ss << "  s.panel.normal.roundness = " << layout.style.panel.normal.roundness << "f;\n";
    ss << "  s.button.normal.roundness = " << layout.style.button.normal.roundness << "f;\n";
    ss << "  s.button.focused.roundness = " << layout.style.button.normal.roundness << "f;\n";
    ss << "  s.button.pressed.roundness = " << layout.style.button.normal.roundness << "f;\n";
    ss << "  s.button.normal.color = {" << layout.style.button.normal.color.r << "f, "
       << layout.style.button.normal.color.g << "f, " << layout.style.button.normal.color.b << "f, "
       << layout.style.button.normal.color.a << "f};\n";
    ss << "  njin::ui_style_set(ctx, s);\n";
    ss << "}\n\n";
  } else {
    ss << "// Lưu ý: Layout này đang ở chế độ kế thừa Style của game (custom_style = false).\n";
    ss << "// Game chỉ cần gọi njin::ui_style_set(...) theo theme của game (như Pixel Art hoặc Modern),\n";
    ss << "// các hàm vẽ bên dưới sẽ tự động thừa hưởng 100% theme đó!\n\n";
  }

  for (const auto &p : layout.panels) {
    std::string p_func = std::string(func_name != nullptr ? func_name : "draw") + "_" + p.id;
    ss << ui_panel_generate_cpp(p, p_func.c_str()) << "\n";
  }

  return ss.str();
}

} // namespace njin
