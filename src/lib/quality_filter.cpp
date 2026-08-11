#include "quality_filter.hpp"

#include "premultiplied_rgba.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

namespace rasterforge::detail {
namespace {

using SignedWide = __int128_t;
using UnsignedWide = __uint128_t;

constexpr Error invalid_dimensions{ErrorCode::invalid_dimensions,
                                   "filter dimensions must both be non-zero"};
constexpr Error invalid_storage{
    ErrorCode::invalid_argument,
    "scalar storage or stride does not contain the requested extent"};
constexpr Error temporary_limit{
    ErrorCode::resource_limit,
    "quality-filter temporary storage exceeds the configured limit"};
constexpr Error arithmetic_limit{
    ErrorCode::resource_limit,
    "quality-filter coefficient arithmetic is not representable"};
constexpr Error allocation_failure{
    ErrorCode::allocation_failure,
    "quality-filter temporary allocation failed"};

struct AxisBounds {
  SignedWide first{};
  SignedWide last{};
  std::uint32_t first_clamped{};
  std::uint32_t last_clamped{};
};

struct AxisShape {
  std::size_t span_count{};
  std::size_t tap_count{};
  std::uint64_t temporary_bytes{};
};

[[nodiscard]] constexpr auto floor_div(SignedWide numerator,
                                       SignedWide denominator) noexcept
    -> SignedWide {
  auto quotient = numerator / denominator;
  if ((numerator % denominator) < 0) {
    --quotient;
  }
  return quotient;
}

[[nodiscard]] constexpr auto checked_add(std::uint64_t left,
                                         std::uint64_t right) noexcept
    -> std::expected<std::uint64_t, Error> {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return std::unexpected{arithmetic_limit};
  }
  return left + right;
}

[[nodiscard]] constexpr auto checked_multiply(std::uint64_t left,
                                              std::uint64_t right) noexcept
    -> std::expected<std::uint64_t, Error> {
  if (right != 0 &&
      left > std::numeric_limits<std::uint64_t>::max() / right) {
    return std::unexpected{arithmetic_limit};
  }
  return left * right;
}

[[nodiscard]] constexpr auto to_uint64(UnsignedWide value) noexcept
    -> std::expected<std::uint64_t, Error> {
  if (value > std::numeric_limits<std::uint64_t>::max()) {
    return std::unexpected{arithmetic_limit};
  }
  return static_cast<std::uint64_t>(value);
}

[[nodiscard]] constexpr auto center_numerator(
    std::uint32_t destination_index, std::uint32_t source_length,
    std::uint32_t destination_length) noexcept -> SignedWide {
  return ((static_cast<SignedWide>(destination_index) * 2 + 1) *
          source_length) -
         destination_length;
}

[[nodiscard]] constexpr auto axis_bounds(
    std::uint32_t destination_index, std::uint32_t source_length,
    std::uint32_t destination_length) noexcept -> AxisBounds {
  const auto center = center_numerator(destination_index, source_length,
                                       destination_length);
  const auto denominator = static_cast<SignedWide>(destination_length) * 2;
  const auto support =
      static_cast<SignedWide>(std::max(source_length, destination_length)) * 2;

  // Triangle weights are positive strictly inside the support. Expressing the
  // bounds this way omits zero-weight endpoints without floating point.
  const auto first = floor_div(center - support, denominator) + 1;
  const auto last = floor_div(center + support - 1, denominator);
  const auto maximum_source = static_cast<SignedWide>(source_length) - 1;
  const auto first_clamped =
      static_cast<std::uint32_t>(std::clamp(first, SignedWide{0},
                                            maximum_source));
  const auto last_clamped =
      static_cast<std::uint32_t>(std::clamp(last, SignedWide{0},
                                            maximum_source));
  return {first, last, first_clamped, last_clamped};
}

[[nodiscard]] constexpr auto raw_weight(
    SignedWide source_index, SignedWide center, SignedWide denominator,
    SignedWide support) noexcept -> UnsignedWide {
  const auto position = denominator * source_index;
  const auto distance = position >= center ? position - center
                                           : center - position;
  return static_cast<UnsignedWide>(support - distance);
}

[[nodiscard]] constexpr auto arithmetic_sum(SignedWide first,
                                            SignedWide last) noexcept
    -> SignedWide {
  const auto count = last - first + 1;
  // Divide an even factor first so the intermediate stays comfortably within
  // the signed 128-bit range for public 32-bit dimensions.
  return (count % 2 == 0) ? (count / 2) * (first + last)
                          : count * ((first + last) / 2);
}

[[nodiscard]] constexpr auto left_outside_weight(
    SignedWide first, SignedWide last, SignedWide center,
    SignedWide denominator, SignedWide support) noexcept -> UnsignedWide {
  if (first > last) {
    return 0;
  }
  const auto count = last - first + 1;
  const auto total =
      count * (support - center) + denominator * arithmetic_sum(first, last);
  return static_cast<UnsignedWide>(total);
}

[[nodiscard]] constexpr auto right_outside_weight(
    SignedWide first, SignedWide last, SignedWide center,
    SignedWide denominator, SignedWide support) noexcept -> UnsignedWide {
  if (first > last) {
    return 0;
  }
  const auto count = last - first + 1;
  const auto total =
      count * (support + center) - denominator * arithmetic_sum(first, last);
  return static_cast<UnsignedWide>(total);
}

[[nodiscard]] auto measure_axis(std::uint32_t source_length,
                                std::uint32_t destination_length,
                                std::uint64_t byte_limit)
    -> std::expected<AxisShape, Error> {
  const auto span_bytes = checked_multiply(destination_length,
                                           sizeof(QualityFilterSpan));
  if (!span_bytes || *span_bytes > byte_limit ||
      destination_length > std::vector<QualityFilterSpan>{}.max_size()) {
    return std::unexpected{temporary_limit};
  }

  std::uint64_t tap_count = 0;
  for (std::uint32_t destination = 0; destination < destination_length;
       ++destination) {
    const auto bounds =
        axis_bounds(destination, source_length, destination_length);
    const auto count = static_cast<std::uint64_t>(bounds.last_clamped) -
                       bounds.first_clamped + 1;
    const auto next_count = checked_add(tap_count, count);
    if (!next_count) {
      return std::unexpected{next_count.error()};
    }
    tap_count = *next_count;

    const auto tap_bytes =
        checked_multiply(tap_count, sizeof(QualityFilterTap));
    if (!tap_bytes) {
      return std::unexpected{tap_bytes.error()};
    }
    const auto total_bytes = checked_add(*span_bytes, *tap_bytes);
    if (!total_bytes || *total_bytes > byte_limit) {
      return std::unexpected{temporary_limit};
    }
  }

  if (tap_count > std::vector<QualityFilterTap>{}.max_size()) {
    return std::unexpected{temporary_limit};
  }
  const auto tap_bytes = checked_multiply(tap_count, sizeof(QualityFilterTap));
  if (!tap_bytes) {
    return std::unexpected{tap_bytes.error()};
  }
  const auto total_bytes = checked_add(*span_bytes, *tap_bytes);
  if (!total_bytes) {
    return std::unexpected{total_bytes.error()};
  }
  return AxisShape{
      .span_count = destination_length,
      .tap_count = static_cast<std::size_t>(tap_count),
      .temporary_bytes = *total_bytes,
  };
}

[[nodiscard]] auto populate_axis(QualityFilterAxisPlan &axis,
                                 std::uint32_t source_length,
                                 std::uint32_t destination_length)
    -> std::expected<void, Error> {
  std::size_t next_tap = 0;
  for (std::uint32_t destination = 0; destination < destination_length;
       ++destination) {
    const auto bounds =
        axis_bounds(destination, source_length, destination_length);
    const auto center =
        center_numerator(destination, source_length, destination_length);
    const auto denominator = static_cast<SignedWide>(destination_length) * 2;
    const auto support =
        static_cast<SignedWide>(std::max(source_length, destination_length)) *
        2;
    const auto span_first = next_tap;
    UnsignedWide span_weight = 0;

    for (std::uint64_t source = bounds.first_clamped;
         source <= bounds.last_clamped; ++source) {
      const auto signed_source = static_cast<SignedWide>(source);
      UnsignedWide weight = 0;
      if (signed_source >= bounds.first && signed_source <= bounds.last) {
        weight += raw_weight(signed_source, center, denominator, support);
      }
      if (source == 0 && bounds.first < 0) {
        weight += left_outside_weight(
            bounds.first, std::min(bounds.last, SignedWide{-1}), center,
            denominator, support);
      }
      if (source == static_cast<std::uint64_t>(source_length) - 1 &&
          bounds.last >= source_length) {
        weight += right_outside_weight(
            std::max(bounds.first, static_cast<SignedWide>(source_length)),
            bounds.last, center, denominator, support);
      }

      const auto stored_weight = to_uint64(weight);
      if (!stored_weight || *stored_weight == 0) {
        return std::unexpected{arithmetic_limit};
      }
      axis.taps[next_tap++] = QualityFilterTap{
          .source_index = static_cast<std::uint32_t>(source),
          .weight = *stored_weight,
      };
      span_weight += weight;
    }

    const auto stored_sum = to_uint64(span_weight);
    if (!stored_sum || *stored_sum == 0) {
      return std::unexpected{arithmetic_limit};
    }
    axis.spans[destination] = QualityFilterSpan{
        .first_tap = span_first,
        .tap_count = next_tap - span_first,
        .weight_sum = *stored_sum,
    };
  }
  return {};
}

[[nodiscard]] constexpr auto rounded_divide(UnsignedWide numerator,
                                            std::uint64_t denominator) noexcept
    -> UnsignedWide {
  return (numerator + (static_cast<UnsignedWide>(denominator) / 2)) /
         denominator;
}

[[nodiscard]] auto checked_storage_size(Extent extent, std::size_t stride)
    -> std::expected<std::size_t, Error> {
  if (stride < extent.width) {
    return std::unexpected{invalid_storage};
  }
  const auto row_offset =
      checked_multiply(extent.height - 1, static_cast<std::uint64_t>(stride));
  if (!row_offset) {
    return std::unexpected{row_offset.error()};
  }
  const auto required = checked_add(*row_offset, extent.width);
  if (!required || *required > std::numeric_limits<std::size_t>::max()) {
    return std::unexpected{arithmetic_limit};
  }
  return static_cast<std::size_t>(*required);
}

} // namespace

