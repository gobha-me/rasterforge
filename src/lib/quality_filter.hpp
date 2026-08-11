#pragma once

#include <rasterforge/rasterforge.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace rasterforge::detail {

// One source sample and its positive, unnormalized triangle-filter weight.
// Duplicate samples created by clamping the filter support at an edge are
// coalesced into one tap.
struct QualityFilterTap {
  std::uint32_t source_index{};
  std::uint64_t weight{};

  friend constexpr auto operator==(const QualityFilterTap &,
                                   const QualityFilterTap &) -> bool = default;
};

// A destination sample owns a contiguous range in its axis tap array. The
// weight sum is retained so the scalar kernel can normalize with exact integer
// division instead of compiler-dependent floating-point arithmetic.
struct QualityFilterSpan {
  std::size_t first_tap{};
  std::size_t tap_count{};
  std::uint64_t weight_sum{};

  friend constexpr auto operator==(const QualityFilterSpan &,
                                   const QualityFilterSpan &) -> bool = default;
};

struct QualityFilterAxisPlan {
  std::vector<QualityFilterSpan> spans{};
  std::vector<QualityFilterTap> taps{};
};

struct QualityFilterPlan {
  QualityFilterAxisPlan horizontal{};
  QualityFilterAxisPlan vertical{};
  std::uint64_t temporary_bytes{};
};

// Build separable, scale-adaptive triangle coefficients. The reported and
// limited temporary storage is the exact vector payload owned by the returned
// plan; allocator bookkeeping is outside the caller-visible contract.
[[nodiscard]] auto make_quality_filter_plan(
    Extent source_extent, Extent destination_extent,
    std::uint64_t max_temporary_bytes)
    -> std::expected<QualityFilterPlan, Error>;

// Deterministic scalar reference executor used to establish the quality-filter
// oracle before RF-03d integrates premultiplied RGBA samples. Strides are in
// scalar samples, making cropped rows usable without a packing copy. No
// destination sample is written unless view validation and all temporary
// allocations have succeeded.
[[nodiscard]] auto resize_quality_scalar(
    std::span<const std::uint8_t> source, Extent source_extent,
    std::size_t source_stride, std::span<std::uint8_t> destination,
    Extent destination_extent, std::size_t destination_stride,
    std::uint64_t max_temporary_bytes) -> std::expected<void, Error>;

} // namespace rasterforge::detail
