add_executable(njin_platformer main.cpp play.cpp menus.cpp ../shared/settings_menu.cpp)
target_link_libraries(njin_platformer PRIVATE njin::rt njin_warnings)

# Each game runs from its own folder (build/bin/platformer/), so two games never share or
# overwrite each other's assets.
set_target_properties(njin_platformer PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/platformer")

# assets/ is copied next to the exe on every build.
njin_add_assets(njin_platformer assets)

# `cmake --build <dir> --target njin_platformer_dist` makes dist/njin_platformer-<version>.zip:
# the exe (with icon, no console in Release) and the assets folder.
njin_package(njin_platformer
  NAME "Sprout's Climb"
  ICON icon.ico
  ASSETS assets)
