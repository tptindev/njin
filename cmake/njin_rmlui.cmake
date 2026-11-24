# RmlUi (MIT) with FreeType (FreeType License), for the rmlui feature: HTML/CSS
# documents as game UI, opt-in (njin_use(<game> FEATURES rmlui)). Built from their
# sources as sdl3-proj builds them: RmlUi's core with its FreeType font engine, the
# visual debugger and the SDL3 platform layer from Backends/ (input, clipboard, IME,
# cursors). The renderer is Njin's own (src/engine/runtime/njin_rml.cpp). Only the
# runtime's rmlui feature links it; games reach it through src/engine/api/njin_rml.h.
include(FetchContent)

# FreeType without its optional codecs: fonts only.
foreach(_njin_ft_opt FT_DISABLE_ZLIB FT_DISABLE_BZIP2 FT_DISABLE_PNG FT_DISABLE_HARFBUZZ FT_DISABLE_BROTLI)
  set(${_njin_ft_opt} ON CACHE BOOL "" FORCE)
endforeach()
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  freetype
  GIT_REPOSITORY https://github.com/freetype/freetype.git
  GIT_TAG VER-2-14-3
  GIT_SHALLOW TRUE
  EXCLUDE_FROM_ALL
  SYSTEM)
FetchContent_MakeAvailable(freetype)

# RmlUi: sources only (its own CMake project would look for FreeType with
# find_package and build samples).
FetchContent_Declare(
  rmlui
  GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
  GIT_TAG 6.3
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR njin-sources-only
  SYSTEM)
FetchContent_MakeAvailable(rmlui)

file(GLOB _njin_rmlui_sources
  "${rmlui_SOURCE_DIR}/Source/Core/*.cpp"
  "${rmlui_SOURCE_DIR}/Source/Core/Elements/*.cpp"
  "${rmlui_SOURCE_DIR}/Source/Core/Layout/*.cpp"
  "${rmlui_SOURCE_DIR}/Source/Core/FontEngineDefault/*.cpp"
  "${rmlui_SOURCE_DIR}/Source/Debugger/*.cpp")
add_library(njin_rmlui_lib STATIC ${_njin_rmlui_sources} "${rmlui_SOURCE_DIR}/Backends/RmlUi_Platform_SDL.cpp")
target_include_directories(njin_rmlui_lib SYSTEM PUBLIC "${rmlui_SOURCE_DIR}/Include" "${rmlui_SOURCE_DIR}/Backends")
target_compile_definitions(njin_rmlui_lib
  PUBLIC RMLUI_STATIC_LIB RMLUI_SDL_VERSION_MAJOR=3
  PRIVATE RMLUI_FONT_ENGINE_FREETYPE RMLUI_VERSION="6.3")
target_link_libraries(njin_rmlui_lib PUBLIC SDL3::SDL3 PRIVATE freetype)
if(MSVC)
  target_compile_options(njin_rmlui_lib PRIVATE /bigobj /utf-8)
endif()
# Not a unity build: RmlUi's sources reuse file-local names (one `instance` per
# singleton, the debugger's embedded stylesheets) that clash in one translation unit.
set_target_properties(njin_rmlui_lib PROPERTIES UNITY_BUILD OFF)
