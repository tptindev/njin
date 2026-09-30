add_executable(njin_sandtable main.cpp game.cpp world.cpp person.cpp crowd.cpp physics.cpp gang.cpp hud.cpp feeds.cpp weather.cpp view.cpp render.cpp audio.cpp
  city/city_map.cpp city/names.cpp city/check.cpp city/gen_core.cpp city/gen_terrain.cpp city/gen_streets.cpp
  city/gen_alleys.cpp city/gen_buildings.cpp city/gen_network.cpp city/gen_life.cpp city/interior.cpp city/interior_furnish.cpp
  city/render_common.cpp city/render_ground.cpp city/render_props.cpp
  city/render_debug.cpp city/render_cutaway.cpp city/render_facade.cpp
  city/render_lod.cpp city/render_hover.cpp city/render_kit.cpp)
target_link_libraries(njin_sandtable PRIVATE njin::rt njin::gpu njin_warnings)

set_target_properties(njin_sandtable PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/sandtable")
njin_add_assets(njin_sandtable assets)

# The njin icon on the exe, its window and its taskbar button (Windows).
njin_icon(njin_sandtable)

target_sources(njin_sandtable PRIVATE kungfu_preview.cpp)
# Animation review can build without unrelated city/gameplay source changes.
add_executable(njin_sandtable_kungfu EXCLUDE_FROM_ALL tools/kungfu_main.cpp kungfu_preview.cpp person.cpp)
target_link_libraries(njin_sandtable_kungfu PRIVATE njin::rt njin::gpu njin_warnings)
set_target_properties(njin_sandtable_kungfu PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/sandtable-kungfu")
