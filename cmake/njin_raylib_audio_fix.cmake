# Stop/join the audio callbacks before destroying the mixer mutex they use.
# Android's AAudio callback exposed this race in the pinned raylib 6.0 source.
set(_njin_raudio "${raylib_SOURCE_DIR}/src/raudio.c")
file(READ "${_njin_raudio}" _njin_raudio_source)
if(NOT _njin_raudio_source MATCHES "NJIN: join audio callbacks before destroying")
  set(_njin_audio_old "        ma_mutex_uninit(&AUDIO.System.lock);\n        ma_device_uninit(&AUDIO.System.device);")
  set(_njin_audio_new "        // NJIN: join audio callbacks before destroying their mixer lock.\n        ma_device_uninit(&AUDIO.System.device);\n        ma_mutex_uninit(&AUDIO.System.lock);")
  string(FIND "${_njin_raudio_source}" "${_njin_audio_old}" _njin_audio_pos)
  if(_njin_audio_pos EQUAL -1)
    message(FATAL_ERROR "raylib audio teardown changed upstream; review njin_raylib_audio_fix.cmake")
  endif()
  string(REPLACE "${_njin_audio_old}" "${_njin_audio_new}" _njin_raudio_source "${_njin_raudio_source}")
  file(WRITE "${_njin_raudio}" "${_njin_raudio_source}")
endif()
