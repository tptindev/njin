if(WIN32)
  set(_njin_default_profiles "glsl;spirv;dx11")
elseif(APPLE)
  set(_njin_default_profiles "metal")
elseif(ANDROID OR EMSCRIPTEN)
  set(_njin_default_profiles "essl;spirv")
else()
  set(_njin_default_profiles "glsl;spirv")
endif()
set(NJIN_BGFX_SHADER_PROFILES "${_njin_default_profiles}" CACHE STRING "Embedded bgfx shader formats: glsl;spirv;dx11;metal;essl")
set(BGFX_DIR "${NJIN_BGFX_SOURCE_DIR}")
set(NJIN_BGFX_SHADERC "" CACHE FILEPATH "Host bgfx shaderc for cross compilation (otherwise built from source)")
if(NJIN_BGFX_SHADERC)
  set(_njin_shaderc "${NJIN_BGFX_SHADERC}")
  set(_njin_shaderc_dependency "${NJIN_BGFX_SHADERC}")
elseif(CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR "Cross builds require -DNJIN_BGFX_SHADERC=<host shaderc executable>")
else()
  set(_njin_shaderc "$<TARGET_FILE:shaderc>")
  set(_njin_shaderc_dependency shaderc)
endif()
set(_package "#include \"njin_bgfx.h\"\nnamespace njin {\n")
set(_lookup "namespace njin::rhi {\nstd::span<const unsigned char> bgfx_shader_bytes(std::string_view format,bool vertex,bool instanced) {\n")
set(_compat_lookup "std::span<const unsigned char> bgfx_compat_vertex_shader_bytes(std::string_view format) {\n")
set(_temporal_lookup "std::span<const unsigned char> bgfx_temporal_shader_bytes(std::string_view format,unsigned stage) {\n")
set(_smaa_lookup "std::span<const unsigned char> bgfx_smaa_shader_bytes(std::string_view format,unsigned stage) {\n")
set(_compute_lookup "std::span<const unsigned char> bgfx_compute_shader_bytes(std::string_view format,unsigned stage) {\n")
set(_atmosphere_lookup "std::span<const unsigned char> bgfx_atmosphere_shader_bytes(std::string_view format) {\n")
set(_virtual_package "#include \"njin_bgfx.h\"\nnamespace njin {\n")
set(_virtual_lookup "namespace njin::rhi {\nstd::span<const unsigned char> bgfx_virtual_shader_bytes(std::string_view format) {\n")
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
    message(FATAL_ERROR "Unknown NJIN_BGFX_SHADER_PROFILES value: ${format}")
  endif()
  set(input "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/fs_njin_atmosphere.sc")
  set(binary "${CMAKE_CURRENT_BINARY_DIR}/bgfx_${format}_atmosphere.bin")
  set(cpp "${binary}.cpp")
  set(symbol "njin_bgfx_${format}_atmosphere")
  add_custom_command(OUTPUT "${binary}" "${cpp}"
    COMMAND "${_njin_shaderc}" -f "${input}" -o "${binary}" --type fragment
      --platform ${platform} -p ${profile} -i "${BGFX_DIR}/src"
      --varyingdef "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/varying.def.sc"
    COMMAND "${CMAKE_COMMAND}" "-DOUT=${cpp}" "-DIN=${binary}" "-DSYMBOL=${symbol}" -P "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake"
    DEPENDS "${input}" "shaders/bgfx/varying.def.sc" "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake" ${_njin_shaderc_dependency}
    VERBATIM)
  target_sources(njin_core PRIVATE "${cpp}")
  string(APPEND _package "extern const unsigned char ${symbol}[]; extern const unsigned long long ${symbol}_size;\n")
  string(APPEND _atmosphere_lookup "if(format==\"${format}\") return {njin::${symbol},size_t(njin::${symbol}_size)};\n")
  foreach(stage vs fs vsi compat_vs)
    if(stage STREQUAL vs OR stage STREQUAL vsi OR stage STREQUAL compat_vs)
      set(type vertex)
    else()
      set(type fragment)
    endif()
    set(input "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/${stage}_njin.sc")
    if(stage STREQUAL vsi)
      set(input "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/vs_njin_instanced.sc")
    endif()
    if(stage STREQUAL compat_vs)
      set(input "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/vs_njin_compat.sc")
    endif()
    set(binary "${CMAKE_CURRENT_BINARY_DIR}/bgfx_${format}_${stage}.bin")
    set(cpp "${binary}.cpp")
    set(symbol "njin_bgfx_${format}_${stage}")
    add_custom_command(OUTPUT "${binary}" "${cpp}"
      COMMAND "${_njin_shaderc}" -f "${input}" -o "${binary}" --type ${type}
        --platform ${platform} -p ${profile} -i "${BGFX_DIR}/src"
        --varyingdef "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/varying.def.sc"
      COMMAND "${CMAKE_COMMAND}" "-DOUT=${cpp}" "-DIN=${binary}" "-DSYMBOL=${symbol}" -P "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake"
      DEPENDS "${input}" "shaders/bgfx/njin_terrain_relief.sh" "shaders/bgfx/njin_surface.sh" "shaders/bgfx/njin_world_fx.sh" "shaders/bgfx/njin_advanced_material.sh" "shaders/bgfx/njin_sky.sh" "shaders/bgfx/njin_clusters.sh" "shaders/bgfx/njin_legacy_post.sh" "shaders/bgfx/njin_particles.sh" "shaders/bgfx/njin_light2d.sh" "shaders/bgfx/njin_volume.sh" "shaders/bgfx/njin_water.sh" "shaders/bgfx/njin_fragment.sh" "shaders/bgfx/njin_virtual.sh" "shaders/bgfx/njin_surface.sh" "shaders/bgfx/varying.def.sc" "${BGFX_DIR}/src/bgfx_shader.sh" "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake" ${_njin_shaderc_dependency}
      VERBATIM)
    target_sources(njin_core PRIVATE "${cpp}")
    string(APPEND _package "extern const unsigned char ${symbol}[]; extern const unsigned long long ${symbol}_size;\n")
  endforeach()
  foreach(smaa_stage RANGE 0 2)
    set(input "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/fs_njin_smaa.sc")
    set(binary "${CMAKE_CURRENT_BINARY_DIR}/bgfx_${format}_smaa${smaa_stage}.bin")
    set(cpp "${binary}.cpp")
    set(symbol "njin_bgfx_${format}_smaa${smaa_stage}")
    add_custom_command(OUTPUT "${binary}" "${cpp}"
      COMMAND "${_njin_shaderc}" -f "${input}" -o "${binary}" --type fragment
        --platform ${platform} -p ${profile} -i "${BGFX_DIR}/src;${smaa_SOURCE_DIR}"
        --define "NJIN_SMAA_STAGE=${smaa_stage}"
        --varyingdef "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/varying.def.sc"
      COMMAND "${CMAKE_COMMAND}" "-DOUT=${cpp}" "-DIN=${binary}" "-DSYMBOL=${symbol}" -P "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake"
      DEPENDS "${input}" "shaders/bgfx/njin_terrain_relief.sh" "shaders/bgfx/njin_surface.sh" "shaders/bgfx/njin_world_fx.sh" "shaders/bgfx/njin_advanced_material.sh" "shaders/bgfx/njin_sky.sh" "shaders/bgfx/njin_clusters.sh" "shaders/bgfx/njin_legacy_post.sh" "shaders/bgfx/njin_particles.sh" "shaders/bgfx/njin_light2d.sh" "shaders/bgfx/njin_volume.sh" "shaders/bgfx/njin_water.sh" "shaders/bgfx/njin_fragment.sh" "shaders/bgfx/njin_virtual.sh" "${smaa_SOURCE_DIR}/SMAA.hlsl" "shaders/bgfx/varying.def.sc"
        "${BGFX_DIR}/src/bgfx_shader.sh" "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake" ${_njin_shaderc_dependency}
      VERBATIM)
    target_sources(njin_core PRIVATE "${cpp}")
    string(APPEND _package "extern const unsigned char ${symbol}[]; extern const unsigned long long ${symbol}_size;\n")
    string(APPEND _smaa_lookup "if(format==\"${format}\" && stage==${smaa_stage}) return {njin::${symbol},size_t(njin::${symbol}_size)};\n")
  endforeach()
  # Temporal stages plus volume (3), 2D lighting (4) and water (5) fragments.
  foreach(temporal_stage RANGE 0 8)
    if(temporal_stage EQUAL 0)
      set(input "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/vs_njin_velocity.sc")
      set(temporal_type vertex)
    elseif(temporal_stage EQUAL 1)
      set(input "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/fs_njin_velocity.sc")
      set(temporal_type fragment)
    elseif(temporal_stage EQUAL 2)
      set(input "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/fs_njin_depth_init.sc")
      set(temporal_type fragment)
    elseif(temporal_stage EQUAL 3)
      set(input "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/fs_njin_volume.sc")
      set(temporal_type fragment)
    elseif(temporal_stage EQUAL 4)
      set(input "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/fs_njin_light2d.sc")
      set(temporal_type fragment)
    elseif(temporal_stage EQUAL 6)
      set(input "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/vs_njin_terrain.sc")
      set(temporal_type vertex)
    elseif(temporal_stage EQUAL 7)
      set(input "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/vs_njin_terrain_velocity.sc")
      set(temporal_type vertex)
    elseif(temporal_stage EQUAL 8)
      set(input "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/fs_njin_virtual.sc")
      set(temporal_type fragment)
    else()
      set(input "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/fs_njin_water.sc")
      set(temporal_type fragment)
    endif()
    set(binary "${CMAKE_CURRENT_BINARY_DIR}/bgfx_${format}_temporal${temporal_stage}.bin")
    set(cpp "${binary}.cpp")
    set(symbol "njin_bgfx_${format}_temporal${temporal_stage}")
    set(_njin_volume_check)
    if(temporal_stage EQUAL 3)
      set(_njin_volume_check COMMAND "${CMAKE_COMMAND}" "-DBINARY=${binary}"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_bgfx_volume.cmake")
    endif()
    add_custom_command(OUTPUT "${binary}" "${cpp}"
      COMMAND "${_njin_shaderc}" -f "${input}" -o "${binary}" --type ${temporal_type}
        --platform ${platform} -p ${profile} -i "${BGFX_DIR}/src;${smaa_SOURCE_DIR}"
        --varyingdef "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/varying.def.sc"
      ${_njin_volume_check}
      COMMAND "${CMAKE_COMMAND}" "-DOUT=${cpp}" "-DIN=${binary}" "-DSYMBOL=${symbol}" -P "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake"
      DEPENDS "${input}" "shaders/bgfx/njin_terrain_relief.sh" "shaders/bgfx/njin_surface.sh" "shaders/bgfx/njin_world_fx.sh" "shaders/bgfx/njin_advanced_material.sh" "shaders/bgfx/njin_sky.sh" "shaders/bgfx/njin_clusters.sh" "shaders/bgfx/njin_legacy_post.sh" "shaders/bgfx/njin_particles.sh" "shaders/bgfx/njin_light2d.sh" "shaders/bgfx/njin_volume.sh" "shaders/bgfx/njin_water.sh" "shaders/bgfx/njin_fragment.sh" "shaders/bgfx/njin_virtual.sh" "${BGFX_DIR}/src/bgfx_shader.sh" "shaders/bgfx/varying.def.sc"
        "${BGFX_DIR}/src/bgfx_shader.sh" "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake" "${PROJECT_SOURCE_DIR}/cmake/check_bgfx_volume.cmake" ${_njin_shaderc_dependency}
      VERBATIM)
    target_sources(njin_core PRIVATE "${cpp}")
    string(APPEND _package "extern const unsigned char ${symbol}[]; extern const unsigned long long ${symbol}_size;\n")
    if(temporal_stage EQUAL 8)
      string(APPEND _virtual_package "extern const unsigned char ${symbol}[]; extern const unsigned long long ${symbol}_size;\n")
      string(APPEND _virtual_lookup "if(format==\"${format}\") return {njin::${symbol},size_t(njin::${symbol}_size)};\n")
    else()
      string(APPEND _temporal_lookup "if(format==\"${format}\" && stage==${temporal_stage}) return {njin::${symbol},size_t(njin::${symbol}_size)};\n")
    endif()
  endforeach()
  # Compute (ray tracing: 0 shadow trace, 1 denoise, 2 reflections, 3 irradiance grid, 4 its light per pixel). GLES compute needs ESSL 3.10.
  set(compute_profile ${profile})
  if(format STREQUAL essl)
    set(compute_profile 310_es)
  endif()
  foreach(compute_stage RANGE 0 8)
    set(input "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/cs_njin_rtshadow.sc")
    set(_compute_define "NJIN_RT_STAGE=${compute_stage}")
    if(compute_stage GREATER 4)
      math(EXPR _occlusion_stage "${compute_stage}-5")
      set(input "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/cs_njin_occlusion.sc")
      set(_compute_define "NJIN_OCCLUSION_STAGE=${_occlusion_stage}")
    endif()
    set(binary "${CMAKE_CURRENT_BINARY_DIR}/bgfx_${format}_compute${compute_stage}.bin")
    set(cpp "${binary}.cpp")
    set(symbol "njin_bgfx_${format}_compute${compute_stage}")
    add_custom_command(OUTPUT "${binary}" "${cpp}"
      COMMAND "${_njin_shaderc}" -f "${input}" -o "${binary}" --type compute
        --platform ${platform} -p ${compute_profile} -i "${BGFX_DIR}/src"
        --define "${_compute_define}"
      COMMAND "${CMAKE_COMMAND}" "-DOUT=${cpp}" "-DIN=${binary}" "-DSYMBOL=${symbol}" -P "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake"
      DEPENDS "${input}" "${BGFX_DIR}/src/bgfx_compute.sh" "${BGFX_DIR}/src/bgfx_shader.sh" "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake" ${_njin_shaderc_dependency}
      VERBATIM)
    target_sources(njin_core PRIVATE "${cpp}")
    string(APPEND _package "extern const unsigned char ${symbol}[]; extern const unsigned long long ${symbol}_size;\n")
    string(APPEND _compute_lookup "if(format==\"${format}\" && stage==${compute_stage}) return {njin::${symbol},size_t(njin::${symbol}_size)};\n")
  endforeach()
  string(APPEND _compat_lookup "if(format==\"${format}\") return {njin::njin_bgfx_${format}_compat_vs,size_t(njin::njin_bgfx_${format}_compat_vs_size)};\n")
  string(APPEND _lookup "if(format==\"${format}\") return vertex ? (instanced ? std::span<const unsigned char>{njin::njin_bgfx_${format}_vsi,size_t(njin::njin_bgfx_${format}_vsi_size)} : std::span<const unsigned char>{njin::njin_bgfx_${format}_vs,size_t(njin::njin_bgfx_${format}_vs_size)}) : std::span<const unsigned char>{njin::njin_bgfx_${format}_fs,size_t(njin::njin_bgfx_${format}_fs_size)};\n")
endforeach()
string(APPEND _package "}\n${_lookup}return {};\n}\n${_smaa_lookup}return {};\n}\n${_temporal_lookup}return {};\n}\n${_compute_lookup}return {};\n}\n${_atmosphere_lookup}return {};\n}\n${_compat_lookup}return {};\n}\n}\n")
# Vulkan ray query kernels (stages 0, 2, 3 of cs_njin_rtshadow.sc with the hardware
# traversal, njin_raytrace_vk.cpp). bgfx's shaderc cannot emit ray queries, so glslc
# (Vulkan SDK) compiles them; without it the compute traversal is used everywhere.
find_program(NJIN_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
set(_rt_hw_lookup "std::span<const unsigned char> bgfx_raytrace_hw_bytes(unsigned stage) {\n")
if(NJIN_GLSLC AND "spirv" IN_LIST NJIN_BGFX_SHADER_PROFILES)
  set(_rt_hw_source "${CMAKE_CURRENT_BINARY_DIR}/njin_rt_hw.comp")
  add_custom_command(OUTPUT "${_rt_hw_source}"
    COMMAND "${CMAKE_COMMAND}" "-DIN=${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/cs_njin_rtshadow.sc"
      "-DWRAPPER=${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx/njin_rt_hw.glsl" "-DOUT=${_rt_hw_source}"
      -P "${PROJECT_SOURCE_DIR}/cmake/njin_rt_hw_source.cmake"
    DEPENDS "shaders/bgfx/cs_njin_rtshadow.sc" "shaders/bgfx/njin_rt_hw.glsl" "${PROJECT_SOURCE_DIR}/cmake/njin_rt_hw_source.cmake"
    VERBATIM)
  foreach(rt_stage 0 2 3)
    set(binary "${CMAKE_CURRENT_BINARY_DIR}/njin_rt_hw${rt_stage}.spv")
    set(cpp "${binary}.cpp")
    set(symbol "njin_rt_hw${rt_stage}")
    add_custom_command(OUTPUT "${binary}" "${cpp}"
      COMMAND "${NJIN_GLSLC}" -fshader-stage=comp --target-env=vulkan1.2 -O "-DNJIN_RT_STAGE=${rt_stage}" "${_rt_hw_source}" -o "${binary}"
      COMMAND "${CMAKE_COMMAND}" "-DOUT=${cpp}" "-DIN=${binary}" "-DSYMBOL=${symbol}" -P "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake"
      DEPENDS "${_rt_hw_source}" "${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake"
      VERBATIM)
    target_sources(njin_core PRIVATE "${cpp}")
    string(APPEND _package "namespace njin {extern const unsigned char ${symbol}[]; extern const unsigned long long ${symbol}_size;}\n")
    string(APPEND _rt_hw_lookup "if(stage==${rt_stage}) return {njin::${symbol},size_t(njin::${symbol}_size)};\n")
  endforeach()
else()
  message(STATUS "njin: glslc not found, bgfx Vulkan ray tracing uses compute traversal only")
endif()
string(APPEND _package "namespace njin::rhi {\n${_rt_hw_lookup}return {};\n}\n}\n")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/njin_bgfx_shaders.cpp" CONTENT "${_package}")
target_sources(njin_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/njin_bgfx_shaders.cpp")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/njin_bgfx_virtual_shaders.cpp" CONTENT "${_virtual_package}}\n${_virtual_lookup}return {};\n}\n}\n")
target_sources(njin_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/njin_bgfx_virtual_shaders.cpp")
