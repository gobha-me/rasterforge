#include <png.h>
#include <webp/decode.h>

#include <cstddef>
#include <cstdio>

extern "C" {
#include <jpeglib.h>
}

#ifndef PNG_USER_MEM_SUPPORTED
#  error "RasterForge requires libpng custom allocator support"
#endif

#ifndef PNG_SET_USER_LIMITS_SUPPORTED
#  error "RasterForge requires libpng dimension and chunk limit support"
#endif

#ifndef PNG_SETJMP_SUPPORTED
#  error "RasterForge requires libpng setjmp error containment"
#endif

#if !defined(PNG_READ_SUPPORTED) || !defined(PNG_READ_TRANSFORMS_SUPPORTED) || \
    !defined(PNG_READ_EXPAND_SUPPORTED) ||                                \
    !defined(PNG_READ_FILLER_SUPPORTED) ||                                \
    !defined(PNG_READ_GRAY_TO_RGB_SUPPORTED) ||                            \
    !defined(PNG_READ_INTERLACING_SUPPORTED) ||                            \
    !defined(PNG_READ_STRIP_16_TO_8_SUPPORTED) ||                          \
    !defined(PNG_READ_USER_CHUNKS_SUPPORTED) ||                            \
    !defined(PNG_SET_UNKNOWN_CHUNKS_SUPPORTED)
#  error "RasterForge requires libpng RGBA read transformations"
#endif

namespace rasterforge::detail {

auto png_dependency_ready() noexcept -> bool {
  return png_access_version_number() == PNG_LIBPNG_VER;
}

auto jpeg_dependency_ready() noexcept -> bool {
#ifdef LIBJPEG_TURBO_VERSION_NUMBER
  return LIBJPEG_TURBO_VERSION_NUMBER >= 2001005;
#else
  return false;
#endif
}

auto webp_dependency_ready() noexcept -> bool {
  return WebPGetDecoderVersion() >= 0x010302;
}

} // namespace rasterforge::detail