auto make_quality_filter_plan(Extent source_extent, Extent destination_extent,
                              std::uint64_t max_temporary_bytes)
    -> std::expected<QualityFilterPlan, Error> {
  if (source_extent.width == 0 || source_extent.height == 0 ||
      destination_extent.width == 0 || destination_extent.height == 0) {
    return std::unexpected{invalid_dimensions};
  }

  const auto horizontal = measure_axis(source_extent.width,
                                       destination_extent.width,
                                       max_temporary_bytes);
  if (!horizontal) {
    return std::unexpected{horizontal.error()};
  }
  const auto remaining = max_temporary_bytes - horizontal->temporary_bytes;
  const auto vertical = measure_axis(source_extent.height,
                                     destination_extent.height, remaining);
  if (!vertical) {
    return std::unexpected{vertical.error()};
  }
  const auto total_bytes =
      checked_add(horizontal->temporary_bytes, vertical->temporary_bytes);
  if (!total_bytes || *total_bytes > max_temporary_bytes) {
    return std::unexpected{temporary_limit};
  }

  QualityFilterPlan plan{};
  try {
    plan.horizontal.spans.resize(horizontal->span_count);
    plan.horizontal.taps.resize(horizontal->tap_count);
    plan.vertical.spans.resize(vertical->span_count);
    plan.vertical.taps.resize(vertical->tap_count);
  } catch (const std::bad_alloc &) {
    return std::unexpected{allocation_failure};
  } catch (const std::length_error &) {
    return std::unexpected{temporary_limit};
  }

  const auto horizontal_result = populate_axis(
      plan.horizontal, source_extent.width, destination_extent.width);
  if (!horizontal_result) {
    return std::unexpected{horizontal_result.error()};
  }
  const auto vertical_result = populate_axis(
      plan.vertical, source_extent.height, destination_extent.height);
  if (!vertical_result) {
    return std::unexpected{vertical_result.error()};
  }
  plan.temporary_bytes = *total_bytes;
  return plan;
}

