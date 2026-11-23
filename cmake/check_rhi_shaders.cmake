# Exercise the same compiler and sources as the embedded Vulkan shader build.
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
foreach(stage vert frag)
  execute_process(COMMAND "${GLSLC}" "${SOURCE_DIR}/slice.${stage}"
    -o "${OUTPUT_DIR}/valid.${stage}.spv"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Valid ${stage} compilation failed: ${output}\n${errors}")
  endif()
endforeach()
file(WRITE "${OUTPUT_DIR}/invalid.vert" "#version 450\nvoid main() { invalid shader syntax; }\n")
foreach(input "${OUTPUT_DIR}/invalid.vert" "${OUTPUT_DIR}/missing.vert")
  execute_process(COMMAND "${GLSLC}" "${input}" -o "${OUTPUT_DIR}/rejected.spv"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(result EQUAL 0 OR "${errors}" STREQUAL "")
    message(FATAL_ERROR "Invalid/missing shader did not fail with a diagnostic: ${input}")
  endif()
endforeach()
