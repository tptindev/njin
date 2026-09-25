file(GLOB_RECURSE NJIN_RT_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

if(NOT NJIN_RT_SOURCES)
  message(
    FATAL_ERROR "njin_rt: no .cpp found under ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

add_library(njin_rt STATIC ${NJIN_RT_SOURCES})
add_library(njin::rt ALIAS njin_rt)

# raylib stays private. EnTT is part of the public ECS API through njin::api.
target_link_libraries(
  njin_rt
  PUBLIC njin::api
  PRIVATE raylib njin_warnings)
