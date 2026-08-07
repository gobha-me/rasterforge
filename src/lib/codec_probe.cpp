#include <png.h>

#ifndef PNG_USER_MEM_SUPPORTED
#  error "RasterForge requires libpng custom allocator support"
#endif

#ifndef PNG_SET_USER_LIMITS_SUPPORTED
#  error "RasterForge requires libpng dimension and chunk limit support"
#endif

namespace rasterforge::detail {

auto png_dependency_ready() noexcept -> bool {
  return png_access_version_number() == PNG_LIBPNG_VER;
}

} // namespace rasterforge::detail
