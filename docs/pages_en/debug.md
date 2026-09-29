# Debug tool: njin_inspector {#debug}

**njin_inspector** is a separate program that runs next to the game. It connects to the game through
`127.0.0.1` and shows everything you need to hunt down bugs in its own window. **The game draws nothing extra**:
if you want to look, look at the inspector window, otherwise just play as normal.

| Panel | What it has |
|---|---|
| **Performance** | FPS, a time chart of the last 600 frames (min, max), the entity count. Pause, step frame by frame, fast-forward or slow down |
| **Entities** | Every entity with its name and list of components, filtered by name, id or component |
| **World** | A minimap: collider boxes (green is a blocker, yellow is a trigger, gray is disabled), tilemap regions (red is a blocker), the game's camera frame (blue). Drag to move, scroll to zoom, click to select an entity |
| **Inspector** | The components of the selected entity, with their values. Edit position, angle and scale directly; toggle collider, trigger and sprite on and off; destroy the entity |
| **Watches** | Values the game sets with njin::debug_watch(), updated live |
| **Log** | The game's log, filtered by level and text |

@image html inspector_overview.png "njin_inspector connected to njin_debug_demo, Overview layout. Entity 27 is selected: you can see the ball, transform and collider components and their values. The Performance panel has a Screen recording section"

## Usage

1. In the game, open the debug port (usually only in debug builds):

@include debug_inspector.cpp

2. Run the game and `build\bin\njin_inspector.exe`, in either order. The inspector connects
   by itself when the game opens the port, and reconnects by itself when the game restarts.

**The log goes to the inspector while it is connected.** In that case the game's console no longer prints the log; every line,
including the lines from opening the window and loading GL, is in the inspector's **Log** panel. If there is no inspector (or it
has been closed), the game still logs to the console as usual. Lines that came out before the inspector connected are kept
(up to 2000) and sent as soon as it connects. The game opens the port itself with njin::debug_server_start(), as above;
with the port off (release builds), the log always goes to stderr.

The default port is 7779. To use a different port: `debug_server_start(*ctx, {.port = 7800})` in the game and
`njin_inspector --port 7800`.

**Window size.** Both layouts fill the window at every size: the position and size of the panels are proportions of the area below the menu bar, so when you drag
the window bigger or smaller the panels scale with it and stay in place, even the panels you have dragged elsewhere. On a screen smaller than 1700 x 960
the window opens smaller to fit. `njin_inspector --size 1280x720` opens a window of exactly that size (minimum 640 x 400). In a very small window,
any panel without enough room becomes scrollable.

The inspector remembers the position and size of the panels in `njin_inspector.ini` in the folder you run it from. `njin_inspector --layout consumption` opens the Consumption layout directly
(the default is Overview).

## Game components

The inspector lists **every** component of an entity, including the game's components, by C++
type name. Engine components (transform, collider, sprite, animator...) also show their values.
For a game component to show its values, register it with njin::debug_component(): the function takes the
component and returns a njin::json_value (see @ref json).

If it is not registered, the inspector writes "no view" under the component name.

## Gizmos: draw your own debug views {#debug_gizmo}

The `gizmo_*` functions (`njin_gizmo.h`) draw lines, arrows, boxes, spheres, axes, points and text
labels on top of the world, in both 2D and 3D (@ref graphics_3d). Callable in any phase, with no need
to be inside a draw call; `duration` keeps a trail instead of only showing for the frame it was
called in.

@code
njin::gizmo_circle(ctx, enemy_pos, aggro_radius, njin::colors::red);
njin::gizmo_arrow(ctx, player_pos, player_pos + velocity * 0.2f, njin::colors::yellow);
njin::gizmo_line3d(ctx, muzzle, hit_point, njin::colors::yellow, 1.0f); // keeps it for 1 second
@endcode

gizmos_set_visible() turns them all on or off, for a debug key or to stay silent in a release build.
While the inspector is connected, gizmos also show in its World panel, 3D gizmos included when the
game is drawing in 3D.

## Consumption: CPU, RAM, GPU {#debug_consumption}

@image html inspector_consumption.png "Consumption layout: Process (the process's CPU, RAM, GPU), Systems (time per system), Memory, Assets and Entities"

