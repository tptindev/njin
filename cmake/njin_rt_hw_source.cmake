# cmake -DIN=cs_njin_rtshadow.sc -DWRAPPER=njin_rt_hw.glsl -DOUT=<out.comp> -P njin_rt_hw_source.cmake
# The bgfx compute shader as plain Vulkan GLSL for glslc: njin_rt_hw.glsl replaces
# bgfx_compute.sh and declares the uniforms in one block, so the loose ones go.
file(READ "${IN}" source)
file(READ "${WRAPPER}" wrapper)
# #version must come first: it leads the file, the rest of the wrapper takes the include's place.
string(REPLACE "#version 460\n" "" wrapper "${wrapper}")
string(REPLACE "#include \"bgfx_compute.sh\"" "${wrapper}" source "${source}")
set(source "#version 460\n${source}")
string(REGEX REPLACE "\nuniform (vec4|mat4) u_[A-Za-z]+(\\[[0-9]+\\])?;" "\n// (uniform in NjinRtUniforms)" source "${source}")
if(EXISTS "${OUT}")
  file(READ "${OUT}" old)
  if(old STREQUAL source)
    return()
  endif()
endif()
file(WRITE "${OUT}" "${source}")