auto resize_quality_scalar(std::span<const std::uint8_t> source,
                           Extent source_extent,
                           std::size_t source_stride,
                           std::span<std::uint8_t> destination,
                           Extent destination_extent,
                           std::size_t destination_stride,
                           std::uint64_t max_temporary_bytes)
    -> std::expected<void, Error> {
  if (source_extent.width == 0 || source_extent.height == 0 ||
      destination_extent.width == 0 || destination_extent.height == 0) {
    return std::unexpected{invalid_dimensions};
  }
  const auto source_required =
      checked_storage_size(source_extent, source_stride);
  if (!source_required) {
    return std::unexpected{source_required.error()};
  }
  const auto destination_required =
      checked_storage_size(destination_extent, destination_stride);
  if (!destination_required) {
    return std::unexpected{destination_required.error()};
  }
  if (source.size() < *source_required ||
      destination.size() < *destination_required) {
    return std::unexpected{invalid_storage};
  }

  const auto plan = make_quality_filter_plan(
      source_extent, destination_extent, max_temporary_bytes);
  if (!plan) {
    return std::unexpected{plan.error()};
  }

  for (std::uint32_t destination_y = 0;
       destination_y < destination_extent.height; ++destination_y) {
    const auto &vertical_span = plan->vertical.spans[destination_y];
    for (std::uint32_t destination_x = 0;
         destination_x < destination_extent.width; ++destination_x) {
      const auto &horizontal_span = plan->horizontal.spans[destination_x];
      UnsignedWide vertical_numerator = 0;

      for (std::size_t vertical_offset = 0;
           vertical_offset < vertical_span.tap_count; ++vertical_offset) {
        const auto &vertical_tap =
            plan->vertical.taps[vertical_span.first_tap + vertical_offset];
        UnsignedWide horizontal_numerator = 0;

        for (std::size_t horizontal_offset = 0;
             horizontal_offset < horizontal_span.tap_count;
             ++horizontal_offset) {
          const auto &horizontal_tap = plan->horizontal.taps[
              horizontal_span.first_tap + horizontal_offset];
          const auto source_offset =
              (static_cast<std::size_t>(vertical_tap.source_index) *
               source_stride) +
              horizontal_tap.source_index;
          horizontal_numerator +=
              static_cast<UnsignedWide>(source[source_offset]) *
              horizontal_tap.weight;
        }

        const auto horizontal_value = rounded_divide(
            horizontal_numerator, horizontal_span.weight_sum);
        vertical_numerator +=
            static_cast<UnsignedWide>(horizontal_value) * vertical_tap.weight;
      }

      const auto destination_offset =
          (static_cast<std::size_t>(destination_y) *
           destination_stride) +
          destination_x;
      destination[destination_offset] = static_cast<std::uint8_t>(
          rounded_divide(vertical_numerator, vertical_span.weight_sum));
    }
  }
  return {};
}

