#include <rasterforge/rasterforge.hpp>
#include <rasterforge/version.hpp>

#include <array>
#include <cstddef>
#include <cstdio>

#if __cpp_if_consteval < 202106L
#error "RasterForge's C++23 usage requirement did not reach the consumer"
#endif

auto main() -> int {
  const auto image = rasterforge::Image::create({1, 1});
  if (!image) {
    return 1;
  }

  // Exercise the installed generic decode boundary without depending on a
  // production codec: a matching one-byte PNG prefix is always truncated.
  constexpr std::array encoded_prefix{std::byte{0x89}};
  const rasterforge::DecodeOptions options{};
  const auto decoded = rasterforge::decode(encoded_prefix, options);
  if (decoded || decoded.error().code != rasterforge::ErrorCode::truncated_data) {
    return 2;
  }
  if (options.orientation != rasterforge::OrientationPolicy::apply) {
    return 3;
  }

  std::printf("%.*s\n",
              static_cast<int>(rasterforge::version::program_name.size()),
              rasterforge::version::program_name.data());
  return image->size_bytes() == sizeof(rasterforge::Rgba8) ? 0 : 4;
}
