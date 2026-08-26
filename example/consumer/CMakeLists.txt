cmake_minimum_required(VERSION 3.28)

# ── A downstream project, in miniature ──────────────────────────────────────
# This acceptance test builds against RasterForge the three ways a real project
# might acquire it, and asserts that each
# one yields the same target and drags nothing else along.
#
# Drive it with example/consumer/verify.sh — that script sets the cache
# variables below and runs all three modes. Read it for the copy-paste each mode
# reduces to.
#
# Deliberately NOT under test/: test/CMakeLists.txt globs test/* and would
# auto-discover this as a Catch2 test directory.

project(consumer LANGUAGES CXX)

# The harness is told the package name so its throwaway public-dependency fork
# can use the same consumer without editing this file.
if (NOT RASTERFORGE_PROJECT_NAME)
  message(FATAL_ERROR "pass -DRASTERFORGE_PROJECT_NAME=<RasterForge's project name>")
endif ()

set(CONSUMER_MODE "find_package" CACHE STRING
    "how to acquire the library: add_subdirectory | fetchcontent | find_package")

# ── Mode 1: add_subdirectory ────────────────────────────────────────────────
# A vendored or submoduled copy sitting in your tree.
if (CONSUMER_MODE STREQUAL "add_subdirectory")
  if (NOT RASTERFORGE_SOURCE_DIR)
    message(FATAL_ERROR "add_subdirectory mode needs -DRASTERFORGE_SOURCE_DIR=<path to RasterForge>")
  endif ()

  # EXCLUDE_FROM_ALL is not required — RasterForge's guards already
  # keep its app and tests out of a consumed build — but it is what you would
  # write anyway, and it means a target that *does* leak still costs nothing.
  add_subdirectory(${RASTERFORGE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/_rasterforge EXCLUDE_FROM_ALL)

# ── Mode 2: FetchContent ────────────────────────────────────────────────────
elseif (CONSUMER_MODE STREQUAL "fetchcontent")
  if (NOT RASTERFORGE_GIT_REPOSITORY OR NOT RASTERFORGE_GIT_TAG)
    message(FATAL_ERROR "fetchcontent mode needs -DRASTERFORGE_GIT_REPOSITORY= and -DRASTERFORGE_GIT_TAG=")
  endif ()

  include(FetchContent)

  # ⚠ SOURCE_DIR is load-bearing, not tidiness.
  #
  # Pin the checkout directory to keep diagnostics and generated paths stable
  # across local and CI runs.
  FetchContent_Declare(${RASTERFORGE_PROJECT_NAME}
    GIT_REPOSITORY ${RASTERFORGE_GIT_REPOSITORY}
    GIT_TAG        ${RASTERFORGE_GIT_TAG}
    SOURCE_DIR     ${FETCHCONTENT_BASE_DIR}/${RASTERFORGE_PROJECT_NAME}
  )

  FetchContent_MakeAvailable(${RASTERFORGE_PROJECT_NAME})

# ── Mode 3: installed package ───────────────────────────────────────────────
# `cmake --install`ed somewhere on CMAKE_PREFIX_PATH. CONFIG mode only: there is
# no Find<project>.cmake module and there should not be one — the package ships
# its own config, generated from cmake/project-config.cmake.in.
elseif (CONSUMER_MODE STREQUAL "find_package")
  find_package(${RASTERFORGE_PROJECT_NAME} CONFIG REQUIRED)

else ()
  message(FATAL_ERROR "unknown CONSUMER_MODE '${CONSUMER_MODE}'")
endif ()

# ── What every mode must deliver ────────────────────────────────────────────
# One target, spelled the same way regardless of how it arrived.
if (NOT TARGET ${RASTERFORGE_PROJECT_NAME}::lib)
  message(FATAL_ERROR
    "${CONSUMER_MODE}: no target ${RASTERFORGE_PROJECT_NAME}::lib. "
    "In find_package mode the export namespace or EXPORT_NAME is wrong; in "
    "fetchcontent mode the usual cause is an unpinned SOURCE_DIR renaming the project.")
endif ()

# ...and nothing else: consuming the library must not create top-level tools or tests.
# the demo application, the test framework, and the test targets. Each assertion
# names a specific guard, so a failure points at the line that regressed rather
# than at "something got bigger".
foreach (_leak IN ITEMS
    ${RASTERFORGE_PROJECT_NAME}
    ${RASTERFORGE_PROJECT_NAME}-benchmark
    Catch2
    10image-test)
  if (TARGET ${_leak})
    message(FATAL_ERROR
      "${CONSUMER_MODE}: consuming the library also created the target '${_leak}'. "
      "Check the PROJECT_IS_TOP_LEVEL defaults on <project>_BUILD_BIN / _TESTS and "
      "the component gating of <project>_DEPS in RasterForge's root CMakeLists.")
  endif ()
endforeach ()

add_executable(consumer main.cpp)

# The only line a real consumer writes. No include directories, no C++ standard:
# both arrive as usage requirements of the imported target.
target_link_libraries(consumer PRIVATE ${RASTERFORGE_PROJECT_NAME}::lib)
