#include <rasterforge/rasterforge.hpp>
#include <rasterforge/version.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

#if __cpp_if_consteval < 202106L
#error "RasterForge's C++23 usage requirement did not reach the consumer"
#endif

auto main() -> int {
  const auto image = rasterforge::Image::create({1, 1});
  if (!image) {
    return 1;
  }

  // A generated 1x1 16-bit grayscale PNG. Keeping it inline proves that every
  // consumer mode links and runs the production codec without filesystem I/O.
  constexpr std::array<std::uint8_t, 68> encoded_png{{
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
      0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
      0x10, 0x00, 0x00, 0x00, 0x00, 0x6A, 0xEE, 0x47, 0x16, 0x00, 0x00, 0x00,
      0x0B, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0x10, 0x32, 0x01, 0x00,
      0x00, 0x5B, 0x00, 0x47, 0x05, 0x5F, 0x6C, 0x82, 0x00, 0x00, 0x00, 0x00,
      0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
  }};
  const rasterforge::DecodeOptions options{};
  const auto decoded =
      rasterforge::decode(std::as_bytes(std::span{encoded_png}), options);
  if (!decoded || decoded->output_extent() != rasterforge::Extent{1, 1} ||
      decoded->has_alpha()) {
    return 2;
  }
  const auto decoded_row = decoded->view().row(0);
  if (!decoded_row ||
      (*decoded_row)[0] != rasterforge::Rgba8{0x12, 0x12, 0x12, 0xFF}) {
    return 3;
  }
  if (options.orientation != rasterforge::OrientationPolicy::apply) {
    return 4;
  }

  rasterforge::Limits fit_limits{};
  fit_limits.max_dimension = 2;
  fit_limits.max_pixels = 4;
  fit_limits.max_output_bytes = 4 * sizeof(rasterforge::Rgba8);
  const auto fitted = rasterforge::fit(
      decoded->view(), {2, 2}, rasterforge::Fit::contain, {},
      rasterforge::Rgba8{0, 0, 0, 0}, fit_limits);
  if (!fitted || fitted->extent() != rasterforge::Extent{2, 2}) {
    return 5;
  }
  const auto fitted_row = fitted->view().row(1);
  if (!fitted_row ||
      (*fitted_row)[1] != rasterforge::Rgba8{0x12, 0x12, 0x12, 0xFF}) {
    return 6;
  }

  std::printf("%.*s\n",
              static_cast<int>(rasterforge::version::program_name.size()),
              rasterforge::version::program_name.data());
  return image->size_bytes() == sizeof(rasterforge::Rgba8) ? 0 : 7;
}
