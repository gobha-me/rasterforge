#include "../../src/lib/fit_geometry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <rasterforge/rasterforge.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace rf = rasterforge;
namespace detail = rasterforge::detail;

namespace {

[[nodiscard]] constexpr auto rect_is_within(detail::Rect rect,
                                            rf::Extent extent) -> bool {
  return static_cast<std::uint64_t>(rect.x) + rect.width <= extent.width &&
         static_cast<std::uint64_t>(rect.y) + rect.height <= extent.height;
}

} // namespace

TEST_CASE("fit geometry rejects invalid inputs before doing arithmetic",
          "[fit][geometry][failure]") {
  SECTION("zero source width") {
    const auto plan = detail::plan_fit({0, 1}, {1, 1}, rf::Fit::contain);
    REQUIRE_FALSE(plan);
    REQUIRE(plan.error().code == rf::ErrorCode::invalid_dimensions);
  }

  SECTION("zero source height") {
    const auto plan = detail::plan_fit({1, 0}, {1, 1}, rf::Fit::cover);
    REQUIRE_FALSE(plan);
    REQUIRE(plan.error().code == rf::ErrorCode::invalid_dimensions);
  }

  SECTION("zero destination width") {
    const auto plan = detail::plan_fit({1, 1}, {0, 1}, rf::Fit::stretch);
    REQUIRE_FALSE(plan);
    REQUIRE(plan.error().code == rf::ErrorCode::invalid_dimensions);
  }

  SECTION("zero destination height") {
    const auto plan = detail::plan_fit({1, 1}, {1, 0}, rf::Fit::none);
    REQUIRE_FALSE(plan);
    REQUIRE(plan.error().code == rf::ErrorCode::invalid_dimensions);
  }

  SECTION("unrecognized fit policy") {
    const auto plan = detail::plan_fit({1, 1}, {1, 1},
                                       static_cast<rf::Fit>(std::uint8_t{255}));
    REQUIRE_FALSE(plan);
    REQUIRE(plan.error().code == rf::ErrorCode::invalid_argument);
  }

  const std::array non_finite{
      std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::infinity(),
      -std::numeric_limits<float>::infinity(),
  };
  for (const auto coordinate : non_finite) {
    CAPTURE(coordinate);
    const auto invalid_x =
        detail::plan_fit({2, 1}, {1, 1}, rf::Fit::cover, {coordinate, 0.5F});
    REQUIRE_FALSE(invalid_x);
    REQUIRE(invalid_x.error().code == rf::ErrorCode::invalid_argument);

    const auto invalid_y =
        detail::plan_fit({1, 2}, {1, 1}, rf::Fit::cover, {0.5F, coordinate});
    REQUIRE_FALSE(invalid_y);
    REQUIRE(invalid_y.error().code == rf::ErrorCode::invalid_argument);
  }
}

TEST_CASE("contain preserves the full source and positions matte slack",
          "[fit][geometry][contain]") {
  SECTION("landscape source") {
    const auto centered = detail::plan_fit({4, 2}, {5, 5}, rf::Fit::contain);
    REQUIRE(centered);
    REQUIRE(centered->source == detail::Rect{0, 0, 4, 2});
    REQUIRE(centered->destination == detail::Rect{0, 1, 5, 3});

    const auto bottom =
        detail::plan_fit({4, 2}, {5, 5}, rf::Fit::contain, {0.5F, 1.0F});
    REQUIRE(bottom);
    REQUIRE(bottom->destination == detail::Rect{0, 2, 5, 3});
  }

  SECTION("portrait source") {
    const auto centered = detail::plan_fit({2, 4}, {5, 5}, rf::Fit::contain);
    REQUIRE(centered);
    REQUIRE(centered->source == detail::Rect{0, 0, 2, 4});
    REQUIRE(centered->destination == detail::Rect{1, 0, 3, 5});
  }

  SECTION("equal aspect ratio") {
    const auto plan = detail::plan_fit({8, 4}, {6, 3}, rf::Fit::contain);
    REQUIRE(plan);
    REQUIRE(plan->source == detail::Rect{0, 0, 8, 4});
    REQUIRE(plan->destination == detail::Rect{0, 0, 6, 3});
  }
}

TEST_CASE("cover fills the destination and positions the source crop",
          "[fit][geometry][cover]") {
  SECTION("landscape source") {
    const auto centered = detail::plan_fit({4, 2}, {5, 5}, rf::Fit::cover);
    REQUIRE(centered);
    REQUIRE(centered->source == detail::Rect{1, 0, 2, 2});
    REQUIRE(centered->destination == detail::Rect{0, 0, 5, 5});

    const auto right =
        detail::plan_fit({4, 2}, {5, 5}, rf::Fit::cover, {1.0F, 0.5F});
    REQUIRE(right);
    REQUIRE(right->source == detail::Rect{2, 0, 2, 2});
  }

  SECTION("portrait source") {
    const auto centered = detail::plan_fit({2, 4}, {5, 5}, rf::Fit::cover);
    REQUIRE(centered);
    REQUIRE(centered->source == detail::Rect{0, 1, 2, 2});
    REQUIRE(centered->destination == detail::Rect{0, 0, 5, 5});
  }

  SECTION("equal aspect ratio") {
    const auto plan = detail::plan_fit({8, 4}, {6, 3}, rf::Fit::cover);
    REQUIRE(plan);
    REQUIRE(plan->source == detail::Rect{0, 0, 8, 4});
    REQUIRE(plan->destination == detail::Rect{0, 0, 6, 3});
  }
}

