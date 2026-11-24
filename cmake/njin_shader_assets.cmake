# Compile a relocatable bgfx shader package; call after add_subdirectory(njin).
# FRAGMENT is required. VERTEX (optional) is the shader's own vertex stage, for 2D draws
# and draw_instanced (a_position: the unit quad; i_data0..3: the instance's floats;
# gl_Position = njin_clip(mul(u_njinMVP, ...)) from <njin_vertex.sh>); 3D draws refuse a
# package with one.
# Uniform/sampler metadata is JSON supplied by the project. Runtime accepts no GLSL source.
function(njin_shader_asset target)
  cmake_parse_arguments(SH "" "NAME;FRAGMENT;VERTEX;METADATA;OUTPUT_DIRECTORY" "DEPENDS" ${ARGN})
  if(NOT NJIN_BGFX)
    message(FATAL_ERROR "njin_shader_asset requires NJIN_BGFX=ON")
  endif()
  foreach(required NAME FRAGMENT METADATA OUTPUT_DIRECTORY)
    if(NOT SH_${required})
      message(FATAL_ERROR "njin_shader_asset: missing ${required}")
    endif()
  endforeach()
  if(NJIN_BGFX_SHADERC)
    set(shaderc "${NJIN_BGFX_SHADERC}")
    set(shaderc_dependency "${NJIN_BGFX_SHADERC}")
  else()
    set(shaderc "$<TARGET_FILE:shaderc>")
    set(shaderc_dependency shaderc)
  endif()
  get_property(bgfx_source GLOBAL PROPERTY NJIN_SHADER_BGFX_SOURCE_DIR)
  set(engine_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
  set(varying "${engine_root}/src/engine/runtime/shaders/bgfx/varying.def.sc")
  file(READ "${SH_METADATA}" manifest)
  string(JSON manifest SET "${manifest}" version 1)
  string(JSON manifest SET "${manifest}" profiles "{}")
  set(outputs)
  foreach(format IN LISTS NJIN_BGFX_SHADER_PROFILES)
    if(format STREQUAL glsl)
      set(profile 430)
      set(platform linux)
    elseif(format STREQUAL spirv)
      set(profile spirv)
      set(platform linux)
    elseif(format STREQUAL dx11)
      set(profile s_5_0)
      set(platform windows)
    elseif(format STREQUAL metal)
      set(profile metal)
      set(platform osx)
    elseif(format STREQUAL essl)
      set(profile 300_es)
      set(platform android)
    else()
      message(FATAL_ERROR "njin_shader_asset: unknown profile ${format}")
    endif()
    set(filename "${SH_NAME}.${format}.bin")
    set(binary "${SH_OUTPUT_DIRECTORY}/${filename}")
    add_custom_command(OUTPUT "${binary}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${SH_OUTPUT_DIRECTORY}"
      COMMAND "${shaderc}" -f "${SH_FRAGMENT}" -o "${binary}.tmp" --type fragment
        --platform ${platform} -p ${profile} -i "${bgfx_source}/src" -i "${engine_root}/src/engine/runtime/shaders/bgfx"
        --varyingdef "${varying}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${binary}.tmp" "${binary}"
      COMMAND "${CMAKE_COMMAND}" -E rm -f "${binary}.tmp"
      DEPENDS "${SH_FRAGMENT}" "${varying}" "${bgfx_source}/src/bgfx_shader.sh"
        ${shaderc_dependency} ${SH_DEPENDS} VERBATIM)
    list(APPEND outputs "${binary}")
    string(JSON manifest SET "${manifest}" profiles ${format} "{}")
    string(JSON manifest SET "${manifest}" profiles ${format} fragment "\"${filename}\"")
    if(SH_VERTEX)
      set(vertex_filename "${SH_NAME}.vertex.${format}.bin")
      set(vertex_binary "${SH_OUTPUT_DIRECTORY}/${vertex_filename}")
      add_custom_command(OUTPUT "${vertex_binary}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SH_OUTPUT_DIRECTORY}"
        COMMAND "${shaderc}" -f "${SH_VERTEX}" -o "${vertex_binary}.tmp" --type vertex
          --platform ${platform} -p ${profile} -i "${bgfx_source}/src" -i "${engine_root}/src/engine/runtime/shaders/bgfx"
          --varyingdef "${varying}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${vertex_binary}.tmp" "${vertex_binary}"
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${vertex_binary}.tmp"
        DEPENDS "${SH_VERTEX}" "${varying}" "${bgfx_source}/src/bgfx_shader.sh"
          ${shaderc_dependency} ${SH_DEPENDS} VERBATIM)
      list(APPEND outputs "${vertex_binary}")
      string(JSON manifest SET "${manifest}" profiles ${format} vertex "\"${vertex_filename}\"")
    endif()
  endforeach()
  file(MAKE_DIRECTORY "${SH_OUTPUT_DIRECTORY}")
  file(CONFIGURE OUTPUT "${SH_OUTPUT_DIRECTORY}/${SH_NAME}.njshader" CONTENT "${manifest}\n" @ONLY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SH_METADATA}")
  add_custom_target(${target}_${SH_NAME}_shaders DEPENDS ${outputs})
  add_dependencies(${target} ${target}_${SH_NAME}_shaders)
endfunction()
