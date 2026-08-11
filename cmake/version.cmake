# Derive the project version from git tags — no commits needed just to roll a
# version. A git archive has no repository metadata, so .git_archival.txt carries
# the same describe string through export-subst. This runs BEFORE project() in
# the root CMakeLists, so the VERSION_* vars it sets are in scope for both
# project(VERSION ...) and the configure_file that generates the build-tree
# rasterforge/version.hpp.
#
# The actual string parsing lives in version_parse.cmake (pure, no git) so it can
# be unit-tested via `cmake -P cmake/version_selftest.cmake`.

include(${CMAKE_CURRENT_LIST_DIR}/version_parse.cmake)  # absolute: no CMAKE_MODULE_PATH here

find_package(Git)

cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH _version_source_root)
file(REAL_PATH "${_version_source_root}" VERSION_SOURCE_ROOT)

set(GIT_TAG_STR "")
set(GIT_RESULT 1)
set(GIT_ROOT_RESULT 1)
set(GIT_ROOT "")

if(GIT_FOUND)
  # Do not inherit tags from an unrelated parent repository when RasterForge is
  # vendored or an archive is unpacked beneath another checkout. Only a .git
  # worktree whose root is this source tree owns the live version metadata.
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --show-toplevel
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    RESULT_VARIABLE GIT_ROOT_RESULT
    OUTPUT_VARIABLE GIT_ROOT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )

  if(GIT_ROOT_RESULT EQUAL 0)
    file(REAL_PATH "${GIT_ROOT}" GIT_ROOT)
  endif()

  if(GIT_ROOT_RESULT EQUAL 0 AND GIT_ROOT STREQUAL VERSION_SOURCE_ROOT)
    # Nearest tag, plus git's -<N>-g<hash> (commits since tag) and -dirty.
    execute_process(
      COMMAND ${GIT_EXECUTABLE} describe --tags --dirty
      WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
      RESULT_VARIABLE GIT_RESULT
      OUTPUT_VARIABLE GIT_TAG_STR
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
  endif()
endif()

parse_git_describe("${GIT_TAG_STR}" GITVER)

set(ARCHIVE_METADATA "")
if(EXISTS "${VERSION_SOURCE_ROOT}/.git_archival.txt")
  file(READ "${VERSION_SOURCE_ROOT}/.git_archival.txt" ARCHIVE_METADATA)
endif()
parse_git_archival("${ARCHIVE_METADATA}" ARCHIVEVER)

if(GITVER_MATCHED)
  set(VERSION "${GITVER_MAJOR}.${GITVER_MINOR}.${GITVER_PATCH}")
  set(VERSION_MAJOR ${GITVER_MAJOR})
  set(VERSION_MINOR ${GITVER_MINOR})
  set(VERSION_PATCH ${GITVER_PATCH})
  set(VERSION_TWEAK ${GITVER_TWEAK})
  set(VERSION_DIRTY ${GITVER_DIRTY})
elseif(ARCHIVEVER_MATCHED)
  set(VERSION "${ARCHIVEVER_MAJOR}.${ARCHIVEVER_MINOR}.${ARCHIVEVER_PATCH}")
  set(VERSION_MAJOR ${ARCHIVEVER_MAJOR})
  set(VERSION_MINOR ${ARCHIVEVER_MINOR})
  set(VERSION_PATCH ${ARCHIVEVER_PATCH})
  set(VERSION_TWEAK ${ARCHIVEVER_TWEAK})
  set(VERSION_DIRTY ${ARCHIVEVER_DIRTY})
  message(STATUS
    "version: using archive description '${ARCHIVEVER_DESCRIBE}'")
else()
  # Fallback: three clean components (not the old 0.0.0.1 sentinel), with every
  # var set concretely so the generated header never gets an empty substitution.
  # 0.0.0 is also a legal tag, so the reason lives in the STATUS message below.
  set(VERSION 0.0.0)
  set(VERSION_MAJOR 0)
  set(VERSION_MINOR 0)
  set(VERSION_PATCH 0)
  set(VERSION_TWEAK 0)
  set(VERSION_DIRTY 0)

  if(NOT GIT_FOUND)
    message(STATUS "version: git and usable archive metadata not found; using ${VERSION}")
  elseif(NOT GIT_ROOT_RESULT EQUAL 0)
    message(STATUS "version: no repository or usable archive metadata found; using ${VERSION}")
  elseif(NOT GIT_ROOT STREQUAL VERSION_SOURCE_ROOT)
    message(STATUS
      "version: source tree does not own the enclosing repository and has no usable archive metadata; using ${VERSION}")
  elseif(NOT GIT_RESULT EQUAL 0)
    message(STATUS "version: no git tags or usable archive metadata found; using ${VERSION}")
  else()
    message(STATUS
      "version: tag '${GIT_TAG_STR}' and archive metadata are not MAJOR.MINOR.PATCH; using ${VERSION}")
  endif()
endif()
