file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(WRITE "${OUTPUT_DIR}/invalid.sc" "$input a_position\n#include <bgfx_shader.sh>\nvoid main() { invalid shader syntax; }\n")
foreach(input "${OUTPUT_DIR}/invalid.sc" "${OUTPUT_DIR}/missing.sc")
  execute_process(COMMAND "${SHADERC}" -f "${input}" -o "${OUTPUT_DIR}/rejected.bin"
    --type vertex --platform linux -p 430 -i "${BGFX_SRC}/src"
    --varyingdef "${VARYING}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
  if(result EQUAL 0 OR "${output}\n${errors}" STREQUAL "")
    message(FATAL_ERROR "shaderc did not reject invalid/missing input: ${input}\n${output}\n${errors}")
  endif()
endforeach()
