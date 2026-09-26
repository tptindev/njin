add_executable(njin_sandbox main.cpp modules/hello.cpp)

# njin::rt pulls njin::api (and its include dir) in transitively.
target_link_libraries(njin_sandbox PRIVATE njin::rt njin::gpu njin_warnings)