auto resize_quality_rgba(ImageView source, Rect source_rect,
                         MutableImageView destination, Rect destination_rect,
                         const QualityFilterPlan &plan)
    -> std::expected<void, Error> {
  const auto source_extent = source.extent();
  const auto destination_extent = destination.extent();
  const auto rect_fits = [](Rect rect, Extent extent) noexcept {
    return rect.width != 0 && rect.height != 0 && rect.x <= extent.width &&
           rect.y <= extent.height && rect.width <= extent.width - rect.x &&
           rect.height <= extent.height - rect.y;
  };
  if (!rect_fits(source_rect, source_extent) ||
      !rect_fits(destination_rect, destination_extent) ||
      plan.horizontal.spans.size() != destination_rect.width ||
      plan.vertical.spans.size() != destination_rect.height) {
    return std::unexpected{invalid_storage};
  }

  for (std::uint32_t destination_y = 0;
       destination_y < destination_rect.height; ++destination_y) {
    const auto &vertical_span = plan.vertical.spans[destination_y];
    auto destination_row = destination.row(destination_rect.y + destination_y);
    if (!destination_row) {
      return std::unexpected{destination_row.error()};
    }

    for (std::uint32_t destination_x = 0;
         destination_x < destination_rect.width; ++destination_x) {
      const auto &horizontal_span = plan.horizontal.spans[destination_x];
      UnsignedWide vertical_red = 0;
      UnsignedWide vertical_green = 0;
      UnsignedWide vertical_blue = 0;
      UnsignedWide vertical_alpha = 0;

      for (std::size_t vertical_offset = 0;
           vertical_offset < vertical_span.tap_count; ++vertical_offset) {
        const auto &vertical_tap =
            plan.vertical.taps[vertical_span.first_tap + vertical_offset];
        const auto source_row =
            source.row(source_rect.y + vertical_tap.source_index);
        if (!source_row) {
          return std::unexpected{source_row.error()};
        }

        UnsignedWide horizontal_red = 0;
        UnsignedWide horizontal_green = 0;
        UnsignedWide horizontal_blue = 0;
        UnsignedWide horizontal_alpha = 0;
        for (std::size_t horizontal_offset = 0;
             horizontal_offset < horizontal_span.tap_count;
             ++horizontal_offset) {
          const auto &horizontal_tap = plan.horizontal.taps[
              horizontal_span.first_tap + horizontal_offset];
          const auto pixel = premultiply_srgba(
              (*source_row)[source_rect.x + horizontal_tap.source_index]);
          horizontal_red += static_cast<UnsignedWide>(pixel.red_times_alpha) *
                            horizontal_tap.weight;
          horizontal_green +=
              static_cast<UnsignedWide>(pixel.green_times_alpha) *
              horizontal_tap.weight;
          horizontal_blue +=
              static_cast<UnsignedWide>(pixel.blue_times_alpha) *
              horizontal_tap.weight;
          horizontal_alpha += static_cast<UnsignedWide>(pixel.alpha) *
                              horizontal_tap.weight;
        }

        vertical_red +=
            rounded_divide(horizontal_red, horizontal_span.weight_sum) *
            vertical_tap.weight;
        vertical_green +=
            rounded_divide(horizontal_green, horizontal_span.weight_sum) *
            vertical_tap.weight;
        vertical_blue +=
            rounded_divide(horizontal_blue, horizontal_span.weight_sum) *
            vertical_tap.weight;
        vertical_alpha +=
            rounded_divide(horizontal_alpha, horizontal_span.weight_sum) *
            vertical_tap.weight;
      }

      const PremultipliedSrgbaProduct filtered{
          .red_times_alpha = static_cast<std::uint16_t>(
              rounded_divide(vertical_red, vertical_span.weight_sum)),
          .green_times_alpha = static_cast<std::uint16_t>(
              rounded_divide(vertical_green, vertical_span.weight_sum)),
          .blue_times_alpha = static_cast<std::uint16_t>(
              rounded_divide(vertical_blue, vertical_span.weight_sum)),
          .alpha = static_cast<std::uint8_t>(
              rounded_divide(vertical_alpha, vertical_span.weight_sum)),
      };
      (*destination_row)[destination_rect.x + destination_x] =
          unpremultiply_srgba(filtered);
    }
  }
  return {};
}

} // namespace rasterforge::detail
