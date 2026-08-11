#include "../../src/lib/quality_filter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <rasterforge/rasterforge.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <numeric>
#include <span>
#include <vector>

namespace rf = rasterforge;
namespace detail = rasterforge::detail;

namespace {

[[nodiscard]] auto resize(std::span<const std::uint8_t> source,
                          rf::Extent source_extent,
                          rf::Extent destination_extent)
    -> std::vector<std::uint8_t> {
  std::vector<std::uint8_t> destination(
      static_cast<std::size_t>(destination_extent.width) *
      destination_extent.height);
  const auto result = detail::resize_quality_scalar(
      source, source_extent, source_extent.width, destination,
      destination_extent, destination_extent.width,
      std::numeric_limits<std::uint64_t>::max());
  REQUIRE(result);
  return destination;
}

void require_axis_invariants(const detail::QualityFilterAxisPlan &axis,
                             std::uint32_t source_length,
                             std::uint32_t destination_length) {
  REQUIRE(axis.spans.size() == destination_length);
  std::size_t expected_first = 0;
  for (const auto &filter_span : axis.spans) {
    REQUIRE(filter_span.first_tap == expected_first);
    REQUIRE(filter_span.tap_count > 0);
    REQUIRE(filter_span.weight_sum > 0);

    std::uint64_t sum = 0;
    std::uint32_t previous = 0;
    for (std::size_t offset = 0; offset < filter_span.tap_count; ++offset) {
      const auto &tap = axis.taps[filter_span.first_tap + offset];
      REQUIRE(tap.source_index < source_length);
      REQUIRE(tap.weight > 0);
      if (offset != 0) {
        REQUIRE(tap.source_index == previous + 1);
      }
      previous = tap.source_index;
      sum += tap.weight;
    }
    REQUIRE(sum == filter_span.weight_sum);
    expected_first += filter_span.tap_count;
  }
  REQUIRE(expected_first == axis.taps.size());
}

} // namespace

TEST_CASE("quality-filter planning rejects invalid and unbounded work",
          "[quality][failure][limits]") {
  SECTION("every zero source or destination axis is invalid") {
    for (const auto source : {rf::Extent{0, 1}, rf::Extent{1, 0}}) {
      const auto result = detail::make_quality_filter_plan(
          source, {1, 1}, std::numeric_limits<std::uint64_t>::max());
      REQUIRE_FALSE(result);
      REQUIRE(result.error().code == rf::ErrorCode::invalid_dimensions);
    }
    for (const auto destination : {rf::Extent{0, 1}, rf::Extent{1, 0}}) {
      const auto result = detail::make_quality_filter_plan(
          {1, 1}, destination, std::numeric_limits<std::uint64_t>::max());
      REQUIRE_FALSE(result);
      REQUIRE(result.error().code == rf::ErrorCode::invalid_dimensions);
    }
  }

  SECTION("zero temporary budget fails before coefficient allocation") {
    const auto result =
        detail::make_quality_filter_plan({1, 1}, {1, 1}, 0);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("extreme destination length is rejected by a small budget") {
    const auto result = detail::make_quality_filter_plan(
        {1, 1}, {std::numeric_limits<std::uint32_t>::max(), 1}, 1024);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("extreme source support is rejected before coefficient work") {
    const auto result = detail::make_quality_filter_plan(
        {std::numeric_limits<std::uint32_t>::max(), 1}, {1, 1}, 1024);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }
}

TEST_CASE("quality-filter temporary accounting is exact and inclusive",
          "[quality][limits]") {
  const auto measured = detail::make_quality_filter_plan(
      {7, 5}, {3, 9}, std::numeric_limits<std::uint64_t>::max());
  REQUIRE(measured);
  REQUIRE(measured->temporary_bytes > 0);

  const auto exact = detail::make_quality_filter_plan(
      {7, 5}, {3, 9}, measured->temporary_bytes);
  REQUIRE(exact);
  REQUIRE(exact->temporary_bytes == measured->temporary_bytes);

  const auto below = detail::make_quality_filter_plan(
      {7, 5}, {3, 9}, measured->temporary_bytes - 1);
  REQUIRE_FALSE(below);
  REQUIRE(below.error().code == rf::ErrorCode::resource_limit);
}

TEST_CASE("quality-filter plans contain bounded normalized axis spans",
          "[quality][coefficients][invariant]") {
  for (std::uint32_t source_width = 1; source_width <= 9; ++source_width) {
    for (std::uint32_t source_height = 1; source_height <= 7;
         ++source_height) {
      for (std::uint32_t destination_width = 1; destination_width <= 8;
           ++destination_width) {
        for (std::uint32_t destination_height = 1; destination_height <= 6;
             ++destination_height) {
          CAPTURE(source_width, source_height, destination_width,
                  destination_height);
          const auto plan = detail::make_quality_filter_plan(
              {source_width, source_height},
              {destination_width, destination_height},
              std::numeric_limits<std::uint64_t>::max());
          REQUIRE(plan);
          require_axis_invariants(plan->horizontal, source_width,
                                  destination_width);
          require_axis_invariants(plan->vertical, source_height,
                                  destination_height);
        }
      }
    }
  }
}

TEST_CASE("quality scalar storage is validated before output changes",
          "[quality][failure][storage]") {
  const std::array source{std::uint8_t{1}, std::uint8_t{2},
                          std::uint8_t{3}, std::uint8_t{4}};
  std::array destination{std::uint8_t{90}, std::uint8_t{91},
                         std::uint8_t{92}, std::uint8_t{93}};
  const auto original = destination;

  SECTION("short source") {
    const auto result = detail::resize_quality_scalar(
        std::span{source}.first<3>(), {2, 2}, 2, destination, {2, 2}, 2,
        4096);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::invalid_argument);
  }

  SECTION("short destination") {
    const auto result = detail::resize_quality_scalar(
        source, {2, 2}, 2, std::span{destination}.first<3>(), {2, 2}, 2,
        4096);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::invalid_argument);
  }

  SECTION("temporary budget failure") {
    const auto result = detail::resize_quality_scalar(
        source, {2, 2}, 2, destination, {2, 2}, 2, 0);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }

  REQUIRE(destination == original);
}

