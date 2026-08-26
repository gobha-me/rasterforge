#pragma once

#include <rasterforge/rasterforge.hpp>

#include <algorithm>
#include <cstdint>

namespace rasterforge::detail {

// Gamma-encoded sRGB channel products retain the full straight-channel byte
// through premultiplication: each color component is stored as channel * alpha
// in [0, 65025]. This is deliberately not a public pixel representation and is
// not linear-light RGB.
struct PremultipliedSrgbaProduct {
  std::uint16_t red_times_alpha{};
  std::uint16_t green_times_alpha{};
  std::uint16_t blue_times_alpha{};
  std::uint8_t alpha{};

  friend constexpr auto operator==(const PremultipliedSrgbaProduct &,
                                   const PremultipliedSrgbaProduct &)
      -> bool = default;
};

[[nodiscard]] constexpr auto premultiply_srgba(Rgba8 pixel) noexcept
    -> PremultipliedSrgbaProduct {
  return {
      .red_times_alpha = static_cast<std::uint16_t>(
          static_cast<std::uint16_t>(pixel.r) * pixel.a),
      .green_times_alpha = static_cast<std::uint16_t>(
          static_cast<std::uint16_t>(pixel.g) * pixel.a),
      .blue_times_alpha = static_cast<std::uint16_t>(
          static_cast<std::uint16_t>(pixel.b) * pixel.a),
      .alpha = pixel.a,
  };
}

[[nodiscard]] constexpr auto
unpremultiply_srgba(PremultipliedSrgbaProduct pixel) noexcept -> Rgba8 {
  if (pixel.alpha == 0) {
    return {0, 0, 0, 0};
  }

  const auto channel = [alpha = static_cast<std::uint32_t>(pixel.alpha)](
                           std::uint16_t product) constexpr -> std::uint8_t {
    const auto rounded =
        (static_cast<std::uint32_t>(product) + (alpha / 2U)) / alpha;
    return static_cast<std::uint8_t>(std::min(rounded, 255U));
  };
  return {
      .r = channel(pixel.red_times_alpha),
      .g = channel(pixel.green_times_alpha),
      .b = channel(pixel.blue_times_alpha),
      .a = pixel.alpha,
  };
}

// Porter-Duff source-over in the same gamma-encoded sRGB working space as the
// quality filter. The exact premultiplied numerators are retained until the
// final straight-alpha conversion, so no intermediate byte quantization can
// make the result compiler-dependent.
[[nodiscard]] constexpr auto source_over_srgba(Rgba8 source,
                                               Rgba8 backdrop) noexcept
    -> Rgba8 {
  const auto source_product = premultiply_srgba(source);
  const auto backdrop_product = premultiply_srgba(backdrop);
  const auto inverse_source_alpha =
      255U - static_cast<std::uint32_t>(source_product.alpha);
  const auto alpha_numerator =
      (static_cast<std::uint32_t>(source_product.alpha) * 255U) +
      (static_cast<std::uint32_t>(backdrop_product.alpha) *
       inverse_source_alpha);

  if (alpha_numerator == 0) {
    return {0, 0, 0, 0};
  }

  const auto channel =
      [alpha_numerator, inverse_source_alpha](
          std::uint16_t source_times_alpha,
          std::uint16_t backdrop_times_alpha) constexpr -> std::uint8_t {
    const auto numerator =
        (static_cast<std::uint32_t>(source_times_alpha) * 255U) +
        (static_cast<std::uint32_t>(backdrop_times_alpha) *
         inverse_source_alpha);
    return static_cast<std::uint8_t>((numerator + (alpha_numerator / 2U)) /
                                     alpha_numerator);
  };

  return {
      .r = channel(source_product.red_times_alpha,
                   backdrop_product.red_times_alpha),
      .g = channel(source_product.green_times_alpha,
                   backdrop_product.green_times_alpha),
      .b = channel(source_product.blue_times_alpha,
                   backdrop_product.blue_times_alpha),
      .a = static_cast<std::uint8_t>((alpha_numerator + 127U) / 255U),
  };
}

} // namespace rasterforge::detail
