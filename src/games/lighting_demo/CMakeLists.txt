# A gallery of the 2D lighting: twelve rooms, one case in each. Read main.cpp for the rooms and the keys.
add_executable(njin_lighting_demo main.cpp module.cpp common.cpp rooms_lights.cpp rooms_occluders.cpp rooms_surfaces.cpp
                                  hud.cpp)
target_link_libraries(njin_lighting_demo PRIVATE njin::rt njin_warnings)

# Each game runs from its own folder (build/bin/lighting_demo/), so two games never
# share or overwrite each other's assets.
set_target_properties(njin_lighting_demo PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/lighting_demo")

njin_add_assets(njin_lighting_demo assets)

# The njin icon on the exe, its window and its taskbar button (Windows).
njin_icon(njin_lighting_demo)
