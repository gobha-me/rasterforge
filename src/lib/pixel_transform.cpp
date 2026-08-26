#include <rasterforge/rasterforge.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rasterforge {
namespace {

constexpr Error invalid_factor{ErrorCode::invalid_argument,
                               "pixel transform factor must be finite"};

[[nodiscard]] constexpr auto multiply_channel(std::uint8_t channel,
                                              std::uint8_t multiplier) noexcept
    -> std::uint8_t {
  const auto product = static_cast<std::uint32_t>(channel) * multiplier;
  return static_cast<std::uint8_t>((product + 127U) / 255U);
}

[[nodiscard]] auto normalized_multiplier(float factor) noexcept
    -> std::expected<std::uint8_t, Error> {
  if (!std::isfinite(factor)) {
    return std::unexpected{invalid_factor};
  }

  const auto clamped = std::clamp(factor, 0.0F, 1.0F);
  const auto scaled = static_cast<double>(clamped) * 255.0;
  return static_cast<std::uint8_t>(std::floor(scaled + 0.5));
}

template <typename Transform>
auto transform_pixels(ImageView source, const Limits &limits,
                      Transform transform) -> std::expected<Image, Error> {
  auto output = Image::create(source.extent(), limits);
  if (!output) {
    return std::unexpected{output.error()};
  }

  auto output_view = output->mutable_view();
  for (std::uint32_t y = 0; y < source.extent().height; ++y) {
    const auto source_row = source.row(y);
    if (!source_row) {
      return std::unexpected{source_row.error()};
    }
    auto output_row = output_view.row(y);
    if (!output_row) {
      return std::unexpected{output_row.error()};
    }

    for (std::uint32_t x = 0; x < source.extent().width; ++x) {
      (*output_row)[x] = transform((*source_row)[x]);
    }
  }

  return output;
}

} // namespace

auto tint(ImageView source, Rgb8 multiplier, const Limits &limits)
    -> std::expected<Image, Error> {
  return transform_pixels(source, limits, [multiplier](Rgba8 pixel) {
    pixel.r = multiply_channel(pixel.r, multiplier.r);
    pixel.g = multiply_channel(pixel.g, multiplier.g);
    pixel.b = multiply_channel(pixel.b, multiplier.b);
    return pixel;
  });
}

auto dim(ImageView source, float factor, const Limits &limits)
    -> std::expected<Image, Error> {
  const auto multiplier = normalized_multiplier(factor);
  if (!multiplier) {
    return std::unexpected{multiplier.error()};
  }

  return transform_pixels(source, limits,
                          [multiplier = *multiplier](Rgba8 pixel) {
                            pixel.r = multiply_channel(pixel.r, multiplier);
                            pixel.g = multiply_channel(pixel.g, multiplier);
                            pixel.b = multiply_channel(pixel.b, multiplier);
                            return pixel;
                          });
}

auto adjust_opacity(ImageView source, float factor, const Limits &limits)
    -> std::expected<Image, Error> {
  const auto multiplier = normalized_multiplier(factor);
  if (!multiplier) {
    return std::unexpected{multiplier.error()};
  }

  return transform_pixels(source, limits,
                          [multiplier = *multiplier](Rgba8 pixel) {
                            pixel.a = multiply_channel(pixel.a, multiplier);
                            return pixel;
                          });
}

} // namespace rasterforge
