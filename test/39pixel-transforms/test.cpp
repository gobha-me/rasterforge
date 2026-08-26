#include <catch2/catch_test_macros.hpp>

#include <rasterforge/rasterforge.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
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

constexpr std::array one_pixel{rf::Rgba8{20, 40, 80, 160}};

} // namespace

TEST_CASE("pixel transforms reject non-finite scalar factors before allocation",
          "[pixel-transform][failure][factor]") {
  auto source = make_image({1, 1}, one_pixel);
  rf::Limits impossible_limits{};
  impossible_limits.max_dimension = 0;

  for (const auto factor : {std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity()}) {
    const auto dimmed = rf::dim(source.view(), factor, impossible_limits);
    REQUIRE_FALSE(dimmed);
    REQUIRE(dimmed.error().code == rf::ErrorCode::invalid_argument);

    const auto faded =
        rf::adjust_opacity(source.view(), factor, impossible_limits);
    REQUIRE_FALSE(faded);
    REQUIRE(faded.error().code == rf::ErrorCode::invalid_argument);
  }
}

TEST_CASE("pixel transforms reject empty images and enforce output limits",
          "[pixel-transform][failure][limits]") {
  SECTION("empty views are not images") {
    const auto tinted = rf::tint({}, {255, 255, 255});
    const auto dimmed = rf::dim({}, 1.0F);
    const auto faded = rf::adjust_opacity({}, 1.0F);
    REQUIRE_FALSE(tinted);
    REQUIRE_FALSE(dimmed);
    REQUIRE_FALSE(faded);
    REQUIRE(tinted.error().code == rf::ErrorCode::invalid_dimensions);
    REQUIRE(dimmed.error().code == rf::ErrorCode::invalid_dimensions);
    REQUIRE(faded.error().code == rf::ErrorCode::invalid_dimensions);
  }

  auto source = make_image({1, 1}, one_pixel);

  SECTION("dimension limits apply") {
    rf::Limits limits{};
    limits.max_dimension = 0;
    const auto result = rf::tint(source.view(), {255, 255, 255}, limits);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("pixel limits apply") {
    rf::Limits limits{};
    limits.max_pixels = 0;
    const auto result = rf::dim(source.view(), 1.0F, limits);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("output-byte limits apply") {
    rf::Limits limits{};
    limits.max_output_bytes = sizeof(rf::Rgba8) - 1;
    const auto result = rf::adjust_opacity(source.view(), 1.0F, limits);
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
    const auto result = rf::dim(source.view(), 0.5F, limits);
    REQUIRE(result);
  }
}

TEST_CASE("finite scalar factors clamp to the normalized range",
          "[pixel-transform][factor][clamp]") {
  auto source = make_image({1, 1}, one_pixel);

  for (const auto factor : {-4.0F, 0.0F}) {
    const auto dimmed = rf::dim(source.view(), factor);
    REQUIRE(dimmed);
    REQUIRE(pixels_of(dimmed->view()) ==
            std::vector<rf::Rgba8>{{0, 0, 0, 160}});

    const auto faded = rf::adjust_opacity(source.view(), factor);
    REQUIRE(faded);
    REQUIRE(pixels_of(faded->view()) ==
            std::vector<rf::Rgba8>{{20, 40, 80, 0}});
  }

  for (const auto factor : {1.0F, 4.0F}) {
    const auto dimmed = rf::dim(source.view(), factor);
    const auto faded = rf::adjust_opacity(source.view(), factor);
    REQUIRE(dimmed);
    REQUIRE(faded);
    REQUIRE(pixels_of(dimmed->view()) ==
            std::vector<rf::Rgba8>{one_pixel.front()});
    REQUIRE(pixels_of(faded->view()) ==
            std::vector<rf::Rgba8>{one_pixel.front()});
  }
}

TEST_CASE("tint multiplies only straight RGB with exact byte rounding",
          "[pixel-transform][tint][rounding]") {
  constexpr std::array pixels{rf::Rgba8{2, 128, 255, 73},
                              rf::Rgba8{200, 100, 50, 0}};
  auto source = make_image({2, 1}, pixels);

  const auto result = rf::tint(source.view(), {64, 128, 255});
  REQUIRE(result);
  REQUIRE(pixels_of(result->view()) ==
          std::vector<rf::Rgba8>{{1, 64, 255, 73}, {50, 50, 50, 0}});
  REQUIRE(pixels_of(source.view()) ==
          std::vector<rf::Rgba8>(pixels.begin(), pixels.end()));

  const auto black = rf::tint(source.view(), {0, 0, 0});
  REQUIRE(black);
  REQUIRE(pixels_of(black->view()) ==
          std::vector<rf::Rgba8>{{0, 0, 0, 73}, {0, 0, 0, 0}});
}

TEST_CASE("dim uses a quantized uniform multiplier and preserves alpha",
          "[pixel-transform][dim][rounding]") {
  constexpr std::array pixels{rf::Rgba8{1, 2, 127, 255},
                              rf::Rgba8{200, 100, 50, 0}};
  auto source = make_image({2, 1}, pixels);

  const auto result = rf::dim(source.view(), 0.5F);
  REQUIRE(result);
  REQUIRE(pixels_of(result->view()) ==
          std::vector<rf::Rgba8>{{1, 1, 64, 255}, {100, 50, 25, 0}});
}

TEST_CASE("opacity changes only alpha, including at the transparent endpoint",
          "[pixel-transform][opacity][rounding]") {
  constexpr std::array pixels{rf::Rgba8{1, 2, 127, 127},
                              rf::Rgba8{200, 100, 50, 0}};
  auto source = make_image({2, 1}, pixels);

  const auto half = rf::adjust_opacity(source.view(), 0.5F);
  REQUIRE(half);
  REQUIRE(pixels_of(half->view()) ==
          std::vector<rf::Rgba8>{{1, 2, 127, 64}, {200, 100, 50, 0}});

  const auto transparent = rf::adjust_opacity(source.view(), 0.0F);
  REQUIRE(transparent);
  REQUIRE(pixels_of(transparent->view()) ==
          std::vector<rf::Rgba8>{{1, 2, 127, 0}, {200, 100, 50, 0}});
}

TEST_CASE("RGB transform order is visible at byte-rounding boundaries",
          "[pixel-transform][ordering]") {
  constexpr std::array pixels{rf::Rgba8{2, 2, 2, 128}};
  auto source = make_image({1, 1}, pixels);

  const auto tint_then_dim_source = rf::tint(source.view(), {64, 64, 64});
  REQUIRE(tint_then_dim_source);
  const auto tint_then_dim = rf::dim(tint_then_dim_source->view(), 0.5F);
  REQUIRE(tint_then_dim);

  const auto dim_then_tint_source = rf::dim(source.view(), 0.5F);
  REQUIRE(dim_then_tint_source);
  const auto dim_then_tint =
      rf::tint(dim_then_tint_source->view(), {64, 64, 64});
  REQUIRE(dim_then_tint);

  REQUIRE(pixels_of(tint_then_dim->view()) ==
          std::vector<rf::Rgba8>{{1, 1, 1, 128}});
  REQUIRE(pixels_of(dim_then_tint->view()) ==
          std::vector<rf::Rgba8>{{0, 0, 0, 128}});

  const auto tint_then_opacity_source = rf::tint(source.view(), {64, 128, 255});
  REQUIRE(tint_then_opacity_source);
  const auto tint_then_opacity =
      rf::adjust_opacity(tint_then_opacity_source->view(), 0.5F);
  REQUIRE(tint_then_opacity);

  const auto opacity_then_tint_source = rf::adjust_opacity(source.view(), 0.5F);
  REQUIRE(opacity_then_tint_source);
  const auto opacity_then_tint =
      rf::tint(opacity_then_tint_source->view(), {64, 128, 255});
  REQUIRE(opacity_then_tint);
  REQUIRE(pixels_of(tint_then_opacity->view()) ==
          pixels_of(opacity_then_tint->view()));
}

TEST_CASE("identity transforms preserve every byte across multiple rows",
          "[pixel-transform][identity]") {
  constexpr std::array pixels{rf::Rgba8{0, 1, 2, 3}, rf::Rgba8{4, 5, 6, 7},
                              rf::Rgba8{250, 251, 252, 253},
                              rf::Rgba8{255, 255, 255, 255}};
  auto source = make_image({2, 2}, pixels);
  const std::vector<rf::Rgba8> expected(pixels.begin(), pixels.end());

  const auto tinted = rf::tint(source.view(), {255, 255, 255});
  const auto dimmed = rf::dim(source.view(), 1.0F);
  const auto opacity = rf::adjust_opacity(source.view(), 1.0F);
  REQUIRE(tinted);
  REQUIRE(dimmed);
  REQUIRE(opacity);
  REQUIRE(pixels_of(tinted->view()) == expected);
  REQUIRE(pixels_of(dimmed->view()) == expected);
  REQUIRE(pixels_of(opacity->view()) == expected);
  REQUIRE(pixels_of(source.view()) == expected);
}
