# A 3D platformer on the 3D API (njin_3d.h): third-person camera, SDF capsule
# player, floating platforms, no external assets (everything drawn with
# primitives and shapes).
add_executable(njin_platformer3d main.cpp platformer3d.cpp)
target_link_libraries(njin_platformer3d PRIVATE njin::rt njin::gpu njin_warnings)

# Each game runs from its own folder (build/bin/platformer3d/), so two games never share or
# overwrite each other's assets.
set_target_properties(njin_platformer3d PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/platformer3d")

njin_icon(njin_platformer3d)
