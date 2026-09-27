# A sample for the rendering features: GPU particles, atlas, culling, post
# effects, vsync. Read main.cpp for the keys.
add_executable(njin_render_demo main.cpp)
target_link_libraries(njin_render_demo PRIVATE njin::rt njin::gpu njin_warnings)

# Each game runs from its own folder (build/bin/render_demo/), so two games never
# share or overwrite each other's assets.
set_target_properties(njin_render_demo PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/render_demo")

njin_add_assets(njin_render_demo assets)

# The njin icon on the exe, its window and its taskbar button (Windows).
njin_icon(njin_render_demo)
