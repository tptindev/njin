# Runs a whole sample game for FRAMES frames on RENDERER from its own folder (assets are
# relative to it) and fails unless it exits cleanly and drew a non-blank frame.
# cmake -DEXE=... -DWORKDIR=... -DRENDERER=... -DFRAMES=... -DSHOT=... -DLOG=... -P check_example_smoke.cmake
if(NOT DEFINED FRAMES)
  set(FRAMES 90)
endif()
if(NOT DEFINED SMOKE_TIMEOUT)
  set(SMOKE_TIMEOUT 150)
endif()
file(REMOVE "${SHOT}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "NJIN_SMOKE_BACKEND=${RENDERER}" "NJIN_SMOKE_FRAMES=${FRAMES}"
    "NJIN_SMOKE_SCREENSHOT=${SHOT}" "${EXE}"
  WORKING_DIRECTORY "${WORKDIR}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT ${SMOKE_TIMEOUT})
if(DEFINED LOG)
  file(WRITE "${LOG}" "${output}\n${errors}")
endif()
if(NOT result EQUAL 0)
  message(FATAL_ERROR "sample did not exit cleanly (${result})\n${errors}")
endif()
if("${output}\n${errors}" MATCHES "[Vv]alidation [Ee]rror|VUID-")
  message(FATAL_ERROR "GPU validation diagnostics detected\n${errors}")
endif()
# BLANK_OK: a sample that only clears the screen (sandbox) must still run to the end.
set(expected "NJIN_SMOKE ok")
if(BLANK_OK)
  set(expected "NJIN_SMOKE (ok|blank)")
endif()
if(NOT "${output}\n${errors}" MATCHES "${expected}")
  string(REGEX MATCH "NJIN_SMOKE[^\n]*" line "${output}\n${errors}")
  message(FATAL_ERROR "sample drew no usable frame: ${line}")
endif()
