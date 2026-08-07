# zlib is a direct link dependency so RasterForge's static install export is
# complete even when libpng is fetched. Prefer a maintained system package;
# the explicit switch exists for deterministic fallback verification.

if (NOT ${PROJECT_NAME}_FORCE_FETCH_DEPS)
  find_package(ZLIB QUIET)
endif ()

if (NOT TARGET ZLIB::ZLIB)
  if (NOT ZLIB_URI)
    set(ZLIB_URI https://github.com/madler/zlib.git)
  endif ()

  if (NOT ZLIB_TAG)
    set(ZLIB_TAG v1.3.2)
  endif ()

  include(FetchContent)

  block()
    set(ZLIB_BUILD_TESTING OFF)
    # zlib 1.3.2's installed config loads both component exports when a caller
    # does not request one explicitly. Install both in RasterForge's owned
    # prefix so that config is complete; RasterForge and libpng still use the
    # static target below.
    set(ZLIB_BUILD_SHARED ON)
    set(ZLIB_BUILD_STATIC ON)
    set(ZLIB_INSTALL ${${PROJECT_NAME}_INSTALL})

    # libpng calls find_package(ZLIB REQUIRED) from its own build. Redirect
    # that call to this already-populated fallback rather than searching again.
    FetchContent_Declare(
      zlib
      GIT_REPOSITORY ${ZLIB_URI}
      GIT_TAG ${ZLIB_TAG}
      OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(zlib)
  endblock()

  # zlib names its static build target ZLIB::ZLIBSTATIC. Use an imported
  # interface wrapper, not an ALIAS: CMake resolves aliases to their underlying
  # names in install exports, while this external spelling must remain the same
  # one that FindZLIB recreates for consumers.
  if (TARGET zlibstatic AND NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB INTERFACE IMPORTED GLOBAL)
    set_property(TARGET ZLIB::ZLIB PROPERTY
      INTERFACE_LINK_LIBRARIES zlibstatic)
  endif ()

  # FetchContent's find-package redirect recreates the target but does not set
  # FindZLIB's legacy variables. libpng uses ZLIB_INCLUDE_DIRS while generating
  # pnglibconf.h, before target usage requirements apply, so provide the same
  # source/build header pair carried by zlibstatic.
  if (TARGET zlibstatic)
    set(ZLIB_FOUND TRUE)
    set(ZLIB_INCLUDE_DIRS "${zlib_SOURCE_DIR};${zlib_BINARY_DIR}")
    set(ZLIB_INCLUDE_DIR "${zlib_SOURCE_DIR}")
    set(ZLIB_LIBRARIES ZLIB::ZLIB)
  endif ()
endif ()

if (NOT TARGET ZLIB::ZLIB)
  message(FATAL_ERROR "zlib did not provide the required ZLIB::ZLIB target")
endif ()
