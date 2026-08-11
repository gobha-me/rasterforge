#include <catch2/catch_test_macros.hpp>

#include <rasterforge/rasterforge.hpp>

#include "../../src/lib/premultiplied_rgba.hpp"
#include "../../src/lib/quality_filter.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace rf = rasterforge;
namespace detail = rasterforge::detail;

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
  for (std::uint32_t y = 0; y < view.extent().height; ++y) {
    const auto row = view.row(y);
    REQUIRE(row);
    pixels.insert(pixels.end(), row->begin(), row->end());
  }
  return pixels;
}

constexpr rf::Rgba8 transparent_black{0, 0, 0, 0};

static_assert(static_cast<std::uint8_t>(rf::ResizeFilter::nearest) == 0);
static_assert(static_cast<std::uint8_t>(rf::ResizeFilter::triangle) == 1);

} // namespace

TEST_CASE("premultiplied sRGBA products have an exact zero-alpha contract",
          "[fit][quality][alpha][conversion]") {
  constexpr rf::Rgba8 translucent{37, 149, 211, 83};
  constexpr auto product = detail::premultiply_srgba(translucent);
  STATIC_REQUIRE(product.red_times_alpha == 37U * 83U);
  STATIC_REQUIRE(product.green_times_alpha == 149U * 83U);
  STATIC_REQUIRE(product.blue_times_alpha == 211U * 83U);
  STATIC_REQUIRE(product.alpha == 83);
  STATIC_REQUIRE(detail::unpremultiply_srgba(product) == translucent);

  constexpr rf::Rgba8 colored_transparent{255, 31, 127, 0};
  STATIC_REQUIRE(detail::premultiply_srgba(colored_transparent) ==
                 detail::PremultipliedSrgbaProduct{});
  STATIC_REQUIRE(detail::unpremultiply_srgba(detail::premultiply_srgba(
                     colored_transparent)) == transparent_black);
}

TEST_CASE("quality fit rejects invalid filters and temporary budgets",
          "[fit][quality][failure][limits]") {
  constexpr std::array pixels{rf::Rgba8{10, 20, 30, 40},
                              rf::Rgba8{50, 60, 70, 80}};
  auto source = make_image({2, 1}, pixels);

  SECTION("unrecognized filter") {
    const auto result =
        rf::fit(source.view(), {3, 1}, rf::Fit::stretch, {}, transparent_black,
                {}, static_cast<rf::ResizeFilter>(std::uint8_t{255}));
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::invalid_argument);
  }

  SECTION("zero budget") {
    rf::Limits limits{};
    limits.max_temporary_bytes = 0;
    const auto result =
        rf::fit(source.view(), {3, 1}, rf::Fit::stretch, {}, transparent_black,
                limits, rf::ResizeFilter::triangle);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("exact budget succeeds and one byte short fails") {
    const auto measured = detail::make_quality_filter_plan(
        {2, 1}, {3, 1}, std::numeric_limits<std::uint64_t>::max());
    REQUIRE(measured);

    rf::Limits limits{};
    limits.max_temporary_bytes = measured->temporary_bytes;
    const auto exact =
        rf::fit(source.view(), {3, 1}, rf::Fit::stretch, {}, transparent_black,
                limits, rf::ResizeFilter::triangle);
    REQUIRE(exact);

    limits.max_temporary_bytes = measured->temporary_bytes - 1;
    const auto short_budget =
        rf::fit(source.view(), {3, 1}, rf::Fit::stretch, {}, transparent_black,
                limits, rf::ResizeFilter::triangle);
    REQUIRE_FALSE(short_budget);
    REQUIRE(short_budget.error().code == rf::ErrorCode::resource_limit);
  }
}

