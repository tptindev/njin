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
| njin::ui_button() | A button, can be disabled (`enabled = false`) | It is pressed |
| njin::ui_toggle() | An on/off switch | The value changes |
| njin::ui_slider() | A slider, shows a number or a percentage | The value changes |
| njin::ui_choice() | Pick one of several items with left/right | The choice changes |
| njin::ui_progress() | A progress bar: health, loading time | |
| njin::ui_progress_circle() | A progress ring: cooldowns, reloads. Thickness, colours, start angle, direction, round caps and centre text are adjustable | |
| njin::ui_label(), njin::ui_image(), njin::ui_space() | Text, image, empty space | |
| njin::ui_last_rect() | Frame of the widget just placed, to draw more on it (after njin::ui_end()) | |
| njin::ui_row() | Lays out the next few widgets as a horizontal row | |
| njin::ui_back() | | The player pressed back |

The label is the widget's identifier within the panel. If two widgets have the same text, add a hidden suffix after `##`:
`"Delete##1"`, `"Delete##2"` (the part after `##` is not drawn).

A panel is positioned by `anchor` (a point on the screen, as a ratio) and `pivot` (a point on the panel), so
`{.anchor = {1, 0}, .pivot = {1, 0}}` is the top-right corner. The height is computed from the content.

A HUD that stays up during play (a resource bar, a row of recruit buttons) sets `.navigable = false`:
its buttons are clicked with the mouse only, are never preselected, and the arrows, Enter, Space and Esc
stay the game's. To draw more on a widget (a picture in a button, a minimap in a njin::ui_space() gap),
get its frame with njin::ui_last_rect() and draw after njin::ui_end().

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

@note Esc closes the window by default (`njin_cfg::exit_key`). If your menu uses Esc to go back, set
`.exit_key = njin::key_none` in njin_cfg and quit with a "Quit" button.

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

## Customizing the look

The whole look lives in njin::ui_style: font, text size, `scale`, spacing, and the look
(njin::ui_look) of each kind of widget: `panel`, `label`, `button`, `track`, `fill`, `knob`, `toast`. Get
the default with njin::ui_default_style(), change it, then njin::ui_style_set(). You can change the style between two
panels, for example a red warning dialog.

Each njin::ui_look has four faces (njin::ui_skin) for four states: `normal`, `focused`,
`pressed`, `disabled`, plus matching text colors. A face is drawn with:

- **a flat color**: `color`, corner rounding `roundness`, outline `outline`;
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

The UI is drawn in screen space, after post-processing, so effects like blur or CRT do not
blur the menu (see @ref post_processing; njin::post::paused() suits a pause menu).
