add_executable(njin_pong main.cpp pong.cpp)

# njin::rt pulls njin::api (and its include dir) in transitively.
target_link_libraries(njin_pong PRIVATE njin::rt njin_warnings)
