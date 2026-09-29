#pragma once
#include "_math.h"
#include "_types.h"
#include "njin_json.h"
#include "njin_ui.h"
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace njin {
struct context;

/// @addtogroup grp_ui
/// @{

/// Kind of widget in a declarative UI layout.
enum class ui_widget_kind {
  label,    ///< A line of text.
  space,    ///< Vertical empty space.
  button,   ///< A button.
  toggle,   ///< An on/off switch.
  slider,   ///< A value slider.
  choice,   ///< A list of options.
  progress, ///< A progress bar.
  image,    ///< A still image.
  row,      ///< Lays the next widgets out in a horizontal row.
  keybind,  ///< A line for binding a control key.
  circle,   ///< A circular progress bar.
};

/// Configuration and state of one widget.
struct ui_widget_data {
  ui_widget_kind kind = ui_widget_kind::label; ///< Kind of widget.
  std::string id;       ///< Unique identifier (used to query events and values).
  std::string label;    ///< Label shown on the widget (supports ## to hide the id).
  std::string text;     ///< Secondary string, or the text drawn on a progress bar.
  bool enabled = true;  ///< Whether the widget is enabled.

  // Interactive values
  bool bool_val = false;       ///< Toggle state.
  f32 float_val = 0.0f;        ///< Value of a slider or progress bar (0..1).
  f32 min_val = 0.0f;          ///< Smallest value of a slider.
  f32 max_val = 1.0f;          ///< Largest value of a slider.
  f32 step = 0.0f;             ///< Step of a slider.
  bool percent = false;        ///< Show as % instead of a number (slider, circle).

  i32 int_val = 0;                     ///< Current choice of a choice widget (index).
  std::vector<std::string> options;    ///< The options of a choice widget.

  // Size and layout
  f32 height = 10.0f;          ///< Height (for kind == space).
  i32 columns = 2;             ///< Number of columns (for kind == row).

  // Image (kind == image)
  std::string texture_path;    ///< Path of the image file.
  texture_handle texture{};    ///< Image handle once loaded.
  vec2 size{64.0f, 64.0f};     ///< Size the image is drawn at.
  rect source{};               ///< Region cut from the image.

  // Progress ring (kind == circle): also uses float_val, text and percent
  f32 diameter = 96.0f;        ///< Diameter of the ring.
  f32 thickness = 10.0f;       ///< Width of the ring.
  f32 start_angle = 0.0f;      ///< Where it starts, degrees from the top, clockwise.
  bool clockwise = true;       ///< `false`: fills counter-clockwise.
  bool round_caps = false;     ///< Rounds both ends of the filled part.
  bool show_track = true;      ///< Draw the background ring.
  rgba track_color{0.0f, 0.0f, 0.0f, 0.0f}; ///< Background ring colour. Alpha 0: uses the style.
  rgba fill_color{0.0f, 0.0f, 0.0f, 0.0f};  ///< Filled part colour. Alpha 0: uses the style.

  // Keybind (kind == keybind)
  std::string action_name;     ///< Name of the action to rebind.
  bool pad = false;            ///< Rebind a gamepad button instead of a key.

  // State of the last frame
  bool clicked = false;        ///< Was pressed this frame (for a button).
  bool changed = false;        ///< The value changed this frame (slider, toggle, choice).
};

/// Data of a panel that holds a list of widgets.
struct ui_panel_data {
  std::string id = "panel";    ///< Unique name of the panel.
  std::string title;           ///< Title at the top of the panel.
  vec2 anchor{0.5f, 0.5f};     ///< Where on the screen the panel hangs, as a ratio ({0.5, 0.5} is the middle).
  vec2 pivot{0.5f, 0.5f};      ///< Which point of the panel sits on the anchor ({0.5, 0.5} is the centre).
  vec2 offset{0.0f, 0.0f};     ///< Pixel shift from the anchor.
  f32 width = 0.0f;            ///< Panel width (0 uses ui_style::width).
  bool background = true;      ///< Whether the panel background is drawn.
  bool visible = true;         ///< Whether the panel is shown when the layout is drawn.
  std::vector<ui_widget_data> widgets; ///< The widgets of the panel.

  /// Finds a widget by ID in this panel.
  /// @param widget_id ID of the widget.
  /// @return Pointer to the widget, or `nullptr` if there is none.
  ui_widget_data *find_widget(std::string_view widget_id);
  /// @copydoc find_widget(std::string_view)
  const ui_widget_data *find_widget(std::string_view widget_id) const;
};

/// Data of a message or confirmation popup.
struct ui_popup_data {
  std::string id = "popup";                ///< Unique name of the popup.
  std::string title;                       ///< Title.
  std::string message;                     ///< Body, wraps by itself.
  std::vector<std::string> buttons{"OK"};  ///< Button labels, at most 4.
  i32 default_button = 0;                  ///< Button selected when it opens.
  i32 cancel_button = -1;                  ///< Button returned on back, -1 for none.
  f32 width = 0.0f;                        ///< Width (0 uses ui_style::width).
  bool open = false;           ///< Whether the popup is open.
};

/// A whole UI layout, loaded from a JSON file or exported by the editor.
struct ui_layout {
  i32 version = 1;                         ///< Version of the UI data structure.
  vec2 design_resolution{1280.0f, 720.0f}; ///< The resolution it was designed for.
  ui_style style = ui_default_style();     ///< Default look of the UI.
  bool custom_style = false;               ///< Whether the layout applies its own style.
  std::vector<ui_panel_data> panels;       ///< The panels.
  std::vector<ui_popup_data> popups;       ///< The popups.

  /// Finds a panel by ID.
  /// @param panel_id ID of the panel.
  /// @return Pointer to the panel, or `nullptr` if there is none.
  ui_panel_data *find_panel(std::string_view panel_id);
  /// @copydoc find_panel(std::string_view)
  const ui_panel_data *find_panel(std::string_view panel_id) const;

  /// Finds a widget by ID across all panels.
  /// @param widget_id ID of the widget.
  /// @return Pointer to the first widget with that ID, or `nullptr` if there is none.
  ui_widget_data *find_widget(std::string_view widget_id);
  /// @copydoc find_widget(std::string_view)
  const ui_widget_data *find_widget(std::string_view widget_id) const;

  /// Finds a popup by ID.
  /// @param popup_id ID of the popup.
  /// @return Pointer to the popup, or `nullptr` if there is none.
  ui_popup_data *find_popup(std::string_view popup_id);
  /// @copydoc find_popup(std::string_view)
  const ui_popup_data *find_popup(std::string_view popup_id) const;
};

/// A UI event emitted when the player interacts with a widget.
struct ui_layout_event {
  /// Kind of event.
  enum kind_t {
    none,
    button_clicked,   ///< A button was pressed.
    value_changed,    ///< A value changed (toggle, slider, choice).
    popup_dismissed,  ///< A popup closed (a button was picked or Back was pressed).
  } kind = none; ///< Kind of event.

  const char *panel_id = "";  ///< ID of the panel that holds the widget. Empty for popup_dismissed.
  /// ID of the widget, or of the popup for popup_dismissed. It is a `const char *`: compare
  /// with `std::string_view`, not with `==` (which only compares pointers).
  const char *widget_id = "";
  bool bool_val = false;      ///< New value of a toggle.
  f32 float_val = 0.0f;       ///< New value of a slider.
  i32 int_val = 0;            ///< New choice of a choice widget, or the button pressed on a popup.
};

/// Type of the callback for UI events.
using ui_event_callback = std::function<void(const ui_layout_event &)>;

/// njin's standard pixel-art style (flat square corners, compact 16px sizes).
/// @return The pixel style.
ui_style ui_pixel_style();

/// Loads a UI layout from a JSON file.
/// @param ctx Engine context.
/// @param path Path of the JSON file.
/// @param out Receives the layout that was read.
/// @return `true` if it loaded.
bool ui_layout_load(context &ctx, const char *path, ui_layout &out);

/// Parses JSON data into a ui_layout.
/// @param json The root json_value.
/// @param out Receives the layout that was read.
/// @return `true` if it is valid.
bool ui_layout_parse(const json_value &json, ui_layout &out);

/// Converts a ui_layout to a json_value, to save it to a file.
/// @param layout The UI layout.
/// @return The JSON object.
json_value ui_layout_to_json(const ui_layout &layout);

/// Writes a UI layout to a JSON file.
/// @param path Path to save to.
/// @param layout The UI layout.
/// @param pretty Break lines and indent so it is easy to read.
/// @return `true` if it saved.
bool ui_layout_save(const char *path, const ui_layout &layout, bool pretty = true);

/// Draws one panel of the layout.
/// Call in phase_post_render. When `layout.custom_style` is on it calls ui_style_set() with
/// the layout's style and does not put the previous style back.
/// @param ctx Engine context.
/// @param layout The layout that holds the panel.
/// @param panel_id ID of the panel to draw.
/// @param on_event Optional callback for widget interactions.
/// @return `true` if anything was interacted with this frame.
bool ui_draw_panel(context &ctx, ui_layout &layout, const char *panel_id,
                   const ui_event_callback &on_event = nullptr);

/// Draws every panel with `visible == true` and every popup that is `open`.
/// Call in phase_post_render. The same note about style as ui_draw_panel() applies.
/// @param ctx Engine context.
/// @param layout The layout to draw.
/// @param on_event Optional callback for widget interactions.
void ui_draw_layout(context &ctx, ui_layout &layout,
                    const ui_event_callback &on_event = nullptr);

// --- Helpers to read and write widget state ---

/// Checks whether a button was pressed in the last draw.
/// @param layout The layout that holds the widget.
/// @param widget_id ID of the button.
/// @return `true` if the button was pressed in the last draw; `false` if not, or if there is no such widget.
bool ui_layout_is_clicked(const ui_layout &layout, const char *widget_id);

/// Reads the bool value of a toggle.
/// @param layout The layout that holds the widget.
/// @param widget_id ID of the toggle.
/// @param fallback Value returned when there is no such widget.
/// @return The current value of the toggle, or `fallback`.
bool ui_layout_get_bool(const ui_layout &layout, const char *widget_id, bool fallback = false);

/// Sets the bool value of a toggle. Does nothing if there is no such widget.
/// @param layout The layout that holds the widget.
/// @param widget_id ID of the toggle.
/// @param value The new value.
void ui_layout_set_bool(ui_layout &layout, const char *widget_id, bool value);

/// Reads the float value of a slider or progress bar.
/// @param layout The layout that holds the widget.
/// @param widget_id ID of the slider or progress bar.
/// @param fallback Value returned when there is no such widget.
/// @return The current value, or `fallback`.
f32 ui_layout_get_float(const ui_layout &layout, const char *widget_id, f32 fallback = 0.0f);

/// Sets the float value of a slider or progress bar. Does nothing if there is no such widget.
/// @param layout The layout that holds the widget.
/// @param widget_id ID of the slider or progress bar.
/// @param value The new value.
void ui_layout_set_float(ui_layout &layout, const char *widget_id, f32 value);

/// Reads the index of a choice widget.
/// @param layout The layout that holds the widget.
/// @param widget_id ID of the choice widget.
/// @param fallback Value returned when there is no such widget.
/// @return The selected index, or `fallback`.
i32 ui_layout_get_int(const ui_layout &layout, const char *widget_id, i32 fallback = 0);

/// Sets the index of a choice widget. Does nothing if there is no such widget.
/// @param layout The layout that holds the widget.
/// @param widget_id ID of the choice widget.
/// @param value The new index.
void ui_layout_set_int(ui_layout &layout, const char *widget_id, i32 value);

/// Reads the label or text of a widget.
/// @param layout The layout that holds the widget.
/// @param widget_id ID of the widget.
/// @param fallback String returned when there is no such widget.
/// @return The label of the widget, or `fallback`. The pointer is invalid once the label changes.
const char *ui_layout_get_text(const ui_layout &layout, const char *widget_id, const char *fallback = "");

/// Sets the label or text of a widget. Does nothing if there is no such widget.
/// @param layout The layout that holds the widget.
/// @param widget_id ID of the widget.
/// @param text The new label.
void ui_layout_set_text(ui_layout &layout, const char *widget_id, const char *text);

/// Generates C++ source that calls njin::ui_* from a ui_layout.
/// @param layout The layout to generate code for.
/// @param func_name Name of the C++ function to create.
/// @return The C++ source as a string.
std::string ui_layout_generate_cpp(const ui_layout &layout, const char *func_name = "draw_ui");

/// Generates C++ source for one panel.
/// @param panel The panel to generate code for.
/// @param func_name Name of the C++ function to create.
/// @return The C++ source as a string.
std::string ui_panel_generate_cpp(const ui_panel_data &panel, const char *func_name = "draw_panel");

/// @}
} // namespace njin
