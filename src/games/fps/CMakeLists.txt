# First-person aim trainer on the 3D API (njin_3d.h), after Biped-Potato's Bevy
# FPS tutorial; the gun model is theirs (assets/models/LICENSE.txt).
add_executable(njin_fps main.cpp fps.cpp)
target_link_libraries(njin_fps PRIVATE njin::rt njin::gpu njin_warnings)

# Each game runs from its own folder (build/bin/fps/), so two games never share or
# overwrite each other's assets.
set_target_properties(njin_fps PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/fps")

njin_add_assets(njin_fps assets)

njin_icon(njin_fps)