TEST_CASE("quality scalar honors validated source and destination strides",
          "[quality][stride]") {
  const std::array source{std::uint8_t{0}, std::uint8_t{100},
                          std::uint8_t{250}, std::uint8_t{150},
                          std::uint8_t{200}, std::uint8_t{251}};
  std::array destination{std::uint8_t{90}, std::uint8_t{91},
                         std::uint8_t{92}, std::uint8_t{93},
                         std::uint8_t{94}, std::uint8_t{95},
                         std::uint8_t{96}, std::uint8_t{97},
                         std::uint8_t{98}, std::uint8_t{99},
                         std::uint8_t{100}, std::uint8_t{101}};

  SECTION("padding is neither sampled nor overwritten") {
    const auto result = detail::resize_quality_scalar(
        source, {2, 2}, 3, std::span{destination}.subspan(1), {3, 3}, 4,
        4096);
    REQUIRE(result);
    REQUIRE(destination ==
            std::array<std::uint8_t, 12>{90, 0, 50, 100, 94, 75, 113, 150,
                                         98, 150, 175, 200});
  }

  SECTION("short strides are rejected without changing output") {
    const auto original = destination;
    const auto bad_source = detail::resize_quality_scalar(
        source, {2, 2}, 1, destination, {2, 2}, 2, 4096);
    REQUIRE_FALSE(bad_source);
    REQUIRE(bad_source.error().code == rf::ErrorCode::invalid_argument);
    REQUIRE(destination == original);

    const auto bad_destination = detail::resize_quality_scalar(
        source, {2, 2}, 3, destination, {2, 2}, 1, 4096);
    REQUIRE_FALSE(bad_destination);
    REQUIRE(bad_destination.error().code == rf::ErrorCode::invalid_argument);
    REQUIRE(destination == original);
  }
}

TEST_CASE("quality scalar identity and one-pixel edges are exact",
          "[quality][identity][edge]") {
  const std::array pixels{std::uint8_t{0}, std::uint8_t{1},
                          std::uint8_t{127}, std::uint8_t{128},
                          std::uint8_t{254}, std::uint8_t{255}};
  REQUIRE(resize(pixels, {3, 2}, {3, 2}) ==
          std::vector<std::uint8_t>{pixels.begin(), pixels.end()});

  const std::array one{std::uint8_t{173}};
  REQUIRE(resize(one, {1, 1}, {7, 5}) ==
          std::vector<std::uint8_t>(35, 173));
}

TEST_CASE("quality scalar enlargement follows triangle interpolation",
          "[quality][upscale][rounding]") {
  const std::array ramp{std::uint8_t{0}, std::uint8_t{255}};
  REQUIRE(resize(ramp, {2, 1}, {4, 1}) ==
          std::vector<std::uint8_t>{0, 64, 191, 255});

  const std::array grid{std::uint8_t{0}, std::uint8_t{100},
                        std::uint8_t{150}, std::uint8_t{200}};
  REQUIRE(resize(grid, {2, 2}, {3, 3}) ==
          std::vector<std::uint8_t>{0, 50, 100, 75, 113, 150, 150, 175,
                                    200});
}

TEST_CASE("quality scalar reduction widens support and clamps edges",
          "[quality][downscale][edge]") {
  const std::array ramp{std::uint8_t{0}, std::uint8_t{64},
                        std::uint8_t{128}, std::uint8_t{255}};
  REQUIRE(resize(ramp, {4, 1}, {2, 1}) ==
          std::vector<std::uint8_t>{40, 184});

  const std::vector<std::uint8_t> constant(64 * 48, 137);
  REQUIRE(resize(constant, {64, 48}, {1, 1}) ==
          std::vector<std::uint8_t>{137});

  std::array<std::uint8_t, 8> left_impulse{};
  std::array<std::uint8_t, 8> right_impulse{};
  left_impulse.front() = 255;
  right_impulse.back() = 255;
  const auto left = resize(left_impulse, {8, 1}, {1, 1});
  const auto right = resize(right_impulse, {8, 1}, {1, 1});
  REQUIRE(left == right);
  REQUIRE(left.front() > 0);

  std::array<std::uint8_t, 8> top_impulse{};
  std::array<std::uint8_t, 8> bottom_impulse{};
  top_impulse.front() = 255;
  bottom_impulse.back() = 255;
  const auto top = resize(top_impulse, {1, 8}, {1, 1});
  const auto bottom = resize(bottom_impulse, {1, 8}, {1, 1});
  REQUIRE(top == bottom);
  REQUIRE(top.front() > 0);
}
