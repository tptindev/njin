# Window, files and saving the game {#window_files}

@include save_window.cpp

## Configuration at creation

Besides the title, size, FPS and background color, njin::config has:

| Field | Default | Meaning |
|---|---|---|
| `fixed_hz` | 60 | Number of `phase_fixed_update` ticks per second |
| `exit_key` | `key_escape` | The key that closes the game immediately. Set `key_none` when you need Esc for a menu |
| `resizable` | `false` | Allows dragging to resize the window |
| `app_name` | the title | Name of the save-game folder |
| `vsync` | `false` | Vertical sync: each frame waits for the display to refresh, no tearing, and the CPU and GPU rest between frames. `target_fps` is still an additional cap |

## Window

| Function | What it does |
|---|---|
| njin::screen_size() | The window's **current** size |
| njin::window_resized() | The window was resized this frame |
| njin::window_set_fullscreen() / njin::window_fullscreen() | Fullscreen as a borderless window |
| njin::window_set_vsync() / njin::window_vsync() | Turns vertical sync on or off while running; saved in the settings |
| njin::window_set_size(), njin::window_set_title() | Change the size, the title |
| njin::cursor_set_visible() | Show or hide the mouse cursor |
| njin::cursor_set_locked() | Lock the cursor inside the window; read njin::mouse_delta() |
| njin::quit() | Quit at the end of the frame. `phase_shutdown` still runs |

**Keep the game view fitting every window size.** Design the game for one fixed size,
then every frame set the camera's zoom to fit the window:

```cpp
const njin::vec2 screen = njin::screen_size(ctx);
cam.zoom = std::fmin(screen.x / 960.0f, screen.y / 540.0f);
cam.offset = screen * 0.5f;
```

## Screenshots

njin::screenshot() saves an image of the current frame to a file:

```cpp
if (njin::key_pressed(ctx, njin::key_f12))
  njin::screenshot(ctx);                      // auto-named, in the save-game folder
njin::screenshot(ctx, "captures/level1.png"); // or specify a path
```

- The image is captured at the **end of the frame**, after `phase_post_render`, so it contains both the world and the UI no matter
  which phase you call it from.
- If you do not pass a path, the image goes into `screenshots/` in the save-game folder (see
  njin::save_path()), named by date and time, never colliding.
- The format follows the file extension: `.png`, `.bmp`, `.tga`, `.qoi`. Any other extension is not saved and a
  warning is written to the log.

@note By default raylib takes a screenshot on its own when you press **F12** in every game, saving it into the working
folder. njin **turns off** that hidden behavior (the `SUPPORT_SCREEN_CAPTURE=0` flag in `CMakeLists.txt`),
so F12 is an ordinary key and the game decides what to use it for.

## Resource paths

Every loading function (texture, shader, font, sound, music) looks for the file in this order:

1. exactly the path given, resolved from the working directory,
2. if not found and it is a relative path: resolved from the **folder containing the exe file**.

Thanks to step 2, a game run by double-clicking the exe (a different working directory) still finds the
`assets/` placed next to the exe.

### Putting assets next to the exe

Put the game's resources in a folder next to the game's `CMakeLists.txt`, then call
`njin_add_assets()` (declared in `cmake/njin.cmake`):

```cmake
add_executable(my_game main.cpp)
target_link_libraries(my_game PRIVATE njin::rt njin_warnings)
njin_add_assets(my_game assets)   # src/games/my_game/assets -> build/bin/assets
```

Every time you build `my_game`, the folder is copied next to the exe, only the files that changed, so editing one
image and rebuilding is enough. In code, always use relative paths like
`"assets/player.png"`. The whole `build/bin` folder can then run anywhere, and can be sent to
other people.

Files deleted from the source folder are **not** deleted from the copy; delete `build/bin/assets` to clean
up. With several games in the same `build/bin`, give the folders different names (for example
`njin_add_assets(pong pong_assets)`) so they do not overwrite each other.

## Save-game format

njin::save_path() tells you *where* to save. To save many structured values (levels, inventory,
settings), use JSON: njin::json_save() and njin::json_load(), see @ref json.

## Files

| Function | What it does |
|---|---|
| njin::file_exists() | Whether the file exists |
| njin::file_read() | Reads the whole file into a string |
| njin::file_write() | Writes the whole file, creating parent folders if needed |
| njin::save_path() | The save-game file path in the user's folder |

njin::file_write() writes to a temporary file and then renames it, so a game that shuts down midway does not
corrupt the old file.

njin::save_path() returns a path in the user's own folder, creating the folder if
it does not exist:

| Operating system | Folder |
|---|---|
| Windows | `%APPDATA%\<app_name>` |
| macOS | `~/Library/Application Support/<app_name>` |
| Linux | `~/.local/share/<app_name>` |

All paths are UTF-8, so file and folder names with Vietnamese diacritics still work correctly.
