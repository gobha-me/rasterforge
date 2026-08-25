#include "fixtures.hpp"

#include "../../src/lib/webp_decoder.hpp"

#include <rasterforge/rasterforge.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <future>
#include <span>
#include <vector>

namespace rf = rasterforge;
namespace detail = rasterforge::detail;
namespace fixtures = webp_fixtures;

namespace {

[[nodiscard]] auto bytes(const std::vector<std::uint8_t> &encoded)
    -> std::span<const std::byte> {
  return std::as_bytes(std::span{encoded});
}

void append_u32(std::vector<std::uint8_t> &output, std::uint32_t value) {
  output.push_back(static_cast<std::uint8_t>(value));
  output.push_back(static_cast<std::uint8_t>(value >> 8U));
  output.push_back(static_cast<std::uint8_t>(value >> 16U));
  output.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void write_u32(std::vector<std::uint8_t> &output, std::size_t offset,
               std::uint32_t value) {
  REQUIRE(offset + 4U <= output.size());
  output[offset] = static_cast<std::uint8_t>(value);
  output[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
  output[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
  output[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

template <std::size_t Size>
void append(std::vector<std::uint8_t> &output,
            const std::array<std::uint8_t, Size> &value) {
  output.insert(output.end(), value.begin(), value.end());
}

[[nodiscard]] auto make_tiff(std::uint16_t orientation)
    -> std::vector<std::uint8_t> {
  return {
      'I',
      'I',
      42,
      0,
      8,
      0,
      0,
      0,
      1,
      0,
      0x12,
      0x01,
      3,
      0,
      1,
      0,
      0,
      0,
      static_cast<std::uint8_t>(orientation),
      static_cast<std::uint8_t>(orientation >> 8U),
      0,
      0,
      0,
      0,
      0,
      0,
  };
}

[[nodiscard]] auto with_exif(const std::vector<std::uint8_t> &simple,
                             const std::vector<std::uint8_t> &exif)
    -> std::vector<std::uint8_t> {
  REQUIRE(simple.size() >= 12U);
  std::vector<std::uint8_t> result{
      'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P',
  };
  append(result, std::array<std::uint8_t, 4>{'V', 'P', '8', 'X'});
  append_u32(result, 10U);
  append(result, std::array<std::uint8_t, 10>{0x18, 0, 0, 0, 1, 0, 0, 0, 0, 0});
  result.insert(result.end(), simple.begin() + 12, simple.end());
  append(result, std::array<std::uint8_t, 4>{'E', 'X', 'I', 'F'});
  append_u32(result, static_cast<std::uint32_t>(exif.size()));
  result.insert(result.end(), exif.begin(), exif.end());
  if ((exif.size() & 1U) != 0U) {
    result.push_back(0U);
  }
  write_u32(result, 4U, static_cast<std::uint32_t>(result.size() - 8U));
  return result;
}

[[nodiscard]] auto decode(const std::vector<std::uint8_t> &encoded,
                          const rf::DecodeOptions &options = {}) {
  return rf::decode(bytes(encoded), options);
}

} // namespace

TEST_CASE("WebP signature classification uses a bounded RIFF prefix",
          "[decode][webp][signature]") {
  const std::array signature{
      std::uint8_t{'R'}, std::uint8_t{'I'}, std::uint8_t{'F'},
      std::uint8_t{'F'}, std::uint8_t{0},   std::uint8_t{0},
      std::uint8_t{0},   std::uint8_t{0},   std::uint8_t{'W'},
      std::uint8_t{'E'}, std::uint8_t{'B'}, std::uint8_t{'P'},
  };
  for (std::size_t size = 1; size < signature.size(); ++size) {
    const std::vector<std::uint8_t> prefix(signature.begin(),
                                           signature.begin() + size);
    const auto result = decode(prefix);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::truncated_data);
  }

  auto mismatch = signature;
  mismatch[11] = 'X';
  const auto result = decode({mismatch.begin(), mismatch.end()});
  REQUIRE_FALSE(result);
  REQUIRE(result.error().code == rf::ErrorCode::unsupported_format);
  STATIC_REQUIRE(rf::decode_signature_prefix_bytes == 12U);
}

TEST_CASE("lossless WebP preserves straight alpha and transparent RGB",
          "[decode][webp][lossless][alpha]") {
  const auto result = decode(fixtures::rgba_lossless);
  REQUIRE(result);
  REQUIRE(result->format() == rf::ImageFormat::webp);
  REQUIRE(result->encoded_extent() == rf::Extent{2, 1});
  REQUIRE(result->output_extent() == rf::Extent{2, 1});
  REQUIRE(result->has_alpha());
  REQUIRE(result->orientation_status() == rf::OrientationStatus::not_present);
  const auto row = result->view().row(0);
  REQUIRE(row);
  REQUIRE((*row)[0] == rf::Rgba8{10, 20, 30, 255});
  REQUIRE((*row)[1] == rf::Rgba8{200, 100, 50, 0});
}

TEST_CASE("lossy WebP returns opaque RGBA within its codec contract",
          "[decode][webp][lossy]") {
  const auto result = decode(fixtures::rgb_lossy);
  REQUIRE(result);
  REQUIRE_FALSE(result->has_alpha());
  const auto row = result->view().row(0);
  REQUIRE(row);
  for (const auto pixel : *row) {
    REQUIRE(std::abs(static_cast<int>(pixel.r) - 40) <= 5);
    REQUIRE(std::abs(static_cast<int>(pixel.g) - 90) <= 5);
    REQUIRE(std::abs(static_cast<int>(pixel.b) - 160) <= 5);
    REQUIRE(pixel.a == 255);
  }
}

TEST_CASE("WebP failures distinguish truncation and malformed structure",
          "[decode][webp][failure]") {
  SECTION("declared container is truncated") {
    auto truncated = fixtures::rgba_lossless;
    truncated.pop_back();
    const auto result = decode(truncated);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::truncated_data);
  }

  SECTION("chunk exceeds the declared container") {
    auto malformed = fixtures::rgba_lossless;
    write_u32(malformed, 16U, 0x7fffffffU);
    const auto result = decode(malformed);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::malformed_data);
  }

  SECTION("corrupt compressed payload never reports partial output") {
    auto corrupt = fixtures::rgba_lossless;
    std::fill(corrupt.begin() + 20, corrupt.end(), 0xffU);
    const auto result = decode(corrupt);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::malformed_data);
  }
}

TEST_CASE("animated WebP is rejected instead of returning its first frame",
          "[decode][webp][animation]") {
  const auto result = decode(fixtures::animated);
  REQUIRE_FALSE(result);
  REQUIRE(result.error().code == rf::ErrorCode::unsupported_feature);
}

TEST_CASE("WebP dimensions, output, and temporary work are bounded",
          "[decode][webp][limits]") {
  SECTION("input bytes") {
    rf::DecodeOptions options{};
    options.limits.max_input_bytes = fixtures::rgba_lossless.size() - 1U;
    const auto result = decode(fixtures::rgba_lossless, options);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::input_too_large);
  }
  SECTION("dimension") {
    rf::DecodeOptions options{};
    options.limits.max_dimension = 1U;
    const auto result = decode(fixtures::rgba_lossless, options);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }
  SECTION("pixels") {
    rf::DecodeOptions options{};
    options.limits.max_pixels = 1U;
    const auto result = decode(fixtures::rgba_lossless, options);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }
  SECTION("output bytes") {
    rf::DecodeOptions options{};
    options.limits.max_output_bytes = 7U;
    const auto result = decode(fixtures::rgba_lossless, options);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }
  SECTION("temporary exact and one-byte-short") {
    const auto required =
        detail::checked_webp_work_bytes({2, 1}, fixtures::rgba_lossless.size());
    REQUIRE(required);
    rf::DecodeOptions options{};
    options.limits.max_temporary_bytes = *required;
    REQUIRE(decode(fixtures::rgba_lossless, options));
    options.limits.max_temporary_bytes = *required - 1U;
    const auto result = decode(fixtures::rgba_lossless, options);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }
}

