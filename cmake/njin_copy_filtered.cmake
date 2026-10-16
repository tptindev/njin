# cmake -DSRC=<dir> -DDST=<dir> -DEXCLUDE=<pattern>|<pattern>... -P njin_copy_filtered.cmake
#
# Copies the folder SRC to DST like `cmake -E copy_directory`, leaving out what
# matches one of the EXCLUDE patterns (separated by |). njin_package() runs it
# for ASSETS when EXCLUDE is given.
#
# A pattern is a path relative to SRC, with * (any characters but /), ** (any
# characters) and ? (one character but /). A pattern that names a folder leaves
# out everything under it. One without a / matches a name at any depth:
#   models/blender    the folder SRC/models/blender
#   *.md              every .md file, at any depth
#   models/*/backups  backups one folder below models
cmake_minimum_required(VERSION 3.21)

if(NOT SRC OR NOT DST)
  message(FATAL_ERROR "njin_copy_filtered: SRC and DST are required")
endif()

string(REPLACE "|" ";" patterns "${EXCLUDE}")
set(regexes "")
foreach(p IN LISTS patterns)
  string(STRIP "${p}" p)
  string(REGEX REPLACE "/+$" "" p "${p}")
  if(p STREQUAL "")
    continue()
  endif()
  string(FIND "${p}" "/" slash)
  # Escape the regex characters, then turn the glob characters into regex.
  string(REGEX REPLACE "([][.+^$(){}|\\\\])" "\\\\\\1" r "${p}")
  string(REPLACE "**" "@@ANY@@" r "${r}")
  string(REPLACE "*" "[^/]*" r "${r}")
  string(REPLACE "@@ANY@@" ".*" r "${r}")
  string(REPLACE "?" "[^/]" r "${r}")
  if(slash EQUAL -1)
    list(APPEND regexes "(^|/)${r}(/.*)?$")
  else()
    list(APPEND regexes "^${r}(/.*)?$")
  endif()
endforeach()

file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE "${SRC}" "${SRC}/*")
set(copied 0)
set(skipped 0)
foreach(f IN LISTS files)
  set(skip FALSE)
  foreach(r IN LISTS regexes)
    if(f MATCHES "${r}")
      set(skip TRUE)
      break()
    endif()
  endforeach()
  if(skip)
    math(EXPR skipped "${skipped} + 1")
  else()
    cmake_path(GET f PARENT_PATH dir)
    file(MAKE_DIRECTORY "${DST}/${dir}")
    file(COPY_FILE "${SRC}/${f}" "${DST}/${f}" ONLY_IF_DIFFERENT)
    math(EXPR copied "${copied} + 1")
  endif()
endforeach()
message(STATUS "njin_copy_filtered: ${copied} files copied, ${skipped} left out")
