# UI editor: build menus by drag and drop {#ui_editor}

`njin_ui_editor` builds UI the way Godot builds a Control scene: drag nodes into a tree, drag to
arrange them, see the result at once. It saves a `.ui.json` file; the game loads it with
njin::ui_layout_load() and draws it with njin::ui_draw_layout(), with no njin::ui_button() calls to type.

@image html ui_editor.png "njin_ui_editor: the Scene tree and the Nodes palette on the left, the Viewport in the middle, the Inspector on the right. The Viewport is drawn by the engine's own UI code"

The editor pays off for menus with many widgets or that change often. A three-button menu is still
quicker to write directly with @ref ui.

## Running the editor

```
cmake --build build --target njin_ui_editor --parallel
build\bin\njin_ui_editor.exe [path_to_file.ui.json]
```

The editor is not part of a shipped game: it is a tool for the person making the game, like `njin_inspector`.

## The windows

The windows dock: drag a tab to move, split or stack it. **Cửa sổ > Bố cục mặc định** (Window >
Default layout) puts them back. The arrangement is remembered in `njin_ui_editor.ini`.

| Window | What it does |
|---|---|
| Scene | The `Layout > Panel > Widget` tree and the Popups |
| Nodes | The node types to drag out |
| Viewport | See the layout as it looks in the game; select, drag, drop |
| Inspector | Properties of the selected node |
| Theme | The layout's own style |
| C++, JSON, Output | Sample code, the file contents, events while playing |

### Scene

Each row is a node. Drag a row onto another: dropped on the upper half it goes before, on the lower
half after, dropped on a panel it becomes that panel's last widget. Widgets can move to another
panel. Right-click to add a child node, change a widget's kind, duplicate, reorder or delete. The
tick box on the right is a panel's `visible` or a popup's `open`. Widgets after a `Row` are indented:
they are the columns of that row.

### Nodes

Panel, Popup, Row, Space and the controls (Label, Button, Toggle, Slider, Choice, Progress, Circle,
Image, Keybind). Drag into the Viewport or the Scene to create one, or double-click to add it to the
selected node.

### Viewport

The image in the Viewport is not a mock-up: the editor runs the very code of njin::ui_begin(),
njin::ui_button()... and draws it with raylib into a texture, so fonts, colours, rounding and sizes
match the game.

| Action | Result |
|---|---|
| Click | Select a panel or widget |
| Drag a panel | Changes `offset`. Snaps to the grid; hold `Alt` to turn snapping off for now |
| Drag the left or right edge of a panel | Changes `width` |
| Drag a widget | Reorders it, or moves it to another panel. The blue line is where it will land |
| Drop a node from Nodes onto a panel | Inserts a widget at the blue line |
| Drop a node from Nodes onto empty space | Creates a new panel there |
| Right-click | Menu to add nodes, duplicate, delete |
| Middle mouse or right-drag | Pan the view |
| Scroll | Zoom |
| Arrow keys (panel selected) | Nudge 1 px, hold `Shift` for 10 px |

The green dot is the `anchor` on the screen, the blue dot is the panel's `pivot`.

### Inspector and Theme

The Inspector has a 3x3 grid to pick an `anchor` and `pivot` pair at once (like Godot's anchor
presets), then `offset` for fine tuning. Open "Neo & tâm riêng" (Own anchor & pivot) when the two
should differ.

Theme decides the file's style:

- **Off** `custom_style` (the default): the file stores no style and the game calls njin::ui_style_set()
  itself. The Viewport previews with the Default or the Pixel style, chosen on the toolbar.
- **On**: the file stores font size, spacing, rounding and the colours, and applies them when drawing.

## Play

`F5` runs a *copy* of the layout: press buttons, drag sliders, flip toggles in the Viewport; arrows,
Enter and Space work too while the Viewport is focused. Each event shows in Output, exactly as the
game will receive it. Stop, and the layout is as it was before playing.

## Shortcuts

