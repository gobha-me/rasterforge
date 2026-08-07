# libpng is RasterForge's first production codec. Prefer a system libpng 1.6;
# otherwise use a pinned static build with only the library and install metadata.

if (NOT ${PROJECT_NAME}_FORCE_FETCH_DEPS)
  find_package(PNG 1.6 QUIET)
endif ()

if (NOT TARGET PNG::PNG)
  if (NOT PNG_URI)
    set(PNG_URI https://github.com/pnggroup/libpng.git)
  endif ()

  if (NOT PNG_TAG)
    set(PNG_TAG v1.6.58)
  endif ()

  include(FetchContent)

  block()
    set(PNG_SHARED OFF)
    set(PNG_STATIC ON)
    set(PNG_TESTS OFF)
    set(PNG_TOOLS OFF)

    # libpng exposes SKIP_INSTALL_ALL rather than a positive install option.
    # Its install/export rules are required only when RasterForge installs.
    if (${PROJECT_NAME}_INSTALL)
      set(SKIP_INSTALL_ALL OFF)
      set(SKIP_INSTALL_EXECUTABLES ON)
      set(SKIP_INSTALL_PROGRAMS ON)
      set(SKIP_INSTALL_FILES ON)
      # libpng otherwise exports png_static twice: once through its legacy
      # libpng16 export and again through modern PNGTargets. CMake refuses an
      # installed dependent target whose dependency belongs to two exports.
      set(SKIP_INSTALL_EXPORT ON)
    else ()
      set(SKIP_INSTALL_ALL ON)
    endif ()

    FetchContent_Declare(
      libpng
      GIT_REPOSITORY ${PNG_URI}
      GIT_TAG ${PNG_TAG}
    )
    FetchContent_MakeAvailable(libpng)
  endblock()

  # Keep the canonical external spelling in RasterForge's install export. An
  # ALIAS would be rewritten to PNG::png_static, which CMake's FindPNG module
  # does not create for an installed consumer.
  if (TARGET png_static AND NOT TARGET PNG::PNG)
    add_library(PNG::PNG INTERFACE IMPORTED GLOBAL)
    set_property(TARGET PNG::PNG PROPERTY
      INTERFACE_LINK_LIBRARIES png_static)
  endif ()
endif ()

if (NOT TARGET PNG::PNG)
  message(FATAL_ERROR "libpng did not provide the required PNG::PNG target")
endif ()
