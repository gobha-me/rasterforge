# Normalize libwebp's decoder-only library into WebP::webpdecoder. An installed
# RasterForge fallback must win over unrelated system packages; otherwise
# prefer an upstream CMake package, then use pkg-config and path discovery.

if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/WebPFallbackVersion.cmake")
  include("${CMAKE_CURRENT_LIST_DIR}/WebPFallbackVersion.cmake")
  get_filename_component(_WebP_fallback_library_dir
    "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
  get_filename_component(_WebP_fallback_prefix
    "${_WebP_fallback_library_dir}/.." ABSOLUTE)
  find_path(WebP_INCLUDE_DIR
    NAMES webp/decode.h
    PATHS "${_WebP_fallback_prefix}/include"
    NO_DEFAULT_PATH
  )
  find_library(WebP_LIBRARY
    NAMES webpdecoder libwebpdecoder
    PATHS "${_WebP_fallback_library_dir}"
    NO_DEFAULT_PATH
  )
endif ()

if (NOT TARGET WebP::webpdecoder AND
    NOT (WebP_INCLUDE_DIR AND WebP_LIBRARY))
  find_package(WebP ${WebP_FIND_VERSION} QUIET CONFIG NO_MODULE)
endif ()

if (TARGET WebP::webpdecoder)
  set(WebP_FOUND TRUE)
  return()
endif ()

find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
  pkg_check_modules(PC_WebP QUIET libwebpdecoder)
endif ()

find_path(WebP_INCLUDE_DIR
  NAMES webp/decode.h
  HINTS ${PC_WebP_INCLUDE_DIRS}
)
find_library(WebP_LIBRARY
  NAMES webpdecoder libwebpdecoder
  HINTS ${PC_WebP_LIBRARY_DIRS}
)

if (NOT WebP_VERSION)
  set(WebP_VERSION "${PC_WebP_VERSION}")
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WebP
  REQUIRED_VARS WebP_INCLUDE_DIR WebP_LIBRARY
  VERSION_VAR WebP_VERSION
)

if (WebP_FOUND AND NOT TARGET WebP::webpdecoder)
  add_library(WebP::webpdecoder UNKNOWN IMPORTED GLOBAL)
  set_target_properties(WebP::webpdecoder PROPERTIES
    IMPORTED_LOCATION "${WebP_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${WebP_INCLUDE_DIR}"
  )
  if (UNIX)
    set_property(TARGET WebP::webpdecoder APPEND PROPERTY
      INTERFACE_LINK_LIBRARIES m)
  elseif (WIN32)
    set_property(TARGET WebP::webpdecoder APPEND PROPERTY
      INTERFACE_LINK_LIBRARIES "shlwapi;ole32;windowscodecs")
  endif ()
endif ()

mark_as_advanced(WebP_INCLUDE_DIR WebP_LIBRARY)
