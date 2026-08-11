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

constexpr rf::Rgba8 a{1, 2, 3, 4};
constexpr rf::Rgba8 b{11, 12, 13, 14};
constexpr rf::Rgba8 c{21, 22, 23, 24};
constexpr rf::Rgba8 d{31, 32, 33, 34};
constexpr rf::Rgba8 e{41, 42, 43, 44};
constexpr rf::Rgba8 f{51, 52, 53, 54};

} // namespace

TEST_CASE("nearest fit rejects invalid geometry and options before sampling",
          "[fit][nearest][failure]") {
  const std::array source_pixels{a};
  auto source = make_image({1, 1}, source_pixels);

  SECTION("empty source view") {
    const auto result = rf::fit({}, {1, 1}, rf::Fit::contain);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::invalid_dimensions);
  }

  SECTION("zero destination extent") {
    const auto result = rf::fit(source.view(), {0, 1}, rf::Fit::cover);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::invalid_dimensions);
  }

  SECTION("unrecognized fit policy") {
    const auto result =
        rf::fit(source.view(), {1, 1},
                static_cast<rf::Fit>(std::uint8_t{255}));
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::invalid_argument);
  }

  SECTION("non-finite focal point") {
    const auto result =
        rf::fit(source.view(), {1, 1}, rf::Fit::cover,
                {std::numeric_limits<float>::quiet_NaN(), 0.5F});
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::invalid_argument);
  }
}

