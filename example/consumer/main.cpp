#include <rasterforge/rasterforge.hpp>
#include <rasterforge/version.hpp>

#include <cstdio>

#if __cpp_if_consteval < 202106L
#error "RasterForge's C++23 usage requirement did not reach the consumer"
#endif

auto main() -> int {
  const auto image = rasterforge::Image::create({1, 1});
  if (!image) {
    return 1;
  }

  std::printf("%.*s\n",
              static_cast<int>(rasterforge::version::program_name.size()),
              rasterforge::version::program_name.data());
  return image->size_bytes() == sizeof(rasterforge::Rgba8) ? 0 : 2;
}
