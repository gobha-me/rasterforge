#include <catch2/catch_test_macros.hpp>

#include <rasterforge/rasterforge.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace rf = rasterforge;

namespace {

struct Coverage {
  std::uint32_t left{};
  std::uint32_t top{};
  std::uint32_t right{};
  std::uint32_t bottom{};
  std::uint32_t pixels{};
};

auto make_opaque_image(rf::Extent extent) -> rf::Image {
  auto image = rf::Image::create(extent);
  REQUIRE(image);

  for (std::uint32_t y = 0; y < extent.height; ++y) {
    auto row = image->mutable_view().row(y);
    REQUIRE(row);
    for (std::uint32_t x = 0; x < extent.width; ++x) {
      (*row)[x] = {
          .r = static_cast<std::uint8_t>((x * 37U + y * 11U) & 0xFFU),
          .g = static_cast<std::uint8_t>((x * 13U + y * 41U) & 0xFFU),
          .b = static_cast<std::uint8_t>((x * 29U + y * 17U) & 0xFFU),
          .a = 255,
      };
    }
  }
  return std::move(*image);
}

auto opaque_coverage(rf::ImageView view) -> Coverage {
  Coverage coverage{
      .left = view.extent().width,
      .top = view.extent().height,
  };
  for (std::uint32_t y = 0; y < view.extent().height; ++y) {
    const auto row = view.row(y);
    REQUIRE(row);
    REQUIRE(row->size() == view.extent().width);
    for (std::uint32_t x = 0; x < view.extent().width; ++x) {
      const auto alpha = (*row)[x].a;
      REQUIRE((alpha == 0 || alpha == 255));
      if (alpha == 0) {
        continue;
      }
      coverage.left = std::min(coverage.left, x);
      coverage.top = std::min(coverage.top, y);
      coverage.right = std::max(coverage.right, x + 1U);
      coverage.bottom = std::max(coverage.bottom, y + 1U);
      ++coverage.pixels;
    }
  }
  REQUIRE(coverage.pixels > 0);
  return coverage;
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

} // namespace

TEST_CASE("fit rejects zero extents and non-finite focal coordinates",
          "[fit][property][failure]") {
  auto source = make_opaque_image({2, 2});

  for (const auto filter :
       {rf::ResizeFilter::nearest, rf::ResizeFilter::triangle}) {
    CAPTURE(filter);

    const auto empty_source =
        rf::fit({}, {1, 1}, rf::Fit::contain, {}, {}, {}, filter);
    REQUIRE_FALSE(empty_source);
    REQUIRE(empty_source.error().code == rf::ErrorCode::invalid_dimensions);

    const auto zero_width =
        rf::fit(source.view(), {0, 1}, rf::Fit::cover, {}, {}, {}, filter);
    REQUIRE_FALSE(zero_width);
    REQUIRE(zero_width.error().code == rf::ErrorCode::invalid_dimensions);

    const auto zero_height =
        rf::fit(source.view(), {1, 0}, rf::Fit::stretch, {}, {}, {}, filter);
    REQUIRE_FALSE(zero_height);
    REQUIRE(zero_height.error().code == rf::ErrorCode::invalid_dimensions);

    const auto nan_focus = rf::fit(
        source.view(), {1, 1}, rf::Fit::none,
        {std::numeric_limits<float>::quiet_NaN(), 0.5F}, {}, {}, filter);
    REQUIRE_FALSE(nan_focus);
    REQUIRE(nan_focus.error().code == rf::ErrorCode::invalid_argument);

    const auto infinite_focus =
        rf::fit(source.view(), {1, 1}, rf::Fit::none,
                {0.5F, std::numeric_limits<float>::infinity()}, {}, {}, filter);
    REQUIRE_FALSE(infinite_focus);
    REQUIRE(infinite_focus.error().code == rf::ErrorCode::invalid_argument);
  }
}

TEST_CASE("fit policies preserve public invariants across small extents",
          "[fit][property][invariant]") {
  constexpr rf::Rgba8 transparent_matte{91, 73, 55, 0};

  for (std::uint32_t source_width = 1; source_width <= 5; ++source_width) {
    for (std::uint32_t source_height = 1; source_height <= 5; ++source_height) {
      auto source = make_opaque_image({source_width, source_height});

      for (std::uint32_t destination_width = 1; destination_width <= 5;
           ++destination_width) {
        for (std::uint32_t destination_height = 1; destination_height <= 5;
             ++destination_height) {
          const rf::Extent destination{destination_width, destination_height};

          for (const auto policy : {rf::Fit::contain, rf::Fit::cover,
                                    rf::Fit::stretch, rf::Fit::none}) {
            for (const auto filter :
                 {rf::ResizeFilter::nearest, rf::ResizeFilter::triangle}) {
              CAPTURE(source_width, source_height, destination_width,
                      destination_height, policy, filter);
              const auto result = rf::fit(source.view(), destination, policy,
                                          {}, transparent_matte, {}, filter);
              REQUIRE(result);
              REQUIRE(result->extent() == destination);
              REQUIRE(result->stride_bytes() ==
                      static_cast<std::size_t>(destination_width) *
                          sizeof(rf::Rgba8));
              REQUIRE(result->size_bytes() ==
                      static_cast<std::size_t>(destination_width) *
                          destination_height * sizeof(rf::Rgba8));

              const auto coverage = opaque_coverage(result->view());
              const auto covered_width = coverage.right - coverage.left;
              const auto covered_height = coverage.bottom - coverage.top;
              REQUIRE(coverage.pixels == covered_width * covered_height);

              if (policy == rf::Fit::contain) {
                const auto source_is_wider =
                    static_cast<std::uint64_t>(source_width) *
                        destination_height >
                    static_cast<std::uint64_t>(destination_width) *
                        source_height;
                if (source_is_wider) {
                  REQUIRE(covered_width == destination_width);
                } else {
                  REQUIRE(covered_height == destination_height);
                }
              } else if (policy == rf::Fit::none) {
                REQUIRE(covered_width ==
                        std::min(source_width, destination_width));
                REQUIRE(covered_height ==
                        std::min(source_height, destination_height));
              } else {
                REQUIRE(coverage.left == 0);
                REQUIRE(coverage.top == 0);
                REQUIRE(coverage.right == destination_width);
                REQUIRE(coverage.bottom == destination_height);
              }
            }
          }
        }
      }
    }
  }
}

TEST_CASE("focal clamping is observable through every fit filter",
          "[fit][property][focus]") {
  auto source = make_opaque_image({5, 2});
  constexpr rf::Rgba8 matte{9, 8, 7, 0};

  for (const auto policy :
       {rf::Fit::contain, rf::Fit::cover, rf::Fit::stretch, rf::Fit::none}) {
    for (const auto filter :
         {rf::ResizeFilter::nearest, rf::ResizeFilter::triangle}) {
      CAPTURE(policy, filter);
      const auto below = rf::fit(source.view(), {3, 4}, policy,
                                 {-10.0F, -10.0F}, matte, {}, filter);
      const auto low = rf::fit(source.view(), {3, 4}, policy, {0.0F, 0.0F},
                               matte, {}, filter);
      const auto above = rf::fit(source.view(), {3, 4}, policy, {10.0F, 10.0F},
                                 matte, {}, filter);
      const auto high = rf::fit(source.view(), {3, 4}, policy, {1.0F, 1.0F},
                                matte, {}, filter);
      REQUIRE(below);
      REQUIRE(low);
      REQUIRE(above);
      REQUIRE(high);
      REQUIRE(pixels_of(below->view()) == pixels_of(low->view()));
      REQUIRE(pixels_of(above->view()) == pixels_of(high->view()));
    }
  }
}
