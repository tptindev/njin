# Box pushing in 2.5D on the 3D API (njin_3d.h): boxes for walls and crates, a
# capsule with its own shader for the player.
add_executable(njin_sokoban main.cpp sokoban.cpp)
target_link_libraries(njin_sokoban PRIVATE njin::rt njin::gpu njin_warnings)

# Each game runs from its own folder (build/bin/sokoban/), so two games never share or
# overwrite each other's assets.
set_target_properties(njin_sokoban PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/sokoban")

njin_add_assets(njin_sokoban assets)

njin_icon(njin_sokoban)
