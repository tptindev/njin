# Public API surface: headers only. Implementations live in ../runtime.
add_library(njin_api INTERFACE)
add_library(njin::api ALIAS njin_api)

target_include_directories(njin_api INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(njin_api INTERFACE cxx_std_20)
target_link_libraries(njin_api INTERFACE EnTT::EnTT)
