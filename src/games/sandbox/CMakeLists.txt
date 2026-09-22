add_executable(njin_sandbox main.cpp)

# njin::rt pulls njin::api (and its include dir) in transitively.
target_link_libraries(njin_sandbox PRIVATE njin::rt njin_warnings)
