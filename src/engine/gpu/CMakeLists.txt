# njin::gpu: link it into a game (target_link_libraries(my_game PRIVATE njin::gpu))
# and, on a laptop with an integrated and a discrete GPU, Windows runs the game on
# the discrete one. Compiled into the executable itself, see gpu_preference.cpp.
#
# The Linux side (PRIME offload variables) is in njin_rt, njin_gpu_hint.cpp: it
# needs no help from the linker.
#
# Turn both off with -DNJIN_PREFER_DISCRETE_GPU=OFF, for example to test on the
# integrated GPU. Defined here, before njin_rt is added, because njin_rt reads it.
option(NJIN_PREFER_DISCRETE_GPU "Run games on the discrete GPU of a hybrid laptop" ON)

add_library(njin_gpu INTERFACE)
add_library(njin::gpu ALIAS njin_gpu)
if(WIN32 AND NJIN_PREFER_DISCRETE_GPU)
  target_sources(njin_gpu INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/gpu_preference.cpp")
endif()
