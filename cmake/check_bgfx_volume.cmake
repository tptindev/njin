if(NOT EXISTS "${BINARY}")
  message(FATAL_ERROR "Volume shader binary missing: ${BINARY}")
endif()
file(READ "${BINARY}" _njin_volume HEX)
# The shaderc uniform table must retain the inverse matrix. A comma-grouped
# matrix declaration lost it after D3D optimization when the first was unused.
foreach(_njin_uniform u_njinPostInverse u_njinLegacyPost s_njinPostAux)
  string(HEX "${_njin_uniform}" _njin_name)
  string(FIND "${_njin_volume}" "${_njin_name}" _njin_found)
  if(_njin_found EQUAL -1)
    message(FATAL_ERROR "Volume shader lost required binding ${_njin_uniform}: ${BINARY}")
  endif()
endforeach()
