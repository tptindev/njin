# Hardware ray queries on bgfx Vulkan (light3d::ray_traced_*, irradiance3d_desc::ray_traced).
# The pinned bgfx neither enables VK_KHR_acceleration_structure/VK_KHR_ray_query nor lets a
# caller reach its device, queue or a texture's VkImage, and its shader compiler cannot emit
# ray queries. This narrow, reproducible patch adds, without changing what bgfx already does:
#   - the ray query extensions and features (enabled only when the whole set is supported
#     and njin asked for them before bgfx::init),
#   - bgfx::njinVkInternals / njinVkTexture: the native handles and a texture's image/layout,
#   - bgfx::njinVkSetDispatchHook: a compute dispatch of a chosen program is handed to njin,
#     inside bgfx's own command buffer (after bgfx bound and transitioned its resources), so
#     njin's ray query kernel runs exactly where the compute kernel would have.
# Reviewed against bgfx.cmake 1af3832b87ad0588c05f1f0bd7ee17a7b14d2315.
set(_njin_rq_cpp "${NJIN_BGFX_SOURCE_DIR}/src/renderer_vk.cpp")
file(READ "${_njin_rq_cpp}" _njin_rq_source)
if(NOT _njin_rq_source MATCHES "NJIN: hardware ray queries")
  function(_njin_rq_replace old new what)
    string(FIND "${_njin_rq_source}" "${old}" _pos)
    if(_pos EQUAL -1)
      message(FATAL_ERROR "bgfx ${what} changed upstream; review njin_bgfx_rayquery_fix.cmake")
    endif()
    string(REPLACE "${old}" "${new}" _out "${_njin_rq_source}")
    set(_njin_rq_source "${_out}" PARENT_SCOPE)
  endfunction()

  # 1. The extensions (enum and registry stay in the same order).
  _njin_rq_replace("\t\t\tNV_low_latency2,\n"
    "\t\t\tNV_low_latency2,\n\t\t\tKHR_acceleration_structure, // NJIN: hardware ray queries\n\t\t\tKHR_ray_query,\n\t\t\tKHR_deferred_host_operations,\n\t\t\tKHR_buffer_device_address,\n\t\t\tKHR_spirv_1_4,\n\t\t\tKHR_shader_float_controls,\n\t\t\tKHR_push_descriptor,\n"
    "extension enum")
  _njin_rq_replace("\t\t{ \"VK_NV_low_latency2\",                     2, false, false, false,                                                         Layer::Count },\n"
    "\t\t{ \"VK_NV_low_latency2\",                     2, false, false, false,                                                         Layer::Count },\n\t\t{ \"VK_KHR_acceleration_structure\",         1, false, false, true, Layer::Count },\n\t\t{ \"VK_KHR_ray_query\",                       1, false, false, true, Layer::Count },\n\t\t{ \"VK_KHR_deferred_host_operations\",        1, false, false, true, Layer::Count },\n\t\t{ \"VK_KHR_buffer_device_address\",           1, false, false, true, Layer::Count },\n\t\t{ \"VK_KHR_spirv_1_4\",                       1, false, false, true, Layer::Count },\n\t\t{ \"VK_KHR_shader_float_controls\",           1, false, false, true, Layer::Count },\n\t\t{ \"VK_KHR_push_descriptor\",                 1, false, false, true, Layer::Count },\n"
    "extension registry")

  # 2. State shared with njin.
  _njin_rq_replace("\t// Extension registry\n\t//\n\tstatic Extension s_extension[] =\n"
    "\t// NJIN: hardware ray queries (see the end of this file).\n\ttypedef bool (*NjinVkDispatchFn)(void* _user, VkCommandBuffer _cmd, uint16_t _program, uint32_t _numX, uint32_t _numY, uint32_t _numZ);\n\tstatic bool s_njinRayQueryWanted = false;\n\tstatic bool s_njinRayQuery = false;\n\tstatic NjinVkDispatchFn s_njinDispatch = NULL;\n\tstatic void* s_njinDispatchUser = NULL;\n\tstatic VkPhysicalDeviceAccelerationStructureFeaturesKHR s_njinAccelerationFeatures;\n\tstatic VkPhysicalDeviceRayQueryFeaturesKHR s_njinRayQueryFeatures;\n\tstatic VkPhysicalDeviceBufferDeviceAddressFeatures s_njinAddressFeatures;\n\n\t// Extension registry\n\t//\n\tstatic Extension s_extension[] =\n"
    "extension registry header")

  # 3. Features, chained only when the whole set is there.
  _njin_rq_replace("\t\t\t\tm_deviceFeatures =\n\t\t\t\t{\n"
    "\t\t\t\t{\n\t\t\t\t\tstatic const Extension::Enum njinSet[] = { Extension::KHR_acceleration_structure, Extension::KHR_ray_query, Extension::KHR_deferred_host_operations, Extension::KHR_buffer_device_address, Extension::KHR_spirv_1_4, Extension::KHR_shader_float_controls, Extension::KHR_push_descriptor };\n\t\t\t\t\tbool njinRq = true\n\t\t\t\t\t\t&& s_njinRayQueryWanted\n\t\t\t\t\t\t&& NULL == g_platformData.context\n\t\t\t\t\t\t&& VK_API_VERSION_1_2 <= m_instanceApiVersion\n\t\t\t\t\t\t&& VK_API_VERSION_1_2 <= m_deviceProperties.apiVersion\n\t\t\t\t\t\t&& NULL != vkGetPhysicalDeviceFeatures2KHR\n\t\t\t\t\t\t;\n\t\t\t\t\tfor (Extension::Enum ext : njinSet) { njinRq = njinRq && s_extension[ext].m_supported; }\n\t\t\t\t\tif (njinRq)\n\t\t\t\t\t{\n\t\t\t\t\t\tbx::memSet(&s_njinAccelerationFeatures, 0, sizeof(s_njinAccelerationFeatures) );\n\t\t\t\t\t\tbx::memSet(&s_njinRayQueryFeatures, 0, sizeof(s_njinRayQueryFeatures) );\n\t\t\t\t\t\tbx::memSet(&s_njinAddressFeatures, 0, sizeof(s_njinAddressFeatures) );\n\t\t\t\t\t\ts_njinAccelerationFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;\n\t\t\t\t\t\ts_njinRayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;\n\t\t\t\t\t\ts_njinAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;\n\t\t\t\t\t\ts_njinAccelerationFeatures.pNext = &s_njinRayQueryFeatures;\n\t\t\t\t\t\ts_njinRayQueryFeatures.pNext = &s_njinAddressFeatures;\n\t\t\t\t\t\tVkPhysicalDeviceFeatures2KHR njinQuery;\n\t\t\t\t\t\tnjinQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR;\n\t\t\t\t\t\tnjinQuery.pNext = &s_njinAccelerationFeatures;\n\t\t\t\t\t\tvkGetPhysicalDeviceFeatures2KHR(m_physicalDevice, &njinQuery);\n\t\t\t\t\t\tnjinRq = s_njinAccelerationFeatures.accelerationStructure && s_njinRayQueryFeatures.rayQuery && s_njinAddressFeatures.bufferDeviceAddress;\n\t\t\t\t\t}\n\t\t\t\t\tif (njinRq)\n\t\t\t\t\t{\n\t\t\t\t\t\tbx::memSet(&s_njinAccelerationFeatures, 0, sizeof(s_njinAccelerationFeatures) );\n\t\t\t\t\t\tbx::memSet(&s_njinRayQueryFeatures, 0, sizeof(s_njinRayQueryFeatures) );\n\t\t\t\t\t\tbx::memSet(&s_njinAddressFeatures, 0, sizeof(s_njinAddressFeatures) );\n\t\t\t\t\t\ts_njinAccelerationFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;\n\t\t\t\t\t\ts_njinAccelerationFeatures.accelerationStructure = VK_TRUE;\n\t\t\t\t\t\ts_njinRayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;\n\t\t\t\t\t\ts_njinRayQueryFeatures.rayQuery = VK_TRUE;\n\t\t\t\t\t\ts_njinRayQueryFeatures.pNext = &s_njinAccelerationFeatures;\n\t\t\t\t\t\ts_njinAccelerationFeatures.pNext = (void*)nextFeatures;\n\t\t\t\t\t\tnextFeatures = &s_njinRayQueryFeatures;\n\t\t\t\t\t\tif (vulkan12FeaturesChained)\n\t\t\t\t\t\t{\n\t\t\t\t\t\t\tvulkan12Features.bufferDeviceAddress = VK_TRUE;\n\t\t\t\t\t\t}\n\t\t\t\t\t\telse\n\t\t\t\t\t\t{\n\t\t\t\t\t\t\ts_njinAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;\n\t\t\t\t\t\t\ts_njinAddressFeatures.bufferDeviceAddress = VK_TRUE;\n\t\t\t\t\t\t\ts_njinAddressFeatures.pNext = (void*)nextFeatures;\n\t\t\t\t\t\t\tnextFeatures = &s_njinAddressFeatures;\n\t\t\t\t\t\t}\n\t\t\t\t\t}\n\t\t\t\t\tfor (Extension::Enum ext : njinSet) { s_extension[ext].m_supported = njinRq; }\n\t\t\t\t\ts_njinRayQuery = njinRq;\n\t\t\t\t}\n\n\t\t\t\tm_deviceFeatures =\n\t\t\t\t{\n"
    "device features")

  # 4. The dispatch hook.
  _njin_rq_replace("\t\t\t\t\telse\n\t\t\t\t\t{\n\t\t\t\t\t\tvkCmdDispatch(m_commandBuffer, compute.m_numX, compute.m_numY, compute.m_numZ);\n\t\t\t\t\t}\n"
    "\t\t\t\t\telse if (NULL != s_njinDispatch\n\t\t\t\t\t&&  s_njinDispatch(s_njinDispatchUser, m_commandBuffer, key.m_program.idx, compute.m_numX, compute.m_numY, compute.m_numZ) )\n\t\t\t\t\t{\n\t\t\t\t\t\t// NJIN: njin bound its own pipeline and descriptors; rebind bgfx's next time.\n\t\t\t\t\t\tcurrentPipeline = VK_NULL_HANDLE;\n\t\t\t\t\t\tcurrentProgram  = BGFX_INVALID_HANDLE;\n\t\t\t\t\t\tcurrentBindHash = 0;\n\t\t\t\t\t}\n\t\t\t\t\telse\n\t\t\t\t\t{\n\t\t\t\t\t\tvkCmdDispatch(m_commandBuffer, compute.m_numX, compute.m_numY, compute.m_numZ);\n\t\t\t\t\t}\n"
    "compute dispatch")

  # 5. The entry points njin calls (declared in njin's njin_bgfx_vk.h).
  _njin_rq_replace("} /* namespace vk */ } // namespace bgfx\n\n#else\n"
    "} /* namespace vk */ } // namespace bgfx\n\nnamespace bgfx\n{\n\tstruct NjinVkInternals\n\t{\n\t\tvoid* instance;\n\t\tvoid* physicalDevice;\n\t\tvoid* device;\n\t\tvoid* queue;\n\t\tuint32_t queueFamily;\n\t\tvoid* getInstanceProcAddr;\n\t\tbool rayQuery;\n\t};\n\n\tvoid njinVkWantRayQuery(bool _want) { vk::s_njinRayQueryWanted = _want; }\n\n\tbool njinVkInternals(NjinVkInternals& _out)\n\t{\n\t\tif (NULL == vk::s_renderVK) { return false; }\n\t\t_out.instance            = (void*)vk::s_renderVK->m_instance;\n\t\t_out.physicalDevice      = (void*)vk::s_renderVK->m_physicalDevice;\n\t\t_out.device              = (void*)vk::s_renderVK->m_device;\n\t\t_out.queue               = (void*)vk::s_renderVK->m_globalQueue;\n\t\t_out.queueFamily         = vk::s_renderVK->m_globalQueueFamily;\n\t\t_out.getInstanceProcAddr = (void*)vk::vkGetInstanceProcAddr;\n\t\t_out.rayQuery            = vk::s_njinRayQuery;\n\t\treturn true;\n\t}\n\n\tbool njinVkTexture(TextureHandle _handle, uint64_t& _image, uint32_t& _layout, uint32_t& _format)\n\t{\n\t\tif (NULL == vk::s_renderVK || !isValid(_handle) ) { return false; }\n\t\tconst vk::TextureVK& texture = vk::s_renderVK->m_textures[_handle.idx];\n\t\tconst bool msaa = VK_NULL_HANDLE != texture.m_singleMsaaImage;\n\t\tconst VkImage image = msaa ? texture.m_singleMsaaImage : texture.m_textureImage;\n\t\tbx::memCopy(&_image, &image, sizeof(VkImage) );\n\t\t_layout = uint32_t(msaa ? texture.m_currentSingleMsaaImageLayout : texture.m_currentImageLayout);\n\t\t_format = uint32_t(texture.m_format);\n\t\treturn VK_NULL_HANDLE != image;\n\t}\n\n\tvoid njinVkSetDispatchHook(void* _fn, void* _user)\n\t{\n\t\tvk::s_njinDispatch     = (vk::NjinVkDispatchFn)_fn;\n\t\tvk::s_njinDispatchUser = _user;\n\t}\n} // namespace bgfx\n\n#else\n"
    "file end")
  _njin_rq_replace("\tvoid rendererDestroy()\n\t{\n\t}\n} /* namespace vk */ } // namespace bgfx\n\n#endif // BGFX_CONFIG_RENDERER_VULKAN"
    "\tvoid rendererDestroy()\n\t{\n\t}\n} /* namespace vk */ } // namespace bgfx\n\nnamespace bgfx\n{\n\t// NJIN: hardware ray queries need the Vulkan renderer.\n\tstruct NjinVkInternals;\n\tvoid njinVkWantRayQuery(bool) {}\n\tbool njinVkInternals(NjinVkInternals&) { return false; }\n\tbool njinVkTexture(TextureHandle, uint64_t&, uint32_t&, uint32_t&) { return false; }\n\tvoid njinVkSetDispatchHook(void*, void*) {}\n} // namespace bgfx\n\n#endif // BGFX_CONFIG_RENDERER_VULKAN"
    "file end stub")
  file(WRITE "${_njin_rq_cpp}" "${_njin_rq_source}")
endif()
