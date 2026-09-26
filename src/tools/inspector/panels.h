// One function per window. Each draws itself with Dear ImGui every frame and
// talks to the game only through `app` (send_cmd, select).
#pragma once
#include "app.h"

namespace inspector {
// Opens the window `name` if the current layout shows it (placing it when the
// layout just changed). Returns false, with nothing left open, when it is
// hidden or collapsed; the caller then just returns.
bool begin_panel(app &a, const char *name);
// Call after the last window of the frame.
void end_frame_layout(app &a);

void status_bar(app &a);          // top bar: connection and scene
void performance_window(app &a);  // FPS, frame graph, pause / step / time scale
void entities_window(app &a);     // filterable entity table
void world_window(app &a);        // map of colliders, tilemaps and the camera
void inspector_window(app &a);    // components of the selected entity
void watches_window(app &a);      // values set with njin::debug_watch
void log_window(app &a);          // the game's log
void monitor_window(app &a);      // CPU, RAM and GPU use of the game process
void systems_window(app &a);      // time per phase and per system
void memory_window(app &a);       // memory per component type and per entity
void assets_window(app &a);       // textures, sounds, fonts, targets: GPU and RAM
} // namespace inspector
