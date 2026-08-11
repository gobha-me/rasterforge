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

} // namespace rasterforge::detail