TEST_CASE("nearest fit enforces destination allocation limits",
          "[fit][nearest][limits]") {
  const std::array source_pixels{a};
  auto source = make_image({1, 1}, source_pixels);

  SECTION("dimension limit") {
    rf::Limits limits{};
    limits.max_dimension = 1;
    const auto result = rf::fit(source.view(), {2, 1}, rf::Fit::stretch, {},
                                {0, 0, 0, 0}, limits);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("pixel limit") {
    rf::Limits limits{};
    limits.max_pixels = 1;
    const auto result = rf::fit(source.view(), {2, 1}, rf::Fit::stretch, {},
                                {0, 0, 0, 0}, limits);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("output-byte limit") {
    rf::Limits limits{};
    limits.max_output_bytes = 7;
    const auto result = rf::fit(source.view(), {2, 1}, rf::Fit::stretch, {},
                                {0, 0, 0, 0}, limits);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("exact output limits accept while unused budgets may be zero") {
    rf::Limits limits{};
    limits.max_input_bytes = 0;
    limits.max_dimension = 2;
    limits.max_pixels = 2;
    limits.max_output_bytes = 2 * sizeof(rf::Rgba8);
    limits.max_temporary_bytes = 0;
    const auto result = rf::fit(source.view(), {2, 1}, rf::Fit::stretch, {},
                                {0, 0, 0, 0}, limits);
    REQUIRE(result);
    REQUIRE(pixels_of(result->view()) == std::vector<rf::Rgba8>{a, a});
  }
}

TEST_CASE("nearest identity preserves every straight-alpha RGBA byte",
          "[fit][nearest][identity]") {
  constexpr std::array source_pixels{
      rf::Rgba8{255, 0, 127, 0}, b, c, d, e, rf::Rgba8{9, 8, 7, 255}};
  auto source = make_image({3, 2}, source_pixels);

  for (const auto policy :
       {rf::Fit::contain, rf::Fit::cover, rf::Fit::stretch, rf::Fit::none}) {
    CAPTURE(policy);
    const auto result = rf::fit(source.view(), {3, 2}, policy);
    REQUIRE(result);
    REQUIRE(result->extent() == rf::Extent{3, 2});
    REQUIRE(pixels_of(result->view()) ==
            std::vector<rf::Rgba8>{source_pixels.begin(), source_pixels.end()});
  }
}

TEST_CASE("nearest sampling uses pixel centers and breaks ties right or down",
          "[fit][nearest][sampling]") {
  SECTION("horizontal downsample") {
    const std::array source_pixels{a, b, c, d};
    auto source = make_image({4, 1}, source_pixels);
    const auto result = rf::fit(source.view(), {2, 1}, rf::Fit::stretch);
    REQUIRE(result);
    REQUIRE(pixels_of(result->view()) == std::vector<rf::Rgba8>{b, d});
  }

  SECTION("vertical downsample") {
    const std::array source_pixels{a, b, c, d};
    auto source = make_image({1, 4}, source_pixels);
    const auto result = rf::fit(source.view(), {1, 2}, rf::Fit::stretch);
    REQUIRE(result);
    REQUIRE(pixels_of(result->view()) == std::vector<rf::Rgba8>{b, d});
  }

  SECTION("one-pixel axes and upsample") {
    const std::array source_pixels{a, b};
    auto horizontal = make_image({2, 1}, source_pixels);
    const auto wide = rf::fit(horizontal.view(), {4, 1}, rf::Fit::stretch);
    REQUIRE(wide);
    REQUIRE(pixels_of(wide->view()) ==
            std::vector<rf::Rgba8>{a, a, b, b});

    auto vertical = make_image({1, 2}, source_pixels);
    const auto tall = rf::fit(vertical.view(), {1, 4}, rf::Fit::stretch);
    REQUIRE(tall);
    REQUIRE(pixels_of(tall->view()) ==
            std::vector<rf::Rgba8>{a, a, b, b});
  }

  SECTION("asymmetric two-dimensional grid") {
    const std::array source_pixels{a, b, c, d, e, f};
    auto source = make_image({3, 2}, source_pixels);
    const auto result = rf::fit(source.view(), {2, 3}, rf::Fit::stretch);
    REQUIRE(result);
    REQUIRE(pixels_of(result->view()) ==
            std::vector<rf::Rgba8>{a, c, d, f, d, f});
  }
}

TEST_CASE("contain fills uncovered pixels with the exact caller matte",
          "[fit][nearest][contain]") {
  const std::array source_pixels{a, b};
  auto source = make_image({2, 1}, source_pixels);

  SECTION("explicit colored translucent matte") {
    constexpr rf::Rgba8 matte{90, 80, 70, 60};
    const auto result =
        rf::fit(source.view(), {4, 4}, rf::Fit::contain, {}, matte);
    REQUIRE(result);
    REQUIRE(result->extent() == rf::Extent{4, 4});
    REQUIRE(pixels_of(result->view()) ==
            std::vector<rf::Rgba8>{matte, matte, matte, matte, a, a, b, b,
                                   a,     a,     b,     b,     matte, matte,
                                   matte, matte});
  }

  SECTION("default matte is transparent black") {
    const auto result = rf::fit(source.view(), {2, 2}, rf::Fit::contain);
    REQUIRE(result);
    REQUIRE(pixels_of(result->view()) ==
            std::vector<rf::Rgba8>{{0, 0, 0, 0}, {0, 0, 0, 0}, a, b});
  }
}

TEST_CASE("cover crops at both focal endpoints without exposing matte",
          "[fit][nearest][cover]") {
  const std::array source_pixels{a, b, c, d};
  auto source = make_image({4, 1}, source_pixels);
  constexpr rf::Rgba8 matte{200, 201, 202, 203};

  const auto left =
      rf::fit(source.view(), {2, 1}, rf::Fit::cover, {0.0F, 0.5F}, matte);
  const auto right =
      rf::fit(source.view(), {2, 1}, rf::Fit::cover, {1.0F, 0.5F}, matte);
  REQUIRE(left);
  REQUIRE(right);
  REQUIRE(pixels_of(left->view()) == std::vector<rf::Rgba8>{a, b});
  REQUIRE(pixels_of(right->view()) == std::vector<rf::Rgba8>{c, d});
}

TEST_CASE("no-scale crops and letterboxes without resampling",
          "[fit][nearest][none]") {
  const std::array source_pixels{a, b, c, d, e, f};
  auto source = make_image({3, 2}, source_pixels);
  constexpr rf::Rgba8 matte{100, 101, 102, 103};

  const auto result =
      rf::fit(source.view(), {2, 4}, rf::Fit::none, {1.0F, 0.0F}, matte);
  REQUIRE(result);
  REQUIRE(pixels_of(result->view()) ==
          std::vector<rf::Rgba8>{b, c, e, f, matte, matte, matte, matte});
}
