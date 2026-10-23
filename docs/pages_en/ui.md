# UI: menus, buttons, sliders {#ui}

njin's UI is *immediate mode*: you do not create button objects, you just call functions every frame in
`phase_post_render`. Any button you call is shown, and the function returns `true` in the frame it is pressed.

@include ui_menu.cpp

A menu with many widgets, or one that changes often, can be built by drag and drop in njin_ui_editor and the file loaded into the game: see @ref ui_editor.

@image html platformer_title.png "The main menu of the sample game Mầm Leo Núi, built with njin::ui_button(). The selected button has a blue outline; change buttons with the keyboard, mouse or gamepad"

## Widgets

| Function | What it is | Returns `true` when |
|---|---|---|
| njin::ui_begin(), njin::ui_end() | A panel: background, title, stacks widgets from top to bottom | |
| njin::ui_button() | A button, can be disabled (`enabled = false`); `selected = true` keeps the "pressed" face for a tool that is switched on. A label wider than the button is shrunk to fit | It is pressed |
| njin::ui_toggle() | An on/off switch | The value changes |
| njin::ui_slider() | A slider, shows a number or a percentage | The value changes |
| njin::ui_choice() | Pick one of several items with left/right | The choice changes |
| njin::ui_tabs() | A tab bar: the pages of a panel, exactly one tab open (see @ref ui_tab_bar "Tab bar") | The open tab changes |
| njin::ui_progress() | A progress bar: health, loading time. The `fill` parameter changes the colour of the filled part (red health when low) | |
| njin::ui_progress_circle() | A progress ring: cooldowns, reloads. Thickness, colours, start angle, direction, round caps and centre text are adjustable | |
| njin::ui_label(), njin::ui_image(), njin::ui_space() | Text (wraps on its own when wider than its space), image, empty space | |
| njin::ui_text() | Text with its own colour, size and alignment (njin::ui_text_desc): a money amount in green or red, a value right-aligned | |
| njin::ui_tooltip() | A small caption next to the cursor, drawn over every panel; call it every frame you want it shown | |
| njin::ui_scroll_begin(), njin::ui_scroll_end() | A scroll area of fixed height inside a panel: long lists | |
| njin::ui_scroll_begin_fit() | A scroll area just tall enough for what it holds, up to one height: a panel no taller than the room it has | |
| njin::ui_scroll_to_end() | Scrolls an area to the end once: a chat panel opens at the newest message | |
| njin::ui_last_rect() | Frame of the widget just placed, to draw more on it (after njin::ui_end()) | |
| njin::ui_row() | Lays out the next few widgets as a horizontal row | |
| njin::ui_input() | A one-line text field: search, naming | The text changes |
| njin::ui_back() | | The player pressed back |

The label is the widget's identifier within the panel. If two widgets have the same text, add a hidden suffix after `##`:
`"Delete##1"`, `"Delete##2"` (the part after `##` is not drawn).

A panel is positioned by `anchor` (a point on the screen, as a ratio) and `pivot` (a point on the panel), so
`{.anchor = {1, 0}, .pivot = {1, 0}}` is the top-right corner. The height is computed from the content.

A HUD that stays up during play (a resource bar, a row of recruit buttons) sets `.navigable = false`:
its buttons are clicked with the mouse only, are never preselected, and the arrows, Enter, Space and Esc
stay the game's. To draw more on a widget (a picture in a button, a minimap in a njin::ui_space() gap),
get its frame with njin::ui_last_rect() and draw after njin::ui_end().

A panel can close itself: point `.open` at a `bool` of the game's and the top-right corner gets a close
button; clicking it (mouse only) sets that variable to `false`. The panel is still drawn for the rest of
that frame; the game stops calling it from the next frame on.

