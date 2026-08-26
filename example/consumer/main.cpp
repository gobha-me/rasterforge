#include <rasterforge/rasterforge.hpp>
#include <rasterforge/version.hpp>

#include "external_rgba.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <type_traits>

#if __cpp_if_consteval < 202106L
#error "RasterForge's C++23 usage requirement did not reach the consumer"
#endif

static_assert(
    !std::is_convertible_v<rasterforge::ImageView, external_rgba::ImageView>);

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
  const auto fitted =
      rasterforge::fit(decoded->view(), {2, 2}, rasterforge::Fit::contain, {},
                       rasterforge::Rgba8{0, 0, 0, 0}, fit_limits,
                       rasterforge::ResizeFilter::triangle);
  if (!fitted || fitted->extent() != rasterforge::Extent{2, 2}) {
    return 5;
  }
  const auto fitted_row = fitted->view().row(1);
  if (!fitted_row ||
      (*fitted_row)[1] != rasterforge::Rgba8{0x12, 0x12, 0x12, 0xFF}) {
    return 6;
  }

  const auto tinted = rasterforge::tint(
      fitted->view(), rasterforge::Rgb8{255, 128, 0}, fit_limits);
  if (!tinted) {
    return 7;
  }
  const auto dimmed = rasterforge::dim(tinted->view(), 0.5F, fit_limits);
  if (!dimmed) {
    return 8;
  }
  auto faded = rasterforge::adjust_opacity(dimmed->view(), 0.5F, fit_limits);
  if (!faded) {
    return 9;
  }
  const auto transformed_row = faded->view().row(0);
  if (!transformed_row ||
      (*transformed_row)[0] != rasterforge::Rgba8{9, 5, 0, 128}) {
    return 10;
  }

  const auto invalid_external =
      external_rgba::copy_to_external_rgba8(rasterforge::ImageView{});
  if (invalid_external ||
      invalid_external.error() != external_rgba::CopyError::invalid_source) {
    return 11;
  }

  const auto external = external_rgba::copy_to_external_rgba8(faded->view());
  if (!external) {
    return 12;
  }
  const auto external_view = external->view();
  if (external_view.width() != 2 || external_view.height() != 2 ||
      external_view.row_stride_bytes() != 2 * sizeof(external_rgba::Rgba8) ||
      external_view.channel_order() != external_rgba::ChannelOrder::rgba8) {
    return 13;
  }
  const auto external_row = external_view.row(1);
  if (external_row.size() != 2 ||
      external_row[1] != external_rgba::Rgba8{9, 5, 0, 128}) {
    return 14;
  }

  // Mutating the RasterForge owner after conversion proves that this adapter
  // copied into independent external storage rather than reinterpreting it.
  const auto mutable_source_row = faded->mutable_view().row(1);
  if (!mutable_source_row) {
    return 15;
  }
  (*mutable_source_row)[1] = rasterforge::Rgba8{255, 0, 255, 0};
  if (external_view.row(1)[1] != external_rgba::Rgba8{9, 5, 0, 128}) {
    return 16;
  }

  std::printf("%.*s\n",
              static_cast<int>(rasterforge::version::program_name.size()),
              rasterforge::version::program_name.data());
  return image->size_bytes() == sizeof(rasterforge::Rgba8) ? 0 : 17;
}
