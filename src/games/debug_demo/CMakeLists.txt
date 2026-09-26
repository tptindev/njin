# A sandbox to try njin_inspector on: run_inspected.bat starts both.
add_executable(njin_debug_demo main.cpp)
target_link_libraries(njin_debug_demo PRIVATE njin::rt njin_warnings)
