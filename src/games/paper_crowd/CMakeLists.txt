add_executable(njin_paper_crowd main.cpp game.cpp crowd.cpp camera.cpp draw.cpp)
target_link_libraries(njin_paper_crowd PRIVATE njin::rt njin::gpu njin_warnings)

# Each game runs from its own folder (build/bin/paper_crowd/), so two games never
# share or overwrite each other's assets.
set_target_properties(njin_paper_crowd PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/paper_crowd")

njin_add_assets(njin_paper_crowd assets)

# The njin icon on the exe, its window and its taskbar button (Windows).
njin_icon(njin_paper_crowd)