TEST_CASE("WebP EXIF uses codec-neutral orientation behavior",
          "[decode][webp][orientation][metadata]") {
  const auto encoded = with_exif(fixtures::rgba_lossless, make_tiff(6U));

  const auto applied = decode(encoded);
  REQUIRE(applied);
  REQUIRE(applied->encoded_extent() == rf::Extent{2, 1});
  REQUIRE(applied->output_extent() == rf::Extent{1, 2});
  REQUIRE(applied->source_orientation() ==
          rf::Orientation::rotate_90_clockwise);
  REQUIRE(applied->orientation_status() == rf::OrientationStatus::applied);

  rf::DecodeOptions options{};
  options.orientation = rf::OrientationPolicy::ignore;
  const auto ignored = decode(encoded, options);
  REQUIRE(ignored);
  REQUIRE(ignored->output_extent() == rf::Extent{2, 1});
  REQUIRE(ignored->orientation_status() == rf::OrientationStatus::ignored);

  const auto invalid = decode(with_exif(fixtures::rgba_lossless, {1, 2, 3}));
  REQUIRE(invalid);
  REQUIRE(invalid->orientation_status() ==
          rf::OrientationStatus::invalid_ignored);

  auto identified_tiff = make_tiff(6U);
  identified_tiff.insert(identified_tiff.begin(), {'E', 'x', 'i', 'f', 0, 0});
  const auto identified =
      decode(with_exif(fixtures::rgba_lossless, identified_tiff));
  REQUIRE(identified);
  REQUIRE(identified->source_orientation() ==
          rf::Orientation::rotate_90_clockwise);
}

TEST_CASE("independent WebP decoder contexts run concurrently",
          "[decode][webp][concurrency]") {
  std::vector<std::future<bool>> decodes;
  for (int index = 0; index < 8; ++index) {
    decodes.emplace_back(std::async(std::launch::async, [] {
      const auto result = decode(fixtures::rgba_lossless);
      if (!result) {
        return false;
      }
      const auto row = result->view().row(0);
      return row && (*row)[0] == rf::Rgba8{10, 20, 30, 255} &&
             (*row)[1] == rf::Rgba8{200, 100, 50, 0};
    }));
  }
  for (auto &result : decodes) {
    REQUIRE(result.get());
  }
}
