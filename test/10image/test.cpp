#include <catch2/catch_test_macros.hpp>

#include <rasterforge/rasterforge.hpp>

#include <cstdint>
#include <limits>
#include <utility>

namespace rf = rasterforge;

TEST_CASE("Image::create rejects invalid dimensions before allocating",
          "[image][failure]") {
  SECTION("zero width") {
    const auto image = rf::Image::create({0, 1});
    REQUIRE_FALSE(image);
    REQUIRE(image.error().code == rf::ErrorCode::invalid_dimensions);
  }

  SECTION("zero height") {
    const auto image = rf::Image::create({1, 0});
    REQUIRE_FALSE(image);
    REQUIRE(image.error().code == rf::ErrorCode::invalid_dimensions);
  }

  SECTION("dimension limit") {
    rf::Limits limits{};
    limits.max_dimension = 8;
    const auto image = rf::Image::create({9, 1}, limits);
    REQUIRE_FALSE(image);
    REQUIRE(image.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("pixel limit") {
    rf::Limits limits{};
    limits.max_pixels = 15;
    const auto image = rf::Image::create({4, 4}, limits);
    REQUIRE_FALSE(image);
    REQUIRE(image.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("byte limit") {
    rf::Limits limits{};
    limits.max_output_bytes = 15;
    const auto image = rf::Image::create({2, 2}, limits);
    REQUIRE_FALSE(image);
    REQUIRE(image.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("byte-count overflow is rejected") {
    rf::Limits limits{
        .max_input_bytes = std::numeric_limits<std::uint64_t>::max(),
        .max_pixels = std::numeric_limits<std::uint64_t>::max(),
        .max_output_bytes = std::numeric_limits<std::uint64_t>::max(),
        .max_dimension = std::numeric_limits<std::uint32_t>::max(),
    };
    const auto image =
        rf::Image::create({std::numeric_limits<std::uint32_t>::max(),
                           std::numeric_limits<std::uint32_t>::max()},
                          limits);
    REQUIRE_FALSE(image);
    REQUIRE(image.error().code == rf::ErrorCode::resource_limit);
  }
}

TEST_CASE("Image views preserve the storage invariants", "[image][view]") {
  auto result = rf::Image::create({2, 2});
  REQUIRE(result);
  auto image = std::move(*result);

  REQUIRE((image.extent() == rf::Extent{2, 2}));
  REQUIRE(image.stride_bytes() == 2 * sizeof(rf::Rgba8));
  REQUIRE(image.size_bytes() == 4 * sizeof(rf::Rgba8));

  auto mutable_row = image.mutable_view().row(1);
  REQUIRE(mutable_row);
  (*mutable_row)[0] = rf::Rgba8{1, 2, 3, 4};

  const auto view = image.view();
  const auto row = view.row(1);
  REQUIRE(row);
  REQUIRE(((*row)[0] == rf::Rgba8{1, 2, 3, 4}));

  const auto past_end = view.row(2);
  REQUIRE_FALSE(past_end);
  REQUIRE(past_end.error().code == rf::ErrorCode::row_out_of_range);
}

TEST_CASE("A moved-from Image is a safe empty owner", "[image][move]") {
  auto result = rf::Image::create({1, 1});
  REQUIRE(result);
  auto source = std::move(*result);
  auto destination = std::move(source);

  REQUIRE((destination.extent() == rf::Extent{1, 1}));
  REQUIRE(source.extent() == rf::Extent{});
  REQUIRE(source.stride_bytes() == 0);
  REQUIRE(source.size_bytes() == 0);

  const auto row = source.view().row(0);
  REQUIRE_FALSE(row);
  REQUIRE(row.error().code == rf::ErrorCode::row_out_of_range);
}
