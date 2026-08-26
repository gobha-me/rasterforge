cmake_minimum_required(VERSION 3.28)

# Toolchain defaulting, top-level only. A toolchain file has to be set before the
# first project() call in the whole build, so this cannot use PROJECT_IS_TOP_LEVEL
# — that variable does not exist until project() runs, a dozen lines down. This is
# the same test, spelled the only way that works this early.
#
# When this project is consumed (add_subdirectory / FetchContent) the consumer
# owns the toolchain, and setting one here would be dead weight at best.
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/cmake/toolchain/default.cmake)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmake/version.cmake)

project(rasterforge
  VERSION ${VERSION}
  DESCRIPTION "Safe in-memory raster decoding and pixel transforms"
  HOMEPAGE_URL "https://github.com/gobha-me/rasterforge"
  LANGUAGES CXX
)

# CMAKE_INSTALL_{INCLUDE,LIB,BIN}DIR, used by src/lib and cmake/install.cmake.
include(GNUInstallDirs)

# PROJECT_VERSION, not CMAKE_PROJECT_VERSION: the CMAKE_ prefixed variable holds
# the *top-level* project's version, which is someone else's when we are consumed.
message(STATUS "${PROJECT_NAME}:${PROJECT_VERSION} (tweak=${VERSION_TWEAK} dirty=${VERSION_DIRTY} top_level=${PROJECT_IS_TOP_LEVEL})")

set(VERSION_HEADER_IN "${CMAKE_CURRENT_LIST_DIR}/include/rasterforge/version.hpp.in.cmake")
set(VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/rasterforge/version.hpp")
configure_file(${VERSION_HEADER_IN} ${VERSION_HEADER} @ONLY)

# ── What gets built ─────────────────────────────────────────────────────────
# Everything except the library defaults to PROJECT_IS_TOP_LEVEL, so a project
# that consumes this one via add_subdirectory() or FetchContent gets the library
# and nothing else — no demo executable, no test suite, and no install rules
# landing in someone else's `cmake --install`. Building this repo directly
# keeps the developer components enabled.
#
# The library itself defaults ON in src/lib/CMakeLists.txt — it is the thing
# worth consuming.
#
# Declared here, above the dependency list, because that list is gated on them.
option(${PROJECT_NAME}_BUILD_LIB "Build the RasterForge library" ON)
option(${PROJECT_NAME}_BUILD_BIN "Build the executable in src/bin"    ${PROJECT_IS_TOP_LEVEL})
option(${PROJECT_NAME}_TESTS     "Build and register the test suite"  ${PROJECT_IS_TOP_LEVEL})
option(${PROJECT_NAME}_FUZZERS   "Build the Clang/libFuzzer targets"   ${PROJECT_IS_TOP_LEVEL})
option(${PROJECT_NAME}_BENCHMARKS "Build the developer benchmark harness" ${PROJECT_IS_TOP_LEVEL})
option(${PROJECT_NAME}_INSTALL   "Generate install / package-config rules" ${PROJECT_IS_TOP_LEVEL})
option(${PROJECT_NAME}_FORCE_FETCH_DEPS
  "Skip system packages and use RasterForge's pinned dependency fallbacks"
  OFF
)

# Dependencies this project uses. Each name maps to cmake/deps/<name>.cmake
# (find_package first, FetchContent fallback). This is the prune point for a new
# project: drop a name to stop fetching that dep.
set(${PROJECT_NAME}_DEPS
  zlib    # Compression backend required by libpng
  png     # Production PNG codec selected by ADR 0001
  jpeg    # JPEG codec with a libjpeg-turbo system/fallback path
  webp    # Static WebP decoder selected by ADR 0011
  catch2  # Catch2 v3 — test framework (test/)
)

# Component gating: a dependency that exists only to serve a target we are not
# building does not get fetched. Without this, consuming the library alone still
# downloads a test framework.
#
# The list above stays the single prune point — these lines only ever REMOVE
# names from it, never add one. Removing a name that is already gone is a no-op,
# so a project that prunes the list can leave these alone.
if (NOT ${PROJECT_NAME}_BUILD_LIB)
  list(REMOVE_ITEM ${PROJECT_NAME}_DEPS zlib png jpeg webp)
endif ()

if (NOT ${PROJECT_NAME}_TESTS)
  list(REMOVE_ITEM ${PROJECT_NAME}_DEPS catch2)
endif ()

include(${CMAKE_CURRENT_LIST_DIR}/cmake/dependencies.cmake)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/src/lib)

if (${PROJECT_NAME}_BUILD_BIN)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/src/bin)
endif ()

if (${PROJECT_NAME}_TESTS)
  include(CTest)
  enable_testing()

  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/test)
endif (${PROJECT_NAME}_TESTS)

# Fuzz targets are developer-only and EXCLUDE_FROM_ALL. The option follows the
# same top-level default as tests, while consumers get neither targets nor
# compiler instrumentation through add_subdirectory / FetchContent.
if (${PROJECT_NAME}_FUZZERS)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/fuzz)
endif ()

# Benchmarks are developer evidence, not correctness tests or installable
# consumer tools. They build by default only for this top-level project and are
# deliberately absent from CTest.
if (${PROJECT_NAME}_BENCHMARKS)
  if (NOT ${PROJECT_NAME}_BUILD_LIB)
    message(FATAL_ERROR
      "${PROJECT_NAME}_BENCHMARKS requires ${PROJECT_NAME}_BUILD_LIB=ON")
  endif ()
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/benchmark)
endif ()

# Install rules, package config and the exported target set. Replaces the
# long-standing "TODO Install Template": find_package(<project> CONFIG) is real
# now, and works against an install tree.
if (${PROJECT_NAME}_INSTALL)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/install.cmake)
endif ()
