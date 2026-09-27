add_executable(njin_pong main.cpp pong.cpp)

# njin::rt pulls njin::api (and its include dir) in transitively.
target_link_libraries(njin_pong PRIVATE njin::rt njin::gpu njin_warnings)

# The njin icon on the exe, its window and its taskbar button (Windows).
njin_icon(njin_pong)
