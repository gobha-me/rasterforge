#include "fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <rasterforge/rasterforge.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <future>
#include <span>
#include <utility>
#include <vector>

namespace rf = rasterforge;
namespace fixtures = png_fixtures;

namespace {

template <std::size_t Size>
[[nodiscard]] auto bytes(const std::array<std::uint8_t, Size> &fixture) {
  return std::as_bytes(std::span{fixture});
}

void require_pixels(const rf::DecodedImage &decoded, rf::Extent extent,
                    std::span<const rf::Rgba8> expected) {
  REQUIRE(decoded.encoded_extent() == extent);
  REQUIRE(decoded.output_extent() == extent);
  REQUIRE(decoded.format() == rf::ImageFormat::png);
  REQUIRE(decoded.orientation_status() == rf::OrientationStatus::not_present);
  REQUIRE(expected.size() ==
          static_cast<std::size_t>(extent.width) * extent.height);

  for (std::uint32_t y = 0; y < extent.height; ++y) {
    const auto row = decoded.view().row(y);
    REQUIRE(row);
    for (std::uint32_t x = 0; x < extent.width; ++x) {
      CAPTURE(x, y);
      REQUIRE((*row)[x] ==
              expected[(static_cast<std::size_t>(y) * extent.width) + x]);
    }
  }
}

} // namespace

TEST_CASE("PNG RGB and RGBA decode to exact straight-alpha bytes",
          "[decode][png][pixels]") {
  SECTION("opaque RGB gains an opaque alpha channel") {
    const auto decoded = rf::decode(bytes(fixtures::rgb_png));
    REQUIRE(decoded);
    REQUIRE_FALSE(decoded->has_alpha());
    constexpr std::array expected{
        rf::Rgba8{10, 20, 30, 255},
        rf::Rgba8{200, 150, 100, 255},
    };
    require_pixels(*decoded, {2, 1}, expected);
  }

  SECTION("transparent colored RGBA is preserved without premultiplication") {
    const auto decoded = rf::decode(bytes(fixtures::rgba_png));
    REQUIRE(decoded);
    REQUIRE(decoded->has_alpha());
    constexpr std::array expected{
        rf::Rgba8{1, 2, 3, 4},
        rf::Rgba8{90, 80, 70, 0},
    };
    require_pixels(*decoded, {2, 1}, expected);
  }
}

TEST_CASE(
    "PNG grayscale, palette, and 16-bit inputs normalize deterministically",
    "[decode][png][pixels]") {
  SECTION("one-bit grayscale expands before RGB conversion") {
    const auto decoded = rf::decode(bytes(fixtures::gray1_png));
    REQUIRE(decoded);
    REQUIRE_FALSE(decoded->has_alpha());
    constexpr std::array expected{
        rf::Rgba8{0, 0, 0, 255},
        rf::Rgba8{255, 255, 255, 255},
    };
    require_pixels(*decoded, {2, 1}, expected);
  }

  SECTION("grayscale alpha remains straight alpha") {
    const auto decoded = rf::decode(bytes(fixtures::gray_alpha_png));
    REQUIRE(decoded);
    REQUIRE(decoded->has_alpha());
    constexpr std::array expected{
        rf::Rgba8{50, 50, 50, 128},
        rf::Rgba8{200, 200, 200, 0},
    };
    require_pixels(*decoded, {2, 1}, expected);
  }

  SECTION("palette transparency contributes alpha semantics") {
    const auto decoded = rf::decode(bytes(fixtures::palette_png));
    REQUIRE(decoded);
    REQUIRE(decoded->has_alpha());
    constexpr std::array expected{
        rf::Rgba8{255, 0, 0, 255},
        rf::Rgba8{0, 255, 0, 0},
    };
    require_pixels(*decoded, {2, 1}, expected);
  }

  SECTION("sixteen-bit samples use the deterministic high byte") {
    const auto decoded = rf::decode(bytes(fixtures::gray16_png));
    REQUIRE(decoded);
    REQUIRE_FALSE(decoded->has_alpha());
    constexpr std::array expected{rf::Rgba8{0x12, 0x12, 0x12, 255}};
    require_pixels(*decoded, {1, 1}, expected);
  }
}

