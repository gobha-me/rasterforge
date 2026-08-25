# Static WebP decode uses libwebp's decoder-only API. Prefer a maintained
# system package, then build the official pinned source in isolation so
# upstream project settings and unrelated tools do not leak into consumers.

if (NOT ${PROJECT_NAME}_FORCE_FETCH_DEPS)
  list(PREPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
  find_package(WebP 1.3.2 QUIET MODULE)
  list(POP_FRONT CMAKE_MODULE_PATH)
endif ()

if (NOT TARGET WebP::webpdecoder)
  include(FetchContent)
  include(ExternalProject)

  set(WEBP_FALLBACK_VERSION 1.6.0)
  if (NOT WEBP_URI)
    set(WEBP_URI
      https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz)
  endif ()
  if (NOT WEBP_HASH)
    set(WEBP_HASH
      SHA256=e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564)
  endif ()

  FetchContent_Declare(libwebp
    URL ${WEBP_URI}
    URL_HASH ${WEBP_HASH}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_GetProperties(libwebp)
  if (NOT libwebp_POPULATED)
    FetchContent_Populate(libwebp)
  endif ()

  set(_webp_stage ${PROJECT_BINARY_DIR}/_deps/libwebp-stage)
  set(_webp_build ${PROJECT_BINARY_DIR}/_deps/libwebp-build)
  file(MAKE_DIRECTORY
    ${_webp_stage}/${CMAKE_INSTALL_INCLUDEDIR}/webp
    ${_webp_stage}/${CMAKE_INSTALL_LIBDIR}
  )

  if (WIN32)
    set(_webp_built_archive
      ${_webp_build}/webpdecoder${CMAKE_STATIC_LIBRARY_SUFFIX})
    set(_webp_archive
      ${_webp_stage}/${CMAKE_INSTALL_LIBDIR}/webpdecoder${CMAKE_STATIC_LIBRARY_SUFFIX})
  else ()
    set(_webp_built_archive
      ${_webp_build}/${CMAKE_STATIC_LIBRARY_PREFIX}webpdecoder${CMAKE_STATIC_LIBRARY_SUFFIX})
    set(_webp_archive
      ${_webp_stage}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}webpdecoder${CMAKE_STATIC_LIBRARY_SUFFIX})
  endif ()

  set(_webp_cmake_args
    -DCMAKE_INSTALL_PREFIX=${_webp_stage}
    -DCMAKE_INSTALL_INCLUDEDIR=${CMAKE_INSTALL_INCLUDEDIR}
    -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
    -DBUILD_SHARED_LIBS=OFF
    -DWEBP_BUILD_ANIM_UTILS=OFF
    -DWEBP_BUILD_CWEBP=OFF
    -DWEBP_BUILD_DWEBP=OFF
    -DWEBP_BUILD_EXTRAS=OFF
    -DWEBP_BUILD_FUZZTEST=OFF
    -DWEBP_BUILD_GIF2WEBP=OFF
    -DWEBP_BUILD_IMG2WEBP=OFF
    -DWEBP_BUILD_LIBWEBPMUX=OFF
    -DWEBP_BUILD_VWEBP=OFF
    -DWEBP_BUILD_WEBPINFO=OFF
    -DWEBP_BUILD_WEBP_JS=OFF
    -DWEBP_BUILD_WEBPMUX=OFF
    -DWEBP_ENABLE_SIMD=OFF
    -DWEBP_USE_THREAD=OFF
  )
  if (CMAKE_BUILD_TYPE)
    list(APPEND _webp_cmake_args -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})
  endif ()
  if (CMAKE_C_COMPILER)
    list(APPEND _webp_cmake_args -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER})
  endif ()

  ExternalProject_Add(${PROJECT_NAME}_libwebp
    SOURCE_DIR ${libwebp_SOURCE_DIR}
    BINARY_DIR ${_webp_build}
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    CMAKE_ARGS ${_webp_cmake_args}
    BUILD_COMMAND
      ${CMAKE_COMMAND} --build <BINARY_DIR> --target webpdecoder
    INSTALL_COMMAND
      ${CMAKE_COMMAND} -E copy_if_different
        ${_webp_built_archive} ${_webp_archive}
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${libwebp_SOURCE_DIR}/src/webp/decode.h
        ${_webp_stage}/${CMAKE_INSTALL_INCLUDEDIR}/webp/decode.h
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${libwebp_SOURCE_DIR}/src/webp/types.h
        ${_webp_stage}/${CMAKE_INSTALL_INCLUDEDIR}/webp/types.h
    BUILD_BYPRODUCTS ${_webp_built_archive}
    INSTALL_BYPRODUCTS ${_webp_archive}
  )

  add_library(WebP::webpdecoder STATIC IMPORTED GLOBAL)
  set_target_properties(WebP::webpdecoder PROPERTIES
    IMPORTED_LOCATION ${_webp_archive}
    INTERFACE_INCLUDE_DIRECTORIES
      "${_webp_stage}/${CMAKE_INSTALL_INCLUDEDIR}"
  )
  if (UNIX)
    set_property(TARGET WebP::webpdecoder APPEND PROPERTY
      INTERFACE_LINK_LIBRARIES m)
  elseif (WIN32)
    set_property(TARGET WebP::webpdecoder APPEND PROPERTY
      INTERFACE_LINK_LIBRARIES "shlwapi;ole32;windowscodecs")
  endif ()
  add_dependencies(WebP::webpdecoder ${PROJECT_NAME}_libwebp)

  configure_file(
    ${PROJECT_SOURCE_DIR}/cmake/WebPFallbackVersion.cmake.in
    ${PROJECT_BINARY_DIR}/WebPFallbackVersion.cmake
    @ONLY
  )

  if (${PROJECT_NAME}_INSTALL)
    install(FILES ${_webp_archive}
      DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY ${_webp_stage}/${CMAKE_INSTALL_INCLUDEDIR}/webp
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES
      ${libwebp_SOURCE_DIR}/COPYING
      ${libwebp_SOURCE_DIR}/PATENTS
      DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/libwebp)
    install(FILES ${PROJECT_BINARY_DIR}/WebPFallbackVersion.cmake
      DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
  endif ()
endif ()

if (NOT TARGET WebP::webpdecoder)
  message(FATAL_ERROR "libwebp did not provide WebP::webpdecoder")
endif ()
