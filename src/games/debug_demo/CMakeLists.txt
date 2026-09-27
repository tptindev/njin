# A sandbox to try njin_inspector on: run_inspected.bat starts both.
add_executable(njin_debug_demo main.cpp)
target_link_libraries(njin_debug_demo PRIVATE njin::rt njin::gpu njin_warnings)

# The njin icon on the exe, its window and its taskbar button (Windows).
njin_icon(njin_debug_demo)