TEST_CASE("Adam7 decoding fills every destination pixel",
          "[decode][png][interlace]") {
  const auto decoded = rf::decode(bytes(fixtures::interlaced_png));
  REQUIRE(decoded);
  constexpr std::array expected{
      rf::Rgba8{1, 2, 3, 255},    rf::Rgba8{41, 2, 4, 255},
      rf::Rgba8{81, 2, 5, 255},   rf::Rgba8{1, 52, 4, 255},
      rf::Rgba8{41, 52, 5, 255},  rf::Rgba8{81, 52, 6, 255},
      rf::Rgba8{1, 102, 5, 255},  rf::Rgba8{41, 102, 6, 255},
      rf::Rgba8{81, 102, 7, 255},
  };
  require_pixels(*decoded, {3, 3}, expected);
}

TEST_CASE("PNG failures are stable and never report partial output",
          "[decode][png][failure]") {
  SECTION("representative truncation boundaries") {
    constexpr std::array sizes{std::size_t{8}, std::size_t{20}, std::size_t{40},
                               fixtures::rgb_png.size() - 1};
    for (const auto size : sizes) {
      CAPTURE(size);
      const auto decoded = rf::decode(bytes(fixtures::rgb_png).first(size));
      REQUIRE_FALSE(decoded);
      REQUIRE(decoded.error().code == rf::ErrorCode::truncated_data);
    }
  }

  SECTION("a corrupt critical payload is malformed") {
    auto corrupt = fixtures::rgb_png;
    corrupt[46] ^= 0x80;
    const auto decoded = rf::decode(bytes(corrupt));
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().code == rf::ErrorCode::malformed_data);
  }

  SECTION("an unknown critical chunk is unsupported") {
    const auto decoded = rf::decode(bytes(fixtures::unknown_critical_png));
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().code == rf::ErrorCode::unsupported_feature);
  }
}

TEST_CASE("PNG output respects caller allocation limits before pixel decode",
          "[decode][png][limits]") {
  SECTION("dimension limit") {
    rf::DecodeOptions options{};
    options.limits.max_dimension = 1;
    const auto decoded = rf::decode(bytes(fixtures::rgb_png), options);
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("pixel limit") {
    rf::DecodeOptions options{};
    options.limits.max_pixels = 1;
    auto corrupt = fixtures::rgb_png;
    corrupt[46] ^= 0x80;
    const auto decoded = rf::decode(bytes(corrupt), options);
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("output-byte limit") {
    rf::DecodeOptions options{};
    options.limits.max_output_bytes = sizeof(rf::Rgba8);
    const auto decoded = rf::decode(bytes(fixtures::rgb_png), options);
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().code == rf::ErrorCode::resource_limit);
  }
}

TEST_CASE("independent PNG decoder contexts are safe to run concurrently",
          "[decode][png][concurrency]") {
  constexpr std::size_t worker_count{8};
  constexpr std::size_t iterations{32};
  std::vector<std::future<bool>> workers;
  workers.reserve(worker_count);

  for (std::size_t worker = 0; worker < worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [] {
      for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        auto decoded = rf::decode(bytes(fixtures::rgba_png));
        if (!decoded || !decoded->has_alpha() ||
            decoded->output_extent() != rf::Extent{2, 1}) {
          return false;
        }
        const auto row = decoded->view().row(0);
        if (!row || (*row)[1] != rf::Rgba8{90, 80, 70, 0}) {
          return false;
        }
      }
      return true;
    }));
  }

  for (auto &worker : workers) {
    REQUIRE(worker.get());
  }
}
