#include <catch2/catch_test_macros.hpp>

#include <rasterforge/rasterforge.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace rf = rasterforge;

namespace {

auto make_image(rf::Extent extent, std::span<const rf::Rgba8> pixels)
    -> rf::Image {
  auto image = rf::Image::create(extent);
  REQUIRE(image);
  REQUIRE(pixels.size() ==
          static_cast<std::size_t>(extent.width) * extent.height);

  for (std::uint32_t y = 0; y < extent.height; ++y) {
    auto row = image->mutable_view().row(y);
    REQUIRE(row);
    const auto offset = static_cast<std::size_t>(y) * extent.width;
    std::ranges::copy(pixels.subspan(offset, extent.width), row->begin());
  }
  return std::move(*image);
}

auto pixels_of(rf::ImageView view) -> std::vector<rf::Rgba8> {
  std::vector<rf::Rgba8> pixels;
  pixels.reserve(static_cast<std::size_t>(view.extent().width) *
                 view.extent().height);
  for (std::uint32_t y = 0; y < view.extent().height; ++y) {
    const auto row = view.row(y);
    REQUIRE(row);
    pixels.insert(pixels.end(), row->begin(), row->end());
  }
  return pixels;
}

constexpr rf::Rgba8 transparent_black{0, 0, 0, 0};

} // namespace

TEST_CASE("source-over rejects invalid extents before compositing",
          "[composite][failure][extent]") {
  constexpr std::array one_pixel{rf::Rgba8{10, 20, 30, 40}};
  constexpr std::array two_pixels{rf::Rgba8{1, 2, 3, 4}, rf::Rgba8{5, 6, 7, 8}};
  auto source = make_image({1, 1}, one_pixel);
  auto backdrop = make_image({2, 1}, two_pixels);

  SECTION("image backdrops must have the same extent") {
    const auto result = rf::composite_over(source.view(), backdrop.view());
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::invalid_argument);
  }

  SECTION("an empty source is not an image") {
    const auto result = rf::composite_over({}, rf::Rgba8{});
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::invalid_dimensions);
  }

  SECTION("matching empty views still fail image validation") {
    const auto result = rf::composite_over(rf::ImageView{}, rf::ImageView{});
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::invalid_dimensions);
  }
}