The **Consumption** button on the top bar switches to a layout of windows that measure consumption (the **Overview** button
switches back). There are two sources of numbers, and it matters to know how they differ:

- **The operating system reports on the game process** (the *Process* window): real CPU, RAM, GPU. The inspector reads
  them itself using the process id (the game only sends its `pid`), so **the game pays nothing** to be measured.
- **The game itself reports on its own data** (the other windows): time per system, size of each
  component and entity, each resource. These numbers are more detailed but only cover what the game knows about.

| Window | What it has |
|---|---|
| **Process** | CPU (% of the whole machine and % of one core), RAM (in use, private, peak), GPU (% of the engine that is busiest, 3D, copy), VRAM (dedicated and shared). Charts for the last two minutes |
| **Systems** | A bar splitting the frame by phase (the rest is waiting for vsync and presenting). A table of **each system**: average ms, last frame, peak, number of calls; filter and sort |
| **Memory** | Each kind of component: count, `sizeof`, heap it holds, total, a pie chart. The **Entities** tab: memory and GPU of **each entity**, sortable, click to select |
| **Assets** | Each texture, render target, font, shader, sound, music, with its size and whether it lives on the GPU or in RAM. Plus tilemap chunk images, post-processing buffers, the virtual screen |
| **Entities** | Two more columns, RAM and GPU. The **Inspector** window shows how much the selected entity holds |

The **Performance** window also has a **Rendering (last frame)** section: the number of sprites drawn and the number culled for being
outside the camera, the number of tilemap chunks, the number of particles (and how many on the GPU), the number of instanced calls, the number of post-processing passes, and the
**estimated number of draw calls** (see @ref render_stats).

How to read the numbers:

- **CPU %** comes from the process's CPU time. 100 % is the whole machine; "% of one core" tells you how busy a single thread is
  (a single-threaded game using a full core on a 16-core machine is only 6 % of the whole machine).
- **GPU and VRAM** are read from Windows performance counters (`GPU Engine`, `GPU Process Memory`), like
  Task Manager, so they work with cards from every vendor (Windows 10 1709 or newer). Integrated graphics use
  system memory, so dedicated VRAM is 0 and the real number is under "shared". On Linux and macOS,
  the Process window has no GPU yet (Linux has CPU and RAM); use the Assets window.
- **Components**: computed as `sizeof` + heap + 8 bytes of EnTT index. **A game component must be registered
  with njin::debug_component()** to get a size (and values); if it is not registered it shows `?`. The heap
  (vectors, strings) of engine components like tilemap, particle_emitter, level_object is estimated;
  for game components only the `sizeof` part is counted.
- **An entity only costs CPU through the systems that run on it**, so CPU is split by **system**, not
  by entity. See which system is expensive, then see which components it runs on. An entity's GPU only exists for
  tilemaps (the baked chunk images); sprites share textures, so the textures are counted in the Assets window.
- The Assets GPU estimate does not include driver memory, the swap chain and the context, so it is **smaller than** the
  VRAM number the operating system reports.

System names come from the third parameter of ecs_register() or `sys_desc::name`; if it is not set, it shows
`module/#number`. Timing **only runs while an inspector is connected** (two clock reads per system),
and stops immediately when the inspector closes.

A game to try the inspector with: `njin_debug_demo` (see @ref samples). Each of its keys deliberately costs something:
add balls (entities, collisions), `H` burns 3 ms of CPU in a system, `M` holds 64 MB of RAM, `G` creates a
32 MB render texture on the GPU. Press it and watch the numbers jump.

## Time controls

| Button | What it does |
|---|---|
| Pause / Resume | Pauses the game, like njin::time_set_paused() |
| Step frame | While paused: runs exactly **one** frame as long as one fixed step (1/60 second), then pauses again. Physics in `phase_fixed_update` advances exactly one step |
| time scale | Slow motion or fast forward, like njin::time_set_scale() |

Commands from the inspector are applied at the start of the frame, before any of the game's systems, so a frame never
runs half paused and half not.

## Screen recording {#debug_recording}

