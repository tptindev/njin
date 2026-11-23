set(renderer_arguments)
if(DEFINED RENDERER)
  list(APPEND renderer_arguments "--renderer=${RENDERER}")
endif()
execute_process(COMMAND "${EXE}" ${renderer_arguments} --frames=1
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if(result EQUAL 0 OR NOT "${output}\n${errors}" MATCHES "${EXPECTED}")
  message(FATAL_ERROR "Expected renderer failure '${EXPECTED}', got ${result}\n${output}\n${errors}")
endif()
