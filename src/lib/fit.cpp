#include <rasterforge/rasterforge.hpp>

#include "fit_geometry.hpp"

#include <algorithm>
#include <cstdint>

namespace rasterforge {
namespace {

// Map a destination-pixel center into a source interval. This is equivalent to
// floor((destination_index + 0.5) * source_length / destination_length),
// including a tie toward the greater source coordinate, but avoids forming a
// product larger than two public 32-bit dimensions.
[[nodiscard]] constexpr auto nearest_index(std::uint32_t destination_index,
                                           std::uint32_t source_length,
                                           std::uint32_t destination_length)
    noexcept -> std::uint32_t {
  const auto product =
      static_cast<std::uint64_t>(destination_index) * source_length;
  auto quotient = product / destination_length;
  const auto remainder = product % destination_length;

  const auto centered_numerator = (remainder * 2U) + source_length;
  const auto centered_denominator =
      static_cast<std::uint64_t>(destination_length) * 2U;
  if (centered_numerator >= centered_denominator) {
    ++quotient;
  }
  return static_cast<std::uint32_t>(quotient);
}

} // namespace

auto fit(ImageView source, Extent destination, Fit policy, FocalPoint focus,
         Rgba8 matte, const Limits &limits) -> std::expected<Image, Error> {
  const auto plan =
      detail::plan_fit(source.extent(), destination, policy, focus);
  if (!plan) {
    return std::unexpected{plan.error()};
  }

  auto output = Image::create(destination, limits);
  if (!output) {
    return std::unexpected{output.error()};
  }

  auto output_view = output->mutable_view();
  for (std::uint32_t y = 0; y < destination.height; ++y) {
    auto row = output_view.row(y);
    if (!row) {
      return std::unexpected{row.error()};
    }
    std::ranges::fill(*row, matte);
  }

  for (std::uint32_t destination_y = 0;
       destination_y < plan->destination.height; ++destination_y) {
    const auto source_y =
        plan->source.y +
        nearest_index(destination_y, plan->source.height,
                      plan->destination.height);
    const auto source_row = source.row(source_y);
    if (!source_row) {
      return std::unexpected{source_row.error()};
    }

    const auto output_y = plan->destination.y + destination_y;
    auto output_row = output_view.row(output_y);
    if (!output_row) {
      return std::unexpected{output_row.error()};
    }

    for (std::uint32_t destination_x = 0;
         destination_x < plan->destination.width; ++destination_x) {
      const auto source_x =
          plan->source.x +
          nearest_index(destination_x, plan->source.width,
                        plan->destination.width);
      (*output_row)[plan->destination.x + destination_x] =
          (*source_row)[source_x];
    }
  }

  return output;
}

} // namespace rasterforge
