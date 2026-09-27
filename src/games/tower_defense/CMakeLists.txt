add_executable(njin_tower_defense main.cpp game.cpp render.cpp effects.cpp)
target_link_libraries(njin_tower_defense PRIVATE njin::rt njin_warnings)
njin_add_assets(njin_tower_defense assets)
