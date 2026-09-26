# Build helpers for games made with njin. Included by the top-level
# CMakeLists.txt, so every game under src/games can call them.

# njin_add_assets(<target> <dir>)
#
# Copies the folder <dir> (relative to the calling CMakeLists.txt) next to the
# executable of <target>, keeping its name: `njin_add_assets(my_game assets)`
# puts it at <exe folder>/assets. Runs on every build of <target> but only copies
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

# The icon and version resource of njin_package need the resource compiler.
# enable_language only works at file scope, so it is switched on here.
if(WIN32)
  enable_language(RC)
endif()

# njin_package(<target> [NAME <shown name>] [VERSION <x.y.z>] [ICON <file.ico>]
#              [ASSETS <dir>...] [FILES <file>...])
#
# Makes <target> ready to hand to players:
# - on Windows, embeds the icon (shown by Explorer and the taskbar) and a
#   version resource (NAME, VERSION) in the executable, and builds it without
#   a console window in every configuration but Debug;
# - adds a target <target>_dist that gathers the executable, the ASSETS
#   folders and the extra FILES (readme, licence) into build/dist/<target>/,
#   then zips that folder to build/dist/<target>-<VERSION>.zip.
#
#   cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-release --target my_game_dist
#
# Paths are relative to the calling CMakeLists.txt. VERSION defaults to the
# project version.
function(njin_package target)
  cmake_parse_arguments(PKG "" "NAME;VERSION;ICON" "ASSETS;FILES" ${ARGN})
  if(NOT TARGET ${target})
    message(FATAL_ERROR "njin_package: '${target}' is not a target")
  endif()
  if(NOT PKG_NAME)
    set(PKG_NAME "${target}")
  endif()
  if(NOT PKG_VERSION)
    set(PKG_VERSION "${PROJECT_VERSION}")
  endif()
  if(NOT PKG_VERSION)
    set(PKG_VERSION "0.0.0")
  endif()

  if(WIN32)
    string(REPLACE "." ";" parts "${PKG_VERSION}")
    list(APPEND parts 0 0 0 0)
    list(SUBLIST parts 0 4 parts)
    list(JOIN parts "," numeric)
    set(rc "${CMAKE_CURRENT_BINARY_DIR}/${target}_package.rc")
    set(body "")
    if(PKG_ICON)
      cmake_path(ABSOLUTE_PATH PKG_ICON BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                 NORMALIZE OUTPUT_VARIABLE icon)
      if(NOT EXISTS "${icon}")
        message(FATAL_ERROR "njin_package: icon not found: ${icon}")
      endif()
      string(APPEND body "1 ICON \"${icon}\"\n")
    endif()
    string(APPEND body
      "1 VERSIONINFO\n"
      "FILEVERSION ${numeric}\n"
      "PRODUCTVERSION ${numeric}\n"
      "BEGIN\n"
      "  BLOCK \"StringFileInfo\"\n"
      "  BEGIN\n"
      "    BLOCK \"040904b0\"\n"
      "    BEGIN\n"
      "      VALUE \"FileDescription\", \"${PKG_NAME}\"\n"
      "      VALUE \"ProductName\", \"${PKG_NAME}\"\n"
      "      VALUE \"FileVersion\", \"${PKG_VERSION}\"\n"
      "      VALUE \"ProductVersion\", \"${PKG_VERSION}\"\n"
      "      VALUE \"OriginalFilename\", \"${target}.exe\"\n"
      "    END\n"
      "  END\n"
      "  BLOCK \"VarFileInfo\"\n"
      "  BEGIN\n"
      "    VALUE \"Translation\", 0x409, 1200\n"
      "  END\n"
      "END\n")
    file(CONFIGURE OUTPUT "${rc}" CONTENT "${body}" @ONLY)
    target_sources(${target} PRIVATE "${rc}")
    # No console window for players; Debug keeps it for the log.
    set_property(TARGET ${target} PROPERTY WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>)
    if(MSVC)
      # A GUI program starts at WinMain; keep main() as the entry point.
      target_link_options(${target} PRIVATE $<$<NOT:$<CONFIG:Debug>>:/ENTRY:mainCRTStartup>)
    endif()
  endif()

  set(dist_root "${CMAKE_BINARY_DIR}/dist")
  set(dist "${dist_root}/${target}")
  set(commands
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${dist}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${dist}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${target}>" "${dist}/")
  foreach(dir IN LISTS PKG_ASSETS)
    cmake_path(ABSOLUTE_PATH dir BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
               NORMALIZE OUTPUT_VARIABLE source)
    cmake_path(GET source FILENAME name)
    list(APPEND commands COMMAND ${CMAKE_COMMAND} -E copy_directory "${source}" "${dist}/${name}")
  endforeach()
  foreach(f IN LISTS PKG_FILES)
    cmake_path(ABSOLUTE_PATH f BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
               NORMALIZE OUTPUT_VARIABLE source)
    list(APPEND commands COMMAND ${CMAKE_COMMAND} -E copy "${source}" "${dist}/")
  endforeach()
  set(zip "${target}-${PKG_VERSION}.zip")
  list(APPEND commands
    COMMAND ${CMAKE_COMMAND} -E rm -f "${dist_root}/${zip}"
    COMMAND ${CMAKE_COMMAND} -E chdir "${dist_root}" ${CMAKE_COMMAND} -E tar cf "${zip}" --format=zip "${target}")
  add_custom_target(${target}_dist ${commands}
    DEPENDS ${target}
    COMMENT "Packaging ${target} into dist/${zip}"
    VERBATIM)
endfunction()
