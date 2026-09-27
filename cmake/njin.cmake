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

# njin_check_boundary()
#
# Runs by itself at the end of configure (deferred below). Every target that
# links njin and is not part of the engine (NJIN_ENGINE_INTERNAL off) is a
# game, and a game reaches raylib only through njin. Configure fails when a
# game links raylib or GLFW, directly or through a library that passes it on,
# or has on its include path the runtime folder or any folder holding
# raylib.h or GLFW/glfw3.h. The compile-time half of the boundary is in
# src/engine/api/CMakeLists.txt.
function(_njin_forbidden_link item out)
  set(${out} "" PARENT_SCOPE)
  # A static library's private dependencies show as $<LINK_ONLY:x>: linked, but
  # no headers pass on. That is how njin_rt carries raylib, and it is allowed.
  if(item MATCHES "^\\$<LINK_ONLY:")
    return()
  endif()
  if(TARGET ${item})
    get_target_property(real ${item} ALIASED_TARGET)
    if(NOT real)
      set(real ${item})
    endif()
    if(real MATCHES "^(raylib|glfw.*)$")
      set(${out} "${item}" PARENT_SCOPE)
      return()
    endif()
    get_property(visited GLOBAL PROPERTY _NJIN_BOUNDARY_VISITED)
    if(real IN_LIST visited)
      return()
    endif()
    set_property(GLOBAL APPEND PROPERTY _NJIN_BOUNDARY_VISITED ${real})
    get_target_property(deps ${real} INTERFACE_LINK_LIBRARIES)
    if(deps)
      foreach(dep IN LISTS deps)
        _njin_forbidden_link("${dep}" found)
        if(found)
          set(${out} "${found} (through ${item})" PARENT_SCOPE)
          return()
        endif()
      endforeach()
    endif()
  elseif(item MATCHES "(^|[/\\\\]|-l)(lib)?(raylib|glfw)[^/\\\\]*$")
    set(${out} "${item}" PARENT_SCOPE)
  endif()
endfunction()

function(_njin_check_target target runtime_dir guard_dir)
  get_target_property(type ${target} TYPE)
  if(type STREQUAL "INTERFACE_LIBRARY" OR type STREQUAL "UTILITY")
    return()
  endif()
  get_target_property(internal ${target} NJIN_ENGINE_INTERNAL)
  if(internal)
    return()
  endif()
  get_target_property(links ${target} LINK_LIBRARIES)
  if(NOT links)
    return()
  endif()
  if(NOT links MATCHES "(^|;)njin(::|_)(rt|api)(;|$)")
    return()
  endif()
  foreach(item IN LISTS links)
    set_property(GLOBAL PROPERTY _NJIN_BOUNDARY_VISITED "")
    _njin_forbidden_link("${item}" found)
    if(found)
      message(FATAL_ERROR
        "njin: game target '${target}' links ${found}. Games reach raylib only "
        "through njin (njin::rt); add what is missing to src/engine/api.")
    endif()
  endforeach()
  get_target_property(dirs ${target} INCLUDE_DIRECTORIES)
  if(dirs)
    foreach(dir IN LISTS dirs)
      if(dir MATCHES "\\$<")
        continue()
      endif()
      cmake_path(NORMAL_PATH dir OUTPUT_VARIABLE dir)
      cmake_path(IS_PREFIX runtime_dir "${dir}" NORMALIZE under_runtime)
      if(under_runtime
         OR (NOT dir STREQUAL guard_dir
             AND (EXISTS "${dir}/raylib.h" OR EXISTS "${dir}/GLFW/glfw3.h")))
        message(FATAL_ERROR
          "njin: game target '${target}' has '${dir}' on its include path. "
          "Games include njin.h only, not raylib or the engine runtime.")
      endif()
    endforeach()
  endif()
endfunction()

function(_njin_check_dir dir runtime_dir guard_dir)
  get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS targets)
    _njin_check_target(${target} "${runtime_dir}" "${guard_dir}")
  endforeach()
  get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(sub IN LISTS subdirs)
    _njin_check_dir("${sub}" "${runtime_dir}" "${guard_dir}")
  endforeach()
endfunction()

function(njin_check_boundary)
  # Deferred calls run in the top-level scope, which may be a parent project,
  # so the njin folder comes from where this function is defined.
  set(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
  cmake_path(NORMAL_PATH root)
  _njin_check_dir("${CMAKE_SOURCE_DIR}" "${root}src/engine/runtime"
                  "${root}src/engine/guard")
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL njin_check_boundary)

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
