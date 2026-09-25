# Build helpers for games made with njin. Included by the top-level
# CMakeLists.txt, so every game under src/games can call them.

# njin_add_assets(<target> <dir>)
#
# Copies the folder <dir> (relative to the calling CMakeLists.txt) next to the
# executable of <target>, keeping its name: `njin_add_assets(my_game assets)`
# puts it at build/bin/assets. Runs on every build of <target> but only copies
# files that changed, so editing a texture and rebuilding is enough. Files
# deleted from <dir> are not deleted from the copy.
#
# The engine's loaders look for relative paths next to the executable when
# they are not found from the working directory, so the game finds
# "assets/player.png" however it is started.
function(njin_add_assets target dir)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "njin_add_assets: '${target}' is not a target")
  endif()
  cmake_path(ABSOLUTE_PATH dir BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
             NORMALIZE OUTPUT_VARIABLE source)
  if(NOT IS_DIRECTORY "${source}")
    message(FATAL_ERROR "njin_add_assets: folder not found: ${source}")
  endif()
  cmake_path(GET source FILENAME name)
  add_custom_target(
    ${target}_${name}
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${source}"
            "$<TARGET_FILE_DIR:${target}>/${name}"
    COMMENT "Copying ${name}/ next to ${target}"
    VERBATIM)
  add_dependencies(${target} ${target}_${name})
endfunction()
