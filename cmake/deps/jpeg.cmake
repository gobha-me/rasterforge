# JPEG decoding requires libjpeg-turbo rather than an arbitrary libjpeg: the
# adapter uses its RGBA output extension and relies on its no-backing-store
# memory implementation. Prefer a maintained system package, then populate the
# official pinned source release and build it in an isolated CMake project.

if (NOT ${PROJECT_NAME}_FORCE_FETCH_DEPS)
  list(PREPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
  find_package(JPEGTurbo 2.1.5 QUIET MODULE)
  list(POP_FRONT CMAKE_MODULE_PATH)
endif ()

if (NOT TARGET JPEGTurbo::JPEG)
  include(FetchContent)
  include(ExternalProject)

  if (NOT JPEG_TURBO_URI)
    set(JPEG_TURBO_URI
      https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.2.0/libjpeg-turbo-3.2.0.tar.gz)
  endif ()
  if (NOT JPEG_TURBO_HASH)
    set(JPEG_TURBO_HASH
      SHA256=6f30092cef9fb839779646608f4ee14ae3cbac989c47fa05e841b0841f09878e)
  endif ()

  # libjpeg-turbo explicitly rejects add_subdirectory integration. FetchContent
  # owns reproducible acquisition; ExternalProject gives the upstream CMake
  # project the top-level build it requires.
  FetchContent_Declare(libjpeg_turbo
    URL ${JPEG_TURBO_URI}
    URL_HASH ${JPEG_TURBO_HASH}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_GetProperties(libjpeg_turbo)
  if (NOT libjpeg_turbo_POPULATED)
    FetchContent_Populate(libjpeg_turbo)
  endif ()

  set(_jpeg_stage ${PROJECT_BINARY_DIR}/_deps/libjpeg_turbo-stage)
  set(_jpeg_build ${PROJECT_BINARY_DIR}/_deps/libjpeg_turbo-build)
  file(MAKE_DIRECTORY
    ${_jpeg_stage}/${CMAKE_INSTALL_INCLUDEDIR}
    ${_jpeg_stage}/${CMAKE_INSTALL_LIBDIR}
  )

  if (WIN32)
    set(_jpeg_archive
      ${_jpeg_stage}/${CMAKE_INSTALL_LIBDIR}/jpeg-static${CMAKE_STATIC_LIBRARY_SUFFIX})
  else ()
    set(_jpeg_archive
      ${_jpeg_stage}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}jpeg${CMAKE_STATIC_LIBRARY_SUFFIX})
  endif ()

  set(_jpeg_cmake_args
    -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    -DCMAKE_INSTALL_INCLUDEDIR=${CMAKE_INSTALL_INCLUDEDIR}
    -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
    -DENABLE_SHARED=OFF
    -DENABLE_STATIC=ON
    -DWITH_TURBOJPEG=OFF
    -DWITH_TOOLS=OFF
    -DWITH_TESTS=OFF
    -DWITH_SIMD=OFF
    -DWITH_ARITH_DEC=OFF
    -DWITH_ARITH_ENC=OFF
  )
  if (CMAKE_BUILD_TYPE)
    list(APPEND _jpeg_cmake_args -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})
  endif ()
  if (CMAKE_TOOLCHAIN_FILE)
    get_filename_component(_jpeg_toolchain_file
      "${CMAKE_TOOLCHAIN_FILE}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
    list(APPEND _jpeg_cmake_args
      -DCMAKE_TOOLCHAIN_FILE=${_jpeg_toolchain_file})
  endif ()
  if (CMAKE_C_COMPILER)
    list(APPEND _jpeg_cmake_args -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER})
  endif ()

  ExternalProject_Add(${PROJECT_NAME}_libjpeg_turbo
    SOURCE_DIR ${libjpeg_turbo_SOURCE_DIR}
    BINARY_DIR ${_jpeg_build}
    INSTALL_DIR ${_jpeg_stage}
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    CMAKE_ARGS ${_jpeg_cmake_args}
    BUILD_BYPRODUCTS ${_jpeg_archive}
  )

  add_library(JPEGTurbo::JPEG STATIC IMPORTED GLOBAL)
  set_target_properties(JPEGTurbo::JPEG PROPERTIES
    IMPORTED_LOCATION ${_jpeg_archive}
    INTERFACE_INCLUDE_DIRECTORIES
      "${_jpeg_stage}/${CMAKE_INSTALL_INCLUDEDIR}"
  )
  add_dependencies(JPEGTurbo::JPEG ${PROJECT_NAME}_libjpeg_turbo)

  if (${PROJECT_NAME}_INSTALL)
    install(FILES ${_jpeg_archive}
      DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY ${_jpeg_stage}/${CMAKE_INSTALL_INCLUDEDIR}/
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES ${libjpeg_turbo_SOURCE_DIR}/LICENSE.md
      DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/libjpeg-turbo)
  endif ()
endif ()

if (NOT TARGET JPEGTurbo::JPEG)
  message(FATAL_ERROR
    "libjpeg-turbo did not provide the required JPEGTurbo::JPEG target")
endif ()
