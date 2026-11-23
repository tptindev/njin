# bgfx b2513038: retain GLES 3.0 while detecting core compute at runtime;
# split Vulkan indirect submissions when optional multiDrawIndirect is absent.
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${NJIN_BGFX_SOURCE_DIR}"
  OUTPUT_VARIABLE _njin_occlusion_revision OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _njin_occlusion_git)
if(NOT _njin_occlusion_git EQUAL 0 OR NOT _njin_occlusion_revision STREQUAL "b2513038f4b753ebeced40821c193978daa0492f")
  message(FATAL_ERROR "Review njin_bgfx_occlusion_fix.cmake for the new bgfx revision")
endif()
function(_njin_occlusion_replace variable old new)
  string(FIND "${${variable}}" "${new}" _new_at)
  if(NOT _new_at EQUAL -1)
    return()
  endif()
  string(FIND "${${variable}}" "${old}" _old_at)
  if(_old_at EQUAL -1)
    message(FATAL_ERROR "bgfx occlusion patch anchor changed; review ${CMAKE_CURRENT_LIST_FILE}")
  endif()
  string(REPLACE "${old}" "${new}" _patched "${${variable}}")
  set(${variable} "${_patched}" PARENT_SCOPE)
endfunction()

set(_path "${NJIN_BGFX_SOURCE_DIR}/src/renderer_gl.cpp")
file(READ "${_path}" _gl)
set(_gl_before "${_gl}")
_njin_occlusion_replace(_gl
  "|| !!(BGFX_CONFIG_RENDERER_OPENGLES >= 31)"
  "|| (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES) && m_essl310Support\n\t\t\t\t\t\t&& NULL != glDispatchCompute && NULL != glBindImageTexture\n\t\t\t\t\t\t&& NULL != glBindBufferBase && NULL != glMemoryBarrier) // Njin runtime ES 3.1")
_njin_occlusion_replace(_gl
  "|| (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES >= 31) // Core since ES 3.1."
  "|| (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES) && m_essl310Support // Njin: core since ES 3.1.")
_njin_occlusion_replace(_gl
  "||  BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES >= 31)"
  "||  (0 != (g_caps.supported & BGFX_CAPS_COMPUTE)) // Njin runtime compute barriers")
if(NOT _gl STREQUAL _gl_before)
  file(WRITE "${_path}" "${_gl}")
endif()

set(_path "${NJIN_BGFX_SOURCE_DIR}/src/renderer_vk.cpp")
file(READ "${_path}" _vk)
set(_vk_before "${_vk}")
_njin_occlusion_replace(_vk
  "const bool indirectDrawSupport = true\n\t\t\t\t\t&& m_deviceFeatures.multiDrawIndirect\n\t\t\t\t\t&& m_deviceFeatures.drawIndirectFirstInstance"
  "const bool indirectDrawSupport = true // Njin splits draws without multiDrawIndirect.\n\t\t\t\t\t&& m_deviceFeatures.drawIndirectFirstInstance")
_njin_occlusion_replace(_vk
  "s_extension[Extension::KHR_draw_indirect_count        ].m_supported && indirectDrawSupport ?"
  "s_extension[Extension::KHR_draw_indirect_count        ].m_supported && indirectDrawSupport && m_deviceFeatures.multiDrawIndirect ?")
foreach(_command IN ITEMS vkCmdDrawIndirect vkCmdDrawIndexedIndirect)
  set(_old "${_command}(\n\t\t\t\t\t\t\t\t\t  m_commandBuffer\n\t\t\t\t\t\t\t\t\t, bufferIndirect\n\t\t\t\t\t\t\t\t\t, bufferOffsetIndirect\n\t\t\t\t\t\t\t\t\t, numDrawIndirect\n\t\t\t\t\t\t\t\t\t, BGFX_CONFIG_DRAW_INDIRECT_STRIDE\n\t\t\t\t\t\t\t\t\t);")
  set(_new "// Njin: drawCount <= 1 is core even without multiDrawIndirect.\n\t\t\t\t\t\t\t\tfor (uint32_t first = 0; first < numDrawIndirect;)\n\t\t\t\t\t\t\t\t{\n\t\t\t\t\t\t\t\t\tconst uint32_t count = m_deviceFeatures.multiDrawIndirect\n\t\t\t\t\t\t\t\t\t\t? bx::min(numDrawIndirect - first, m_deviceProperties.limits.maxDrawIndirectCount) : 1;\n\t\t\t\t\t\t\t\t\t${_command}(m_commandBuffer, bufferIndirect,\n\t\t\t\t\t\t\t\t\t\tbufferOffsetIndirect + first * BGFX_CONFIG_DRAW_INDIRECT_STRIDE, count, BGFX_CONFIG_DRAW_INDIRECT_STRIDE);\n\t\t\t\t\t\t\t\t\tfirst += count;\n\t\t\t\t\t\t\t\t}")
  _njin_occlusion_replace(_vk "${_old}" "${_new}")
endforeach()
if(NOT _vk STREQUAL _vk_before)
  file(WRITE "${_path}" "${_vk}")
endif()