The **Performance** panel has a **Screen recording** section: press **Record** (or **F9** anywhere in the inspector, except while typing
in a text field) to record the game window into a GIF file, and press it again to stop. The game does not have to do anything besides
njin::debug_server_start().

@image html inspector_recording.png "The Screen recording section of njin_inspector, connected to njin_platformer. Left: recording (REC 0:02, 38 frames, 223 KB, 640x360). Right: stopped, showing the file path with Open folder and Copy path buttons"

Recording happens **inside the game**, not in the inspector: every 1/fps seconds, after the frame has finished drawing (UI and post-processing
effects included), the game reads the image back from the graphics card, shrinks it and appends it to the GIF file. Because it reads from the game itself rather than grabbing the operating system's screen,
other windows covering the game do not end up in the file (tested). The file lives in the game's save folder,
`recordings/rec_<date>_<time>.gif` (the same place as the `screenshots/` of njin::screenshot(); see njin::save_path()); the inspector shows
the path, and **Open folder** opens that folder.

| Option | Values | Notes |
|---|---|---|
| Speed | 10, 15, 20, 30 frames/second | Default 15 |
| Size | 100%, 75%, 50%, 33% of the window | Default 50%. Shrinks by averaging pixels |
| Limit | 5 to 120 seconds | Stops on its own when time is up. Default 30 |

The options apply to the **next** recording. Recording stops when: you press Stop or F9, the limit is reached, the inspector closes or loses connection
(the game finishes the file first, so you can still watch it), or the game quits. If the game window is resized during recording, each frame is
squeezed back to the size at the start (if the aspect ratio changes too, the picture is distorted).

A few things about the file:

- Each frame has its **own 256-color palette**, chosen from that frame's real colors, with no color mixing (dither). Pixel art with fewer than
  256 colors comes out with the exact colors (to a precision of 5 bits per channel, a maximum error of 7 out of 255); smooth gradients may show
  color banding.
- A frame **identical** to the previous one is not written: the previous frame is just held longer. A still screen (a menu, a paused game) gives
  a very small file.
- Each frame's duration is the real time between two captures, so the GIF plays for exactly the time that was recorded, even when the game stutters.
  GIF viewers round each frame's duration to 1/100 second.
- Tested with njin_platformer (main menu, 640x360, 15 frames/second, 3 seconds): 41 frames, 248 KB.

**Cost:** in each frame that gets captured the game pays extra in that very frame: reading back from the GPU, then shrinking and GIF-compressing right on the game's thread
(measuring the compression step alone, a 480x270 frame, an image with many colors: about 4 ms). The inspector's frame time chart has spikes in step with the recording, and the game
slows down noticeably if the frame is already close to 16.7 ms. For a lighter load, lower the speed or the size.

## Cost and safety

- It only listens on `127.0.0.1`: other machines cannot connect. You should still only turn it on in debug builds
  (`#ifndef NDEBUG`), because the inspector can edit and destroy entities.
- With no inspector connected, each frame only costs one socket poll.
- With an inspector: frame numbers are sent every frame, while the entity list, the selected entity and watches
  are sent at `debug_server_desc::snapshot_hz` (default 10 times/second), up to
  `max_entities` entities (default 4000; the inspector tells you when the list is truncated).
- **It never freezes the game**: the socket is non-blocking, and if the inspector reads slowly, new data
  is dropped instead of piling up (the inspector's status bar reports the number of dropped messages).
- Game components are recognized by the type name the compiler generates, so the name may differ slightly
  between MSVC, GCC and Clang.

## Under the hood

The game and inspector talk with lines of JSON over TCP. On the game side is the core module `njin.debug`
(`runtime/modules/debug.cpp`); the inspector side is `src/tools/inspector`, using Dear ImGui through
rlImGui. Both sides check the protocol version number when connecting (currently 4): if it mismatches, the inspector says one of the two needs
rebuilding. Screen recording goes through the `rec` command (the inspector sends the speed, size and limit) and the `rec` message (the game reports
the state, frame count, size and file path); the capture and GIF writing are `runtime/modules/debug_record.cpp` and
`runtime/njin_gif.cpp`.

Turn off building the inspector (and downloading ImGui) with `-DNJIN_BUILD_INSPECTOR=OFF` when configuring CMake.