TEST_CASE("stretch and none have explicit scale and placement behavior",
          "[fit][geometry]") {
  SECTION("stretch uses both complete rectangles") {
    const auto plan = detail::plan_fit({7, 3}, {2, 9}, rf::Fit::stretch);
    REQUIRE(plan);
    REQUIRE(plan->source == detail::Rect{0, 0, 7, 3});
    REQUIRE(plan->destination == detail::Rect{0, 0, 2, 9});
  }

  SECTION("none crops one axis and letterboxes the other") {
    const auto centered = detail::plan_fit({5, 2}, {3, 4}, rf::Fit::none);
    REQUIRE(centered);
    REQUIRE(centered->source == detail::Rect{1, 0, 3, 2});
    REQUIRE(centered->destination == detail::Rect{0, 1, 3, 2});

    const auto endpoints =
        detail::plan_fit({5, 2}, {3, 4}, rf::Fit::none, {1.0F, 0.0F});
    REQUIRE(endpoints);
    REQUIRE(endpoints->source == detail::Rect{2, 0, 3, 2});
    REQUIRE(endpoints->destination == detail::Rect{0, 0, 3, 2});
  }
}

TEST_CASE("focal clamping and half-up rounding are deterministic",
          "[fit][geometry][rounding]") {
  const auto below =
      detail::plan_fit({4, 1}, {1, 1}, rf::Fit::cover, {-10.0F, 0.5F});
  const auto left =
      detail::plan_fit({4, 1}, {1, 1}, rf::Fit::cover, {0.0F, 0.5F});
  REQUIRE(below);
  REQUIRE(left);
  REQUIRE(*below == *left);
  REQUIRE(left->source.x == 0);

  const auto above =
      detail::plan_fit({4, 1}, {1, 1}, rf::Fit::cover, {10.0F, 0.5F});
  const auto right =
      detail::plan_fit({4, 1}, {1, 1}, rf::Fit::cover, {1.0F, 0.5F});
  REQUIRE(above);
  REQUIRE(right);
  REQUIRE(*above == *right);
  REQUIRE(right->source.x == 3);

  const auto centered = detail::plan_fit({4, 1}, {1, 1}, rf::Fit::cover);
  REQUIRE(centered);
  REQUIRE(centered->source.x == 2); // 3 * 0.5 ties toward the right.

  const auto ratio_tie = detail::plan_fit({2, 1}, {3, 3}, rf::Fit::contain);
  REQUIRE(ratio_tie);
  REQUIRE(ratio_tie->destination == detail::Rect{0, 1, 3, 2});
}

TEST_CASE("fit geometry remains bounded at integer extremes",
          "[fit][geometry][overflow]") {
  constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();

  for (const auto fit :
       {rf::Fit::contain, rf::Fit::cover, rf::Fit::stretch, rf::Fit::none}) {
    CAPTURE(fit);
    const auto plan = detail::plan_fit({maximum, 1}, {1, maximum}, fit);
    REQUIRE(plan);
    REQUIRE(plan->source.width > 0);
    REQUIRE(plan->source.height > 0);
    REQUIRE(plan->destination.width > 0);
    REQUIRE(plan->destination.height > 0);
    REQUIRE(rect_is_within(plan->source, {maximum, 1}));
    REQUIRE(rect_is_within(plan->destination, {1, maximum}));
  }
}

TEST_CASE("fit-plan invariants hold across asymmetric small extents",
          "[fit][geometry][invariant]") {
  STATIC_REQUIRE(noexcept(detail::plan_fit({}, {}, rf::Fit::contain)));
  STATIC_REQUIRE(std::is_trivially_copyable_v<detail::FitPlan>);

  for (std::uint32_t source_width = 1; source_width <= 7; ++source_width) {
    for (std::uint32_t source_height = 1; source_height <= 7; ++source_height) {
      for (std::uint32_t destination_width = 1; destination_width <= 7;
           ++destination_width) {
        for (std::uint32_t destination_height = 1; destination_height <= 7;
             ++destination_height) {
          const rf::Extent source_extent{source_width, source_height};
          const rf::Extent destination_extent{destination_width,
                                              destination_height};

          for (const auto fit : {rf::Fit::contain, rf::Fit::cover,
                                 rf::Fit::stretch, rf::Fit::none}) {
            CAPTURE(source_extent, destination_extent, fit);
            const auto plan =
                detail::plan_fit(source_extent, destination_extent, fit);
            REQUIRE(plan);
            REQUIRE(plan->source_extent == source_extent);
            REQUIRE(plan->destination_extent == destination_extent);
            REQUIRE(plan->source.width > 0);
            REQUIRE(plan->source.height > 0);
            REQUIRE(plan->destination.width > 0);
            REQUIRE(plan->destination.height > 0);
            REQUIRE(rect_is_within(plan->source, source_extent));
            REQUIRE(rect_is_within(plan->destination, destination_extent));

            if (fit == rf::Fit::contain) {
              REQUIRE(plan->source ==
                      detail::Rect{0, 0, source_width, source_height});
            }
            if (fit == rf::Fit::cover) {
              REQUIRE(plan->destination == detail::Rect{0, 0, destination_width,
                                                        destination_height});
            }
          }
        }
      }
    }
  }
}
