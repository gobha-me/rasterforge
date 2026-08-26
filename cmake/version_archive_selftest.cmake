# Integration test for version discovery from a real git archive. The archive
# is deliberately configured inside the caller's build tree, which is itself
# beneath the live checkout: this also proves version.cmake refuses to inherit
# tags from an unrelated enclosing repository.

cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED SOURCE_DIR OR SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "version archive self-test requires -DSOURCE_DIR=<repository>")
endif()
if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
  message(FATAL_ERROR "version archive self-test requires -DWORK_DIR=<scratch directory>")
endif()

cmake_path(ABSOLUTE_PATH SOURCE_DIR NORMALIZE OUTPUT_VARIABLE _source)
cmake_path(ABSOLUTE_PATH WORK_DIR NORMALIZE OUTPUT_VARIABLE _work)
cmake_path(GET _work FILENAME _work_name)
if(NOT _work_name STREQUAL "version-archive-selftest")
  message(FATAL_ERROR
    "WORK_DIR must end in version-archive-selftest; refusing unsafe cleanup of '${_work}'")
endif()

find_package(Git QUIET)
if(NOT GIT_FOUND)
  message(STATUS "version archive self-test: skipped because Git is unavailable")
  return()
endif()

execute_process(
  COMMAND ${GIT_EXECUTABLE} rev-parse --show-toplevel
  WORKING_DIRECTORY ${_source}
  RESULT_VARIABLE _root_result
  OUTPUT_VARIABLE _git_root
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT _root_result EQUAL 0)
  message(STATUS "version archive self-test: skipped outside a Git checkout")
  return()
endif()
file(REAL_PATH "${_git_root}" _git_root)
if(NOT _git_root STREQUAL _source)
  message(STATUS
    "version archive self-test: skipped because the source does not own the enclosing repository")
  return()
endif()

file(REMOVE_RECURSE "${_work}")
file(MAKE_DIRECTORY "${_work}/repository")

# Snapshot tracked files plus new, non-ignored files so the test exercises the
# working tree that is about to be committed rather than the previous HEAD.
execute_process(
  COMMAND ${GIT_EXECUTABLE} ls-files --cached --others --exclude-standard
  WORKING_DIRECTORY ${_source}
  RESULT_VARIABLE _files_result
  OUTPUT_VARIABLE _files_output
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_VARIABLE _files_error
)
if(NOT _files_result EQUAL 0 OR _files_output STREQUAL "")
  message(FATAL_ERROR "could not enumerate source files: ${_files_error}")
endif()
string(REPLACE "\n" ";" _files "${_files_output}")
foreach(_relative IN LISTS _files)
  if(IS_DIRECTORY "${_source}/${_relative}")
    continue()
  endif()
  cmake_path(GET _relative PARENT_PATH _parent)
  file(MAKE_DIRECTORY "${_work}/repository/${_parent}")
  file(COPY_FILE "${_source}/${_relative}"
                 "${_work}/repository/${_relative}")
endforeach()

function(_run_fixture_git LABEL)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} ${ARGN}
    WORKING_DIRECTORY ${_work}/repository
    RESULT_VARIABLE _result
    ERROR_VARIABLE _error
  )
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "could not ${LABEL} archive fixture: ${_error}")
  endif()
endfunction()

_run_fixture_git("initialize" init -q -b main)
_run_fixture_git("stage" add -A)
_run_fixture_git("commit" -c user.email=verify@example.invalid
                         -c user.name=verify commit -qm "working tree snapshot")
_run_fixture_git("tag" tag v9.8.7)

execute_process(
  COMMAND ${GIT_EXECUTABLE} archive --format=tar --prefix=source/
          --output=${_work}/source.tar v9.8.7
  WORKING_DIRECTORY ${_work}/repository
  RESULT_VARIABLE _archive_result
  ERROR_VARIABLE _archive_error
)
if(NOT _archive_result EQUAL 0)
  message(FATAL_ERROR "git archive failed: ${_archive_error}")
endif()
execute_process(
  COMMAND ${CMAKE_COMMAND} -E tar xf source.tar
  WORKING_DIRECTORY ${_work}
  RESULT_VARIABLE _extract_result
  ERROR_VARIABLE _extract_error
)
if(NOT _extract_result EQUAL 0)
  message(FATAL_ERROR "archive extraction failed: ${_extract_error}")
endif()
if(EXISTS "${_work}/source/.git")
  message(FATAL_ERROR "archive unexpectedly contains repository metadata")
endif()
file(READ "${_work}/source/.git_archival.txt" _archival)
if(NOT _archival MATCHES
    "(^|\n)describe-name:[ \t]*v9\\.8\\.7([ \t]*\n|$)")
  message(FATAL_ERROR "git archive did not substitute the expected tag metadata")
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND}
          -S ${_work}/source
          -B ${_work}/configured
          -Drasterforge_BUILD_LIB=OFF
          -Drasterforge_BUILD_BIN=OFF
          -Drasterforge_TESTS=OFF
          -Drasterforge_FUZZERS=OFF
          -Drasterforge_BENCHMARKS=OFF
          -Drasterforge_INSTALL=OFF
  RESULT_VARIABLE _configure_result
  OUTPUT_VARIABLE _configure_output
  ERROR_VARIABLE _configure_error
)
if(NOT _configure_result EQUAL 0)
  message(FATAL_ERROR
    "archived source did not configure:\n${_configure_output}\n${_configure_error}")
endif()

file(READ "${_work}/configured/generated/rasterforge/version.hpp" _header)
foreach(_field IN ITEMS "major;9" "minor;8" "patch;7" "tweak;0")
  list(GET _field 0 _member)
  list(GET _field 1 _expected)
  if(NOT _header MATCHES
      "inline constexpr std::uint32_t ${_member}\\{${_expected}\\};")
    message(FATAL_ERROR
      "archive header does not contain expected ${_member}=${_expected}")
  endif()
endforeach()
if(NOT _header MATCHES "inline constexpr bool dirty\\{0\\};")
  message(FATAL_ERROR "archive header must report dirty=false")
endif()

message(STATUS
  "version archive self-test: v9.8.7 survived export without .git")
