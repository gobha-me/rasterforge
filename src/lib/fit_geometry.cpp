#include "fit_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace rasterforge::detail {
namespace {

constexpr Error invalid_dimensions{
    ErrorCode::invalid_dimensions,
    "fit source and destination dimensions must all be non-zero"};
constexpr Error invalid_focus{ErrorCode::invalid_argument,
                              "fit focal coordinates must be finite"};
constexpr Error invalid_fit{ErrorCode::invalid_argument,
                            "fit policy is not recognized"};
constexpr Error geometry_overflow{
    ErrorCode::resource_limit,
    "fit geometry is not representable with public coordinates"};

[[nodiscard]] constexpr auto full_rect(Extent extent) noexcept -> Rect {
  return Rect{.x = 0, .y = 0, .width = extent.width, .height = extent.height};
}

// Every product has two uint32_t operands and therefore fits uint64_t. Round
// exact halves upward so all geometry has one compiler-independent tie rule.
[[nodiscard]] constexpr auto rounded_ratio(std::uint32_t left,
                                           std::uint32_t right,
                                           std::uint32_t divisor) noexcept
    -> std::expected<std::uint32_t, Error> {
  const auto product = static_cast<std::uint64_t>(left) * right;
  auto quotient = product / divisor;
  const auto remainder = product % divisor;
  if ((remainder * 2U) >= divisor) {
    ++quotient;
  }
  quotient = std::max<std::uint64_t>(quotient, 1U);
  if (quotient > std::numeric_limits<std::uint32_t>::max()) {
    return std::unexpected{geometry_overflow};
  }
  return static_cast<std::uint32_t>(quotient);
}

[[nodiscard]] auto focal_offset(std::uint32_t slack, float focus) noexcept
    -> std::uint32_t {
  if (focus <= 0.0F) {
    return 0;
  }
  if (focus >= 1.0F) {
    return slack;
  }

  const auto scaled = static_cast<double>(slack) * static_cast<double>(focus);
  return static_cast<std::uint32_t>(std::floor(scaled + 0.5));
}

[[nodiscard]] constexpr auto source_is_wider(Extent source,
                                             Extent destination) noexcept
    -> bool {
  return (static_cast<std::uint64_t>(source.width) * destination.height) >
         (static_cast<std::uint64_t>(destination.width) * source.height);
}

} // namespace

auto plan_fit(Extent source_extent, Extent destination_extent, Fit fit,
              FocalPoint focus) noexcept -> std::expected<FitPlan, Error> {
  if (source_extent.width == 0 || source_extent.height == 0 ||
      destination_extent.width == 0 || destination_extent.height == 0) {
    return std::unexpected{invalid_dimensions};
  }
  if (!std::isfinite(focus.x) || !std::isfinite(focus.y)) {
    return std::unexpected{invalid_focus};
  }

  focus.x = std::clamp(focus.x, 0.0F, 1.0F);
  focus.y = std::clamp(focus.y, 0.0F, 1.0F);

  const auto full_source = full_rect(source_extent);
  const auto full_destination = full_rect(destination_extent);

  switch (fit) {
  case Fit::stretch:
    return FitPlan{.source_extent = source_extent,
                   .destination_extent = destination_extent,
                   .source = full_source,
                   .destination = full_destination};

  case Fit::contain: {
    auto destination = full_destination;
    if (source_is_wider(source_extent, destination_extent)) {
      const auto height = rounded_ratio(
          source_extent.height, destination_extent.width, source_extent.width);
      if (!height) {
        return std::unexpected{height.error()};
      }
      if (*height > destination_extent.height) {
        return std::unexpected{geometry_overflow};
      }
      destination.height = *height;
      destination.y =
          focal_offset(destination_extent.height - destination.height, focus.y);
    } else {
      const auto width = rounded_ratio(
          source_extent.width, destination_extent.height, source_extent.height);
      if (!width) {
        return std::unexpected{width.error()};
      }
      if (*width > destination_extent.width) {
        return std::unexpected{geometry_overflow};
      }
      destination.width = *width;
      destination.x =
          focal_offset(destination_extent.width - destination.width, focus.x);
    }
    return FitPlan{.source_extent = source_extent,
                   .destination_extent = destination_extent,
                   .source = full_source,
                   .destination = destination};
  }

  case Fit::cover: {
    auto source = full_source;
    if (source_is_wider(source_extent, destination_extent)) {
      const auto width =
          rounded_ratio(source_extent.height, destination_extent.width,
                        destination_extent.height);
      if (!width) {
        return std::unexpected{width.error()};
      }
      if (*width > source_extent.width) {
        return std::unexpected{geometry_overflow};
      }
      source.width = *width;
      source.x = focal_offset(source_extent.width - source.width, focus.x);
    } else {
      const auto height =
          rounded_ratio(source_extent.width, destination_extent.height,
                        destination_extent.width);
      if (!height) {
        return std::unexpected{height.error()};
      }
      if (*height > source_extent.height) {
        return std::unexpected{geometry_overflow};
      }
      source.height = *height;
      source.y = focal_offset(source_extent.height - source.height, focus.y);
    }
    return FitPlan{.source_extent = source_extent,
                   .destination_extent = destination_extent,
                   .source = source,
                   .destination = full_destination};
  }

  case Fit::none: {
    const Extent transfer{
        .width = std::min(source_extent.width, destination_extent.width),
        .height = std::min(source_extent.height, destination_extent.height),
    };
    const Rect source{
        .x = focal_offset(source_extent.width - transfer.width, focus.x),
        .y = focal_offset(source_extent.height - transfer.height, focus.y),
        .width = transfer.width,
        .height = transfer.height,
    };
    const Rect destination{
        .x = focal_offset(destination_extent.width - transfer.width, focus.x),
        .y = focal_offset(destination_extent.height - transfer.height, focus.y),
        .width = transfer.width,
        .height = transfer.height,
    };
    return FitPlan{.source_extent = source_extent,
                   .destination_extent = destination_extent,
                   .source = source,
                   .destination = destination};
  }
  }

  return std::unexpected{invalid_fit};
}

} // namespace rasterforge::detail