TEST_CASE("source-over enforces only destination allocation limits",
          "[composite][failure][limits]") {
  constexpr std::array pixels{rf::Rgba8{10, 20, 30, 40}};
  auto source = make_image({1, 1}, pixels);

  SECTION("dimension limit") {
    rf::Limits limits{};
    limits.max_dimension = 0;
    const auto result =
        rf::composite_over(source.view(), transparent_black, limits);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("pixel limit") {
    rf::Limits limits{};
    limits.max_pixels = 0;
    const auto result =
        rf::composite_over(source.view(), transparent_black, limits);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("output-byte limit") {
    rf::Limits limits{};
    limits.max_output_bytes = sizeof(rf::Rgba8) - 1;
    const auto result =
        rf::composite_over(source.view(), transparent_black, limits);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("exact output limits accept while unused budgets may be zero") {
    rf::Limits limits{};
    limits.max_input_bytes = 0;
    limits.max_dimension = 1;
    limits.max_pixels = 1;
    limits.max_output_bytes = sizeof(rf::Rgba8);
    limits.max_temporary_bytes = 0;
    const auto result =
        rf::composite_over(source.view(), transparent_black, limits);
    REQUIRE(result);
  }
}

TEST_CASE("source-over has exact alpha endpoint behavior",
          "[composite][alpha][identity]") {
  constexpr rf::Rgba8 colored_transparent_source{250, 40, 90, 0};
  constexpr rf::Rgba8 translucent_backdrop{11, 22, 33, 200};
  constexpr rf::Rgba8 opaque_source{7, 8, 9, 255};
  constexpr rf::Rgba8 colored_transparent_backdrop{80, 70, 60, 0};
  constexpr rf::Rgba8 translucent_source{200, 100, 50, 128};

  SECTION("a transparent colored source preserves the backdrop") {
    const std::array pixels{colored_transparent_source};
    auto source = make_image({1, 1}, pixels);
    const auto result = rf::composite_over(source.view(), translucent_backdrop);
    REQUIRE(result);
    REQUIRE(pixels_of(result->view()) ==
            std::vector<rf::Rgba8>{translucent_backdrop});
  }

  SECTION("an opaque source replaces the backdrop exactly") {
    const std::array pixels{opaque_source};
    auto source = make_image({1, 1}, pixels);
    const auto result =
        rf::composite_over(source.view(), colored_transparent_backdrop);
    REQUIRE(result);
    REQUIRE(pixels_of(result->view()) == std::vector<rf::Rgba8>{opaque_source});
  }

  SECTION("a transparent backdrop preserves a nonzero-alpha source") {
    const std::array pixels{translucent_source};
    auto source = make_image({1, 1}, pixels);
    const auto result =
        rf::composite_over(source.view(), colored_transparent_backdrop);
    REQUIRE(result);
    REQUIRE(pixels_of(result->view()) ==
            std::vector<rf::Rgba8>{translucent_source});
  }

  SECTION("two alpha-zero pixels canonicalize to transparent black") {
    const std::array pixels{colored_transparent_source};
    auto source = make_image({1, 1}, pixels);
    const auto result =
        rf::composite_over(source.view(), colored_transparent_backdrop);
    REQUIRE(result);
    REQUIRE(pixels_of(result->view()) ==
            std::vector<rf::Rgba8>{transparent_black});
  }
}

TEST_CASE("source-over uses deterministic half-up integer rounding",
          "[composite][alpha][rounding]") {
  SECTION("a translucent edge over an opaque backdrop has exact bytes") {
    constexpr std::array pixels{rf::Rgba8{255, 0, 0, 128}};
    auto source = make_image({1, 1}, pixels);
    const auto result =
        rf::composite_over(source.view(), rf::Rgba8{0, 0, 255, 255});
    REQUIRE(result);
    REQUIRE(pixels_of(result->view()) ==
            std::vector<rf::Rgba8>{{128, 0, 127, 255}});
  }

  SECTION("an exact straight-channel half rounds upward") {
    constexpr std::array pixels{rf::Rgba8{0, 0, 0, 2}};
    auto source = make_image({1, 1}, pixels);
    const auto result =
        rf::composite_over(source.view(), rf::Rgba8{254, 0, 0, 2});
    REQUIRE(result);
    REQUIRE(pixels_of(result->view()) ==
            std::vector<rf::Rgba8>{{127, 0, 0, 4}});
  }
}

TEST_CASE("solid and image backdrops share the same row-major operation",
          "[composite][overlay]") {
  constexpr std::array source_pixels{
      rf::Rgba8{255, 0, 0, 255}, rf::Rgba8{255, 0, 0, 128},
      rf::Rgba8{10, 20, 30, 0}, rf::Rgba8{10, 20, 30, 64}};
  constexpr rf::Rgba8 solid{0, 0, 255, 255};
  constexpr std::array backdrop_pixels{solid, solid, solid, solid};
  auto source = make_image({2, 2}, source_pixels);
  auto backdrop = make_image({2, 2}, backdrop_pixels);

  const auto over_solid = rf::composite_over(source.view(), solid);
  const auto over_image = rf::composite_over(source.view(), backdrop.view());
  REQUIRE(over_solid);
  REQUIRE(over_image);
  REQUIRE(pixels_of(over_solid->view()) == pixels_of(over_image->view()));
  REQUIRE(pixels_of(over_solid->view()) ==
          std::vector<rf::Rgba8>{{255, 0, 0, 255},
                                 {128, 0, 127, 255},
                                 {0, 0, 255, 255},
                                 {3, 5, 199, 255}});
}

TEST_CASE("read-only composite inputs may alias",
          "[composite][overlay][alias]") {
  constexpr std::array pixels{rf::Rgba8{100, 50, 25, 128}};
  auto image = make_image({1, 1}, pixels);
  const auto result = rf::composite_over(image.view(), image.view());
  REQUIRE(result);
  REQUIRE(pixels_of(result->view()) ==
          std::vector<rf::Rgba8>{{100, 50, 25, 192}});
}