TEST_CASE("quality identity preserves opaque and translucent straight colors",
          "[fit][quality][identity][alpha]") {
  constexpr std::array pixels{rf::Rgba8{1, 2, 3, 255},
                              rf::Rgba8{37, 149, 211, 83},
                              rf::Rgba8{255, 31, 127, 0}};
  auto source = make_image({3, 1}, pixels);
  const auto result =
      rf::fit(source.view(), {3, 1}, rf::Fit::stretch, {}, transparent_black,
              {}, rf::ResizeFilter::triangle);
  REQUIRE(result);
  REQUIRE(pixels_of(result->view()) ==
          std::vector<rf::Rgba8>{pixels[0], pixels[1], transparent_black});
}

TEST_CASE("quality filtering ignores color carried by transparent neighbors",
          "[fit][quality][alpha][regression]") {
  constexpr std::array pixels{rf::Rgba8{255, 0, 0, 255},
                              rf::Rgba8{0, 0, 255, 0}};
  auto source = make_image({2, 1}, pixels);
  const auto result =
      rf::fit(source.view(), {4, 1}, rf::Fit::stretch, {}, transparent_black,
              {}, rf::ResizeFilter::triangle);
  REQUIRE(result);
  REQUIRE(pixels_of(result->view()) ==
          std::vector<rf::Rgba8>{{255, 0, 0, 255},
                                 {255, 0, 0, 191},
                                 {254, 0, 0, 64},
                                 transparent_black});
}

TEST_CASE("quality fit applies planned crop and matte rectangles",
          "[fit][quality][geometry][matte]") {
  constexpr rf::Rgba8 matte{9, 8, 7, 6};

  SECTION("contain leaves the exact matte outside the filtered rectangle") {
    constexpr std::array pixels{rf::Rgba8{200, 10, 20, 128},
                                rf::Rgba8{200, 10, 20, 128}};
    auto source = make_image({2, 1}, pixels);
    const auto result = rf::fit(source.view(), {3, 3}, rf::Fit::contain, {},
                                matte, {}, rf::ResizeFilter::triangle);
    REQUIRE(result);
    REQUIRE(pixels_of(result->view()) ==
            std::vector<rf::Rgba8>{matte, matte, matte, pixels[0], pixels[0],
                                   pixels[0], pixels[0], pixels[0], pixels[0]});
  }

  SECTION("cover honors both focal endpoints before filtering") {
    constexpr std::array pixels{
        rf::Rgba8{10, 0, 0, 255}, rf::Rgba8{20, 0, 0, 255},
        rf::Rgba8{30, 0, 0, 255}, rf::Rgba8{40, 0, 0, 255}};
    auto source = make_image({4, 1}, pixels);
    const auto left =
        rf::fit(source.view(), {2, 1}, rf::Fit::cover, {0.0F, 0.5F}, matte, {},
                rf::ResizeFilter::triangle);
    const auto right =
        rf::fit(source.view(), {2, 1}, rf::Fit::cover, {1.0F, 0.5F}, matte, {},
                rf::ResizeFilter::triangle);
    REQUIRE(left);
    REQUIRE(right);
    REQUIRE(pixels_of(left->view()) ==
            std::vector<rf::Rgba8>{pixels[0], pixels[1]});
    REQUIRE(pixels_of(right->view()) ==
            std::vector<rf::Rgba8>{pixels[2], pixels[3]});
  }

  SECTION("no-scale uses matching crop and matte rectangles") {
    constexpr std::array pixels{rf::Rgba8{10, 0, 0, 255},
                                rf::Rgba8{20, 0, 0, 255},
                                rf::Rgba8{30, 0, 0, 255}};
    auto source = make_image({3, 1}, pixels);
    const auto result =
        rf::fit(source.view(), {2, 3}, rf::Fit::none, {1.0F, 0.0F}, matte, {},
                rf::ResizeFilter::triangle);
    REQUIRE(result);
    REQUIRE(pixels_of(result->view()) ==
            std::vector<rf::Rgba8>{pixels[1], pixels[2], matte, matte, matte,
                                   matte});
  }
}