| Key | Action |
|---|---|
| `Ctrl+N`, `Ctrl+O`, `Ctrl+S`, `Ctrl+Shift+S` | New, open, save, save as |
| `Ctrl+Z`, `Ctrl+Y` | Undo, redo. One drag or one text edit is one step |
| `F5` | Play, stop |
| `Del`, `Ctrl+D` | Delete, duplicate a node (while Scene or Viewport is focused) |
| `Ctrl+A`, `Ctrl+Up`, `Ctrl+Down` | Add a child node, reorder (in Scene) |
| `F` | Fit the view (in Viewport) |

## Using the file in a game

@include ui_layout.cpp

njin::ui_layout_load() reads the file into a njin::ui_layout. njin::ui_draw_layout(), called in
`phase_post_render`, draws every panel with `visible` and every popup with `open`;
njin::ui_draw_panel() draws one panel by `id`. The callback receives a njin::ui_layout_event:
`button_clicked` when a button is pressed, `value_changed` when a toggle, slider or choice changes,
`popup_dismissed` when a popup closes.

Without a callback, read any state at any time by the widget's `id`: njin::ui_layout_is_clicked(),
njin::ui_layout_get_bool(), njin::ui_layout_get_float(), njin::ui_layout_get_int(),
njin::ui_layout_get_text() and the matching `set` functions. Show or hide a menu by setting a
panel's `visible` (njin::ui_layout::find_panel()) or a popup's `open` (njin::ui_layout::find_popup()).

The editor also generates C++ that calls njin::ui_begin(), njin::ui_button()... directly (the **C++**
tab), for when you would rather not keep a JSON file. The same is available in the game through
njin::ui_layout_generate_cpp() and njin::ui_panel_generate_cpp().

### Things to know

- `ev.widget_id` is a `const char *`. `ev.widget_id == "btn_play"` compares two pointers and is always
  false: use `std::string_view` as in the example above.
- A layout with `custom_style` calls njin::ui_style_set() when drawing and does **not** put the old
  style back. A game that mixes a layout's own style with its own should set its style again after
  drawing.
- An `image` widget only stores a path; njin::ui_layout_load() does not load images. The game has to
  assign that widget's `texture` itself (njin::ui_layout::find_widget()) for the image to show.
- A `circle` widget (njin::ui_progress_circle()) has its diameter, thickness, start angle, direction, round caps,
  colours and centre text editable in the Inspector. Untick "own colour" and the ring takes its colour from the game's style.
- A `keybind` widget is drawn as a line of text; the game does the key binding itself with
  njin::ui_keybind().
- A panel's height comes from its content and is not stored in the file.
- The tree and the layout hold only panels and popups. There is no nesting except `Row`.

## File format

```json
{
  "version": 1,
  "design_resolution": [1280.0, 720.0],
  "panels": [
    {
      "id": "main_menu", "title": "Main menu",
      "anchor": [0.5, 0.5], "pivot": [0.5, 0.5], "offset": [0.0, 0.0],
      "width": 340.0, "visible": true,
      "widgets": [
        { "type": "label", "id": "lbl_1", "text": "Welcome!" },
        { "type": "button", "id": "btn_play", "label": "Play" },
        { "type": "slider", "id": "sld_volume", "label": "Volume",
          "value": 0.8, "min": 0.0, "max": 1.0, "step": 0.05, "percent": true },
        { "type": "row", "columns": 2 },
        { "type": "toggle", "id": "tog_a", "label": "A", "value": false },
        { "type": "toggle", "id": "tog_b", "label": "B", "value": true }
      ]
    }
  ],
  "popups": [
    { "id": "quit", "title": "Quit?", "message": "Unsaved progress is lost.",
      "buttons": ["Stay", "Quit"], "cancel_button": 0 }
  ]
}
```

Widget types: `label`, `space`, `button`, `toggle`, `slider`, `choice`, `progress`, `circle`, `image`,
`row`, `keybind`. A field missing from the file takes its default. Add a `"style"` block when `custom_style`
is on.

`design_resolution` is only the frame the editor previews in: the game draws at the real screen
size, and `anchor` with `offset` decide where the panel sits on it.