```cpp
static bool show_shop = true;
if (show_shop) {
  njin::ui_begin(ctx, {.id = "shop", .title = "Shop", .open = &show_shop});
  njin::ui_row(ctx, 2);
  njin::ui_label(ctx, "Gold");
  njin::ui_text(ctx, "1500k", {.color = njin::rgb8(120, 200, 100), .align = 1.0f});
  njin::ui_scroll_begin(ctx, "items", 240.0f);
  for (const item &it : items)
    if (njin::ui_button(ctx, it.name.c_str(), true, it.id == picked))
      picked = it.id;
  njin::ui_scroll_end(ctx);
  njin::ui_end(ctx);
}
```

A scroll area takes the mouse wheel when the cursor is over it (the game does not see that scroll),
remembers how far it has scrolled by `id`, and scrolls on its own to the widget selected by keyboard or
gamepad. Setting `ui_style::scrollbar_width` above 0 gives the scroll area a scrollbar on its right edge
(the thumb `ui_style::scrollbar`, the track `ui_style::scrollbar_rail`), shown when it holds more than its
height: drag the thumb to scroll, click the track to jump there; widgets in the area leave room for the
bar. The default is 0: no bar, as before. A tooltip can be called from anywhere,
with no ui_begin needed: usually right after a widget, when njin::point_in_rect(njin::mouse_pos(ctx),
njin::ui_last_rect(ctx)).

Text from njin::ui_label() and njin::ui_text() that is wider than its space (the whole line, or one
column of njin::ui_row()) wraps on its own at spaces, the lines stacking under each other; a row is as
tall as its tallest widget, so a two-line text column does not overlap the row after it.

### Tab bar {#ui_tab_bar}

A panel with several pages (overview, statement, savings) uses njin::ui_tabs() instead of a row of
njin::ui_button(): the tabs split one line evenly, clicking a tab opens it, and the open tab's face
slides over to the new one. When the bar is selected by keyboard or gamepad, left/right move to the
next tab. Text longer than the tab is shrunk to fit.

```cpp
static njin::i32 page = 0;
njin::ui_tabs(ctx, "bank_tabs", page, {"Overview", "Statement", "Savings"});
if (page == 0)
  overview(ctx);
```

njin::ui_tabs_desc changes the height and text size, and `icon_size` leaves a square to the left of each
tab's text for the game to draw an icon into: get the rect of tab `k` with njin::ui_tabs_icon_rect() right
after njin::ui_tabs(). The look lives in njin::ui_style: `tab` (a closed tab: normal, hovered, pressed),
`tab_open` and `tab_open_text` (the open tab), `tab_bar` (the background behind the whole bar), `tab_line`
(the line under the open tab), `tab_spacing`, `tab_slide_seconds`.

### Progress ring

njin::ui_progress_circle() takes a njin::ui_circle_desc. Every field is optional; a colour with alpha 0 comes from
the game's style (`track`, `fill`, `panel.text`), so a ring you have not tuned still fits the theme.

```cpp
njin::ui_progress_circle(ctx, {.value = cooldown, .diameter = 64.0f, .thickness = 8.0f,
                               .round_caps = true, .fill = {0.95f, 0.35f, 0.25f, 1.0f}, .percent = true});
```

