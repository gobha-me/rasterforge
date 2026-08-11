# Find a libjpeg-turbo implementation of the libjpeg API. RasterForge relies
# on libjpeg-turbo's in-memory source manager, extended RGBA output, and
# no-backing-store memory implementation, so a generic IJG libjpeg is not an
# interchangeable dependency.

find_path(JPEGTurbo_INCLUDE_DIR NAMES jpeglib.h)
find_path(JPEGTurbo_CONFIG_INCLUDE_DIR
  NAMES jconfig.h
  HINTS ${JPEGTurbo_INCLUDE_DIR}
  PATH_SUFFIXES ${CMAKE_LIBRARY_ARCHITECTURE}
)
find_library(JPEGTurbo_LIBRARY NAMES jpeg jpeg-static)

set(JPEGTurbo_VERSION "")
if (JPEGTurbo_CONFIG_INCLUDE_DIR AND
    EXISTS "${JPEGTurbo_CONFIG_INCLUDE_DIR}/jconfig.h")
  file(STRINGS "${JPEGTurbo_CONFIG_INCLUDE_DIR}/jconfig.h"
    _jpeg_turbo_version_line
    REGEX "^#[ \t]*define[ \t]+LIBJPEG_TURBO_VERSION[ \t]+[0-9]+\\.[0-9]+\\.[0-9]+"
    LIMIT_COUNT 1
  )
  string(REGEX REPLACE
    ".*LIBJPEG_TURBO_VERSION[ \t]+([0-9]+\\.[0-9]+\\.[0-9]+).*"
    "\\1" JPEGTurbo_VERSION "${_jpeg_turbo_version_line}")
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(JPEGTurbo
  REQUIRED_VARS
    JPEGTurbo_INCLUDE_DIR
    JPEGTurbo_CONFIG_INCLUDE_DIR
    JPEGTurbo_LIBRARY
  VERSION_VAR JPEGTurbo_VERSION
)

if (JPEGTurbo_FOUND AND NOT TARGET JPEGTurbo::JPEG)
  add_library(JPEGTurbo::JPEG UNKNOWN IMPORTED GLOBAL)
  set_target_properties(JPEGTurbo::JPEG PROPERTIES
    IMPORTED_LOCATION "${JPEGTurbo_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES
      "${JPEGTurbo_INCLUDE_DIR};${JPEGTurbo_CONFIG_INCLUDE_DIR}"
  )
endif ()

mark_as_advanced(
  JPEGTurbo_INCLUDE_DIR
  JPEGTurbo_CONFIG_INCLUDE_DIR
  JPEGTurbo_LIBRARY
)
