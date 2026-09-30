add_executable(njin_sandtable main.cpp game.cpp sim.cpp levels.cpp sprites.cpp weather.cpp view.cpp render.cpp audio.cpp)
target_link_libraries(njin_sandtable PRIVATE njin::rt njin::gpu njin_warnings)

set_target_properties(njin_sandtable PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/sandtable")
njin_add_assets(njin_sandtable assets)

# The njin icon on the exe, its window and its taskbar button (Windows).
njin_icon(njin_sandtable)
