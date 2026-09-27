# Public API surface: headers only. Implementations live in ../runtime.
add_library(njin_api INTERFACE)
add_library(njin::api ALIAS njin_api)

target_include_directories(njin_api INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(njin_api INTERFACE cxx_std_20)
target_link_libraries(njin_api INTERFACE EnTT::EnTT)

# The engine boundary. A target with NJIN_ENGINE_INTERNAL on (njin_rt, the
# inspector) is the engine; every other one is a game. A game gets ../guard
# first on its include path, whose raylib.h, rlgl.h, raymath.h and GLFW/glfw3.h
# only #error, so it cannot use raylib even when the toolchain ships a copy.
# The engine gets NJIN_ENGINE_INTERNAL defined, which the private runtime
# headers require (runtime/njin_internal_only.h).
set(njin_internal "$<BOOL:$<TARGET_PROPERTY:NJIN_ENGINE_INTERNAL>>")
target_include_directories(
  njin_api BEFORE INTERFACE
  "$<$<NOT:${njin_internal}>:${CMAKE_CURRENT_SOURCE_DIR}/../guard>")
target_compile_definitions(njin_api INTERFACE "$<${njin_internal}:NJIN_ENGINE_INTERNAL>")
