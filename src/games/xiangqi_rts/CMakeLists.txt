add_executable(njin_xiangqi_rts main.cpp game.cpp sim.cpp render.cpp audio.cpp)
target_link_libraries(njin_xiangqi_rts PRIVATE njin::rt njin_warnings)

set_target_properties(njin_xiangqi_rts PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/xiangqi_rts")
njin_add_assets(njin_xiangqi_rts assets)

# The njin icon on the exe, its window and its taskbar button (Windows).
njin_icon(njin_xiangqi_rts)
