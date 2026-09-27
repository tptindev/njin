add_executable(njin_moteswarm
  main.cpp game.cpp player.cpp npc.cpp render.cpp
  creature/blobs.cpp creature/tails.cpp creature/eye.cpp creature/tick.cpp creature/draw.cpp)
target_link_libraries(njin_moteswarm PRIVATE njin::rt njin::gpu njin_warnings)

# Each game runs from its own folder (build/bin/moteswarm/), so two games never share or
# overwrite each other's assets.
set_target_properties(njin_moteswarm PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/moteswarm")

njin_add_assets(njin_moteswarm assets)

# The njin icon on the exe, its window and its taskbar button (Windows).
njin_icon(njin_moteswarm)
