add_executable(njin_sandbox main.cpp modules/hello.cpp)

# njin::rt pulls njin::api (and its include dir) in transitively.
target_link_libraries(njin_sandbox PRIVATE njin::rt njin::gpu njin_warnings)

# The njin icon on the exe, its window and its taskbar button (Windows).
njin_icon(njin_sandbox)