| Field | Effect |
|---|---|
| `diameter`, `thickness` | Outer diameter and the width of the band. A thickness of half the diameter or more makes a solid disc |
| `start_angle`, `clockwise` | Where the ring starts (degrees, 0 is the top, 90 is 3 o'clock) and which way it fills |
| `round_caps` | Rounds both ends of the filled part |
| `show_track`, `track`, `fill` | Whether the background ring is drawn, its colour, and the colour of the filled part |
| `text`, `percent`, `text_color` | Text in the middle; or `percent` to show "75%" |

The widget takes one line as tall as `diameter` and is centred; inside njin::ui_row() it shrinks to fit the column.

### Text field

njin::ui_input() edits a `std::string` in place. Click the field to start typing; Enter, Esc or a
click outside it stops. The characters come from njin::text_char(), so they have been through the
operating system's keyboard layout and input method, and Vietnamese with diacritics can be typed
(including input methods that delete and retype a letter within one frame). While typing,
**the game sees no keys**: an "e" typed into the field does not become a shortcut. Input that does
not go through the keys (gamepad, actions) should ask njin::ui_typing() first.

@include ui_input.cpp

## Mouse, keyboard, gamepad

Every widget works with all three, with nothing extra to write:

| Action | Mouse | Keyboard | Gamepad |
|---|---|---|---|
| Select | Point at it | Up/down arrows (left/right within a row) | D-pad or left stick |
| Press | Click | Enter, Space | A (bottom face button) |
| Change slider, choice | Drag, click | Left/right | Left/right |
| Back | | Esc, Backspace | B (right face button) |

Holding a direction repeats the selection automatically. Going up from the topmost widget wraps around to the bottom one.
Disabled widgets are skipped. When a menu has just opened, the first widget is preselected for the gamepad; to select
a different button, call njin::ui_focus() when opening.

**While a panel is showing**, the UI keeps the navigation keys (arrows, Enter, Space, Esc,
Backspace) and mouse clicks on the panel: the game does not see them, so the character does not run while the
player is choosing from the menu. Check with njin::ui_active(). The UI only takes the left button; a game that
uses the right button or the wheel in the world asks njin::ui_mouse_over() first, so a click on the HUD does not fall through.

**Overlapping panels** go by draw order: a panel called later sits on top, and only the topmost panel
under the cursor receives the mouse; buttons on a panel covered below do not highlight or respond to
clicks. A game that wants a window to come to the front calls it last, for example when
njin::ui_pressed_on() reports the player just clicked on it. A game that reads the mouse around a panel
itself should ask njin::ui_on_top() (outside the panel) or njin::ui_covered() (inside the panel) so it
also does not react through a panel that covers it.

@note Esc closes the window by default (`config::exit_key`). If your menu uses Esc to go back, set
`.exit_key = njin::key_none` in config and quit with a "Quit" button.

## Popups

njin::ui_popup() is a confirmation dialog: a dark background, a title, content that wraps automatically and up to 4 buttons.
Call it every frame while the popup is open; the `open` variable is kept by the game and the popup sets it to `false` itself when it closes.

@include ui_popup_toast.cpp

The function returns the index of the button that was just pressed (in `buttons` order), or -1 if nothing has happened yet. Back (Esc, Backspace,
the B button) closes the popup and returns `cancel_button`, or -1 if it is not set.

A popup is **modal**:

- Other panels are still drawn, but they receive no mouse, keyboard or gamepad input until the popup closes.
  The menu behind does not need to be hidden, and you do not need an `if/else` between screens.
- njin::ui_back() only reports to the popup, so the same Esc press does not both close the popup and go back in the menu.
- When it opens, the `default_button` is preselected: make it the **safe** button ("Stay", "Cancel") so that pressing Enter
  by mistake loses no data. When it closes, the selection returns to the same widget of the menu behind.
- The frame in which the popup has just opened ignores the back button. This way a menu that opens a popup with Esc (`if (ui_back(ctx))
  open = true;`) does not get the popup closed immediately by that very Esc press.
- The press that has just closed the popup does not fall through to the widget behind.

If you need arbitrary content (sliders, toggles...), use njin::ui_popup_begin() and njin::ui_popup_end()
around ordinary widgets, see `settings_popup` in the example. Only the `id`, `title` and `width` of
njin::ui_popup_desc are used; close the popup yourself by no longer calling it.

The overlay color that darkens the background is `ui_style::dim`. The popup's panel shares the `ui_style::panel` look.

## Toasts

njin::ui_toast() shows a small notification in a corner of the screen and then makes it disappear on its own: "Game saved", "Picked up 5
gold", "Connection lost".

```cpp
njin::ui_toast(ctx, "Game saved", {.kind = njin::ui_toast_success});
njin::ui_toast(ctx, "Inventory is full", {.kind = njin::ui_toast_warning, .seconds = 4.0f});
```

Call it from **anywhere**, in any phase (even inside `phase_update` or an event handler).
No ui_begin needed: the engine queues, slides in, fades out and draws over everything, popups included, except the scene
transition effect. Long text wraps automatically according to `ui_style::toast_width`.

| Property | Details |
|---|---|
| **Does not swallow keys** | Unlike UI panels: toasts are for reading only, the game underneath still receives arrows, Enter, Esc and the mouse |
| Real time | Counted by njin::delta_real(): it keeps running and expires on its own while the game is paused or in hitstop |
| Kind | njin::ui_toast_info, `_success`, `_warning`, `_error` change the color of the bar on the left (`ui_style::toast_accent`) |
| Count | At most `ui_style::toast_max` (default 5); the oldest is dropped when full |
| Position | `ui_style::toast_anchor`: `{1, 1}` is the bottom-right corner (default), `{0.5, 0}` is the middle of the top edge. The newest toast sits closest to the corner, older ones stack inward |
| Look | `ui_style::toast`: color, rounded corners, 9-slice image and shader like every other widget |

njin::ui_toast_clear() removes all toasts currently showing, for example when changing scenes.

@note If you build a toast with ui_begin, that panel will swallow the game's navigation keys every time it shows, and
the character will stop dead. Use njin::ui_toast().

## Motion

The UI draws instantly, so by default there is no motion: a panel appears and disappears right away, a
button's face changes right away. The fields below turn motion on, counted by real time; leave them at
default and the UI behaves as before.

| Field | Detail |
|---|---|
| njin::ui_panel_desc::enter_seconds, njin::ui_panel_desc::enter_from | When a panel starts being drawn (it was not there the previous frame), it slides in from `enter_from` pixels away to its place and fades in, over `enter_seconds` seconds |
| njin::ui_panel_desc::leaving | A panel that is leaving, 0..1. The game counts it itself and keeps calling the panel a while longer; the panel fades out, slides toward `enter_from` and stops taking the mouse |
| `ui_style::hover_seconds` | ui_button() eases into its selected face when the mouse enters, and back out when it leaves |
| `ui_style::press_scale` | ui_button() held down draws shrunk around its centre, for example `0.96` |
| njin::ui_panel_desc::inert | A panel that is only shown, taking no mouse input: for when the game draws it distorted itself |

```cpp
// A toolbar panel rising from the taskbar, buttons warming up as the mouse passes over them.
ui_style s = njin::ui_style_get(ctx);
s.hover_seconds = 0.12f;
s.press_scale = 0.96f;
njin::ui_style_set(ctx, s);
njin::ui_begin(ctx, {.id = "tools", .anchor = {0.5f, 1.0f}, .pivot = {0.5f, 1.0f},
                     .enter_seconds = 0.18f, .enter_from = {0.0f, 28.0f}});
```

A self-drawn effect (like macOS's "genie" window blowing out of an icon): draw the panel into a render
texture between njin::render_texture_begin() and njin::render_texture_end() with `.inert = true`, get its
real frame with njin::ui_panel_rect(), then draw that render texture back warped using strips of
njin::draw_triangle_textured().

What the game draws over a panel itself (an icon, its own text) follows the panel's slide because it takes
its position from njin::ui_last_rect(); for it to fade along when the panel leaves, wrap the call in
njin::draw_alpha_push() and njin::draw_alpha_pop().

## Customizing the look

The whole look lives in njin::ui_style: font, text size, `scale`, spacing, and the look
(njin::ui_look) of each kind of widget: `panel`, `label`, `button`, `track`, `fill`, `knob`, `toast`. Get
the default with njin::ui_default_style(), change it, then njin::ui_style_set(). You can change the style between two
panels, for example a red warning dialog.

Each njin::ui_look has four faces (njin::ui_skin) for four states: `normal`, `focused`,
`pressed`, `disabled`, plus matching text colors. A face is drawn with:

- **a flat color**: `color`, corner rounding `radius` (pixels, the same corner at every size) or `roundness` (relative to the short side), outline `outline`;
- **an image**: `texture` (and `source` if you use a region of an atlas), multiplied by the `color`;
- **a 9-slice image**: add `border`: the four corners stay as they are, the edges and the middle stretch, so one
  small frame drawing works for buttons of any size.

```cpp
njin::ui_style s = njin::ui_default_style();
const njin::texture_handle frame = njin::texture_load(ctx, "assets/ui/frame.png");
s.button.normal = {.texture = frame, .border = 6};
s.button.focused = {.texture = frame, .border = 6, .color = {1.0f, 0.9f, 0.6f, 1.0f}};
s.button.shader = njin::shader_load(ctx, nullptr, "assets/ui/glow.fs");
njin::ui_style_set(ctx, s);
```

**Shader.** `ui_look::shader` is activated when drawing the widget's faces. The engine sets the following uniforms
by itself if the shader declares them (if it does not, they are skipped, with no warning):

| Uniform | Type | Value |
|---|---|---|
| `uiState` | float | 0 normal, 1 selected, 2 pressed, 3 disabled |
| `uiTime` | float | Seconds since startup, for animated effects |
| `uiRect` | vec4 | x, y, width, height of the widget, in screen pixels |
| `uiValue` | float | Progress 0..1 of a slider or progress bar |

An example of a button that glows and pulses when selected:

```glsl
#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float uiState;
uniform float uiTime;
out vec4 finalColor;
void main() {
  vec4 c = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
  float glow = uiState >= 1.0 ? 0.35 + 0.1 * sin(uiTime * 6.0) : 0.0;
  finalColor = vec4(c.rgb + vec3(glow, glow * 0.6, 0.0), c.a);
}
```

**Sound.** `sound_move`, `sound_accept`, `sound_back` in the style play when the selection moves,
when a value is pressed or changed, and when going back.

**Font.** The engine's default font (JetBrains Mono, embedded) already has Vietnamese text. For a
different font, load it with njin::font_load() and put it in `ui_style::font` (see @ref drawing).

## Scaling with the window {#ui_canvas}

The UI is measured in window pixels: a panel 640 pixels tall fits a 1600 x 900 window but overflows a
1280 x 720 one, and looks tiny on a 4K screen. njin::ui_canvas_set() gives the UI a drawing frame (a
canvas) with a fixed design height: the scale is the window height divided by the design height, clamped
to `min_scale`..`max_scale`; `min_width` reduces the scale further when the window is too narrow, so the
canvas is always at least that wide.

```cpp
// At startup: the HUD is designed for 1600 x 900, never narrower than 1280.
njin::ui_canvas_set(ctx, {.height = 900.0f, .min_width = 1280.0f});

// Every frame, in phase_post_render:
njin::ui_canvas_begin(ctx);
draw_hud(ctx);
njin::ui_canvas_end(ctx);
```

Between njin::ui_canvas_begin() and njin::ui_canvas_end():

- njin::screen_size() returns the canvas size (as tall as `height`, as wide as the window's aspect ratio
  allows), so panels anchored to the screen land in the right place on their own;
- njin::mouse_pos() and njin::mouse_delta() are in canvas units;
- every shape, image, panel and widget is scaled accordingly; text is built at its on-screen size so it
  stays crisp; njin::clip_begin() takes a region in canvas units;
- drawing into a render texture is not scaled (coordinates still follow canvas units).

While the canvas is on, every UI panel must be built inside the canvas: the UI reads the mouse in canvas
units right at the start of the frame. Toasts also draw inside the canvas. Things tied to the world (a
label on the map, picking an object with the mouse in `phase_update`) still use window pixels: multiply or
divide by njin::ui_canvas_scale() when converting between the two. The canvas does not combine with a
virtual resolution: when njin::window_set_virtual_size() is set, the scale is always 1.

The UI is drawn in screen space, after post-processing, so effects like blur or CRT do not
blur the menu (see @ref post_processing; njin::post::paused() suits a pause menu).
