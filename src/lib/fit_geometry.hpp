#pragma once

#include <rasterforge/rasterforge.hpp>

#include <cstdint>
#include <expected>

namespace rasterforge::detail {

// Pixel rectangles are half-open: [x, x + width) by [y, y + height).
struct Rect {
  std::uint32_t x{};
  std::uint32_t y{};
  std::uint32_t width{};
  std::uint32_t height{};

  friend constexpr auto operator==(const Rect &, const Rect &)
      -> bool = default;
};

// Describes the source pixels to sample and the destination pixels to fill.
// Areas of the destination extent outside destination are matte. This value
// contains geometry only and never owns or allocates pixel storage.
struct FitPlan {
  Extent source_extent{};
  Extent destination_extent{};
  Rect source{};
  Rect destination{};

  friend constexpr auto operator==(const FitPlan &, const FitPlan &)
      -> bool = default;
};

[[nodiscard]] auto plan_fit(Extent source_extent, Extent destination_extent,
                            Fit fit, FocalPoint focus = {}) noexcept
    -> std::expected<FitPlan, Error>;

} // namespace rasterforge::detail
