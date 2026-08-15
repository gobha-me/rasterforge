#include "fixtures.hpp"

#include "../../src/lib/jpeg_decoder.hpp"

#include <rasterforge/rasterforge.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rf = rasterforge;
namespace detail = rasterforge::detail;
namespace fixtures = jpeg_fixtures;

namespace {

[[nodiscard]] auto bytes(const std::vector<std::uint8_t> &encoded)
    -> std::span<const std::byte> {
  return std::as_bytes(std::span{encoded});
}

[[nodiscard]] auto find_marker(const std::vector<std::uint8_t> &encoded,
                               std::uint8_t marker) -> std::size_t {
  const std::array needle{std::uint8_t{0xFF}, marker};
  const auto found =
      std::search(encoded.begin(), encoded.end(), needle.begin(), needle.end());
  REQUIRE(found != encoded.end());
  return static_cast<std::size_t>(found - encoded.begin());
}

[[nodiscard]] auto entropy_start(const std::vector<std::uint8_t> &encoded)
    -> std::size_t {
  const auto scan = find_marker(encoded, 0xDA);
  REQUIRE(scan + 4U <= encoded.size());
  const auto length =
      (static_cast<std::size_t>(encoded[scan + 2U]) << 8U) | encoded[scan + 3U];
  REQUIRE(length >= 2U);
  REQUIRE(scan + 2U + length < encoded.size());
  return scan + 2U + length;
}

[[nodiscard]] auto decode(const std::vector<std::uint8_t> &encoded,
                          const rf::DecodeOptions &options = {}) {
  return rf::decode(bytes(encoded), options);
}

} // namespace

TEST_CASE("JPEG signature classification is bounded and codec-neutral",
          "[decode][jpeg][signature]") {
  const std::vector<std::uint8_t> partial{0xFF};
  const auto partial_result = decode(partial);
  REQUIRE_FALSE(partial_result);
  REQUIRE(partial_result.error().code == rf::ErrorCode::truncated_data);

  const std::vector<std::uint8_t> soi{0xFF, 0xD8};
  const auto soi_result = decode(soi);
  REQUIRE_FALSE(soi_result);
  REQUIRE(soi_result.error().code == rf::ErrorCode::truncated_data);

  const std::vector<std::uint8_t> mismatch{0xFF, 0x00};
  const auto mismatch_result = decode(mismatch);
  REQUIRE_FALSE(mismatch_result);
  REQUIRE(mismatch_result.error().code == rf::ErrorCode::unsupported_format);
  STATIC_REQUIRE(rf::decode_signature_prefix_bytes == 8);
}

TEST_CASE("baseline RGB and grayscale JPEG normalize to opaque RGBA",
          "[decode][jpeg][pixels][metadata]") {
  SECTION("RGB 4:4:4") {
    const auto result =
        decode(fixtures::decode_base64(fixtures::rgb444_base64));
    REQUIRE(result);
    REQUIRE(result->format() == rf::ImageFormat::jpeg);
    REQUIRE(result->encoded_extent() == rf::Extent{2, 1});
    REQUIRE(result->output_extent() == rf::Extent{2, 1});
    REQUIRE_FALSE(result->has_alpha());
    REQUIRE(result->orientation_status() == rf::OrientationStatus::not_present);

    const auto row = result->view().row(0);
    REQUIRE(row);
    REQUIRE((*row)[0] == rf::Rgba8{10, 20, 30, 255});
    REQUIRE((*row)[1] == rf::Rgba8{10, 20, 30, 255});
  }

  SECTION("grayscale") {
    const auto result = decode(fixtures::decode_base64(fixtures::gray_base64));
    REQUIRE(result);
    const auto row = result->view().row(0);
    REQUIRE(row);
    REQUIRE((*row)[0] == rf::Rgba8{128, 128, 128, 255});
    REQUIRE((*row)[1] == rf::Rgba8{128, 128, 128, 255});
  }
}

TEST_CASE("progressive and subsampled JPEG inputs decode completely",
          "[decode][jpeg][progressive][subsampling]") {
  SECTION("progressive") {
    const auto result =
        decode(fixtures::decode_base64(fixtures::progressive_base64));
    if (!result) {
      INFO(result.error().message);
    }
    REQUIRE(result);
    REQUIRE(result->output_extent() == rf::Extent{2, 2});
    for (std::uint32_t y = 0; y < 2; ++y) {
      const auto row = result->view().row(y);
      REQUIRE(row);
      REQUIRE((*row)[0] == rf::Rgba8{33, 64, 128, 255});
      REQUIRE((*row)[1] == rf::Rgba8{33, 64, 128, 255});
    }
  }

  for (const auto encoded :
       {fixtures::sub422_base64, fixtures::sub420_base64}) {
    const auto result = decode(fixtures::decode_base64(encoded));
    if (!result) {
      INFO(result.error().message);
    }
    REQUIRE(result);
    REQUIRE(result->output_extent() == rf::Extent{8, 8});
    const auto row = result->view().row(0);
    REQUIRE(row);
    REQUIRE((*row)[0] == rf::Rgba8{254, 0, 0, 255});
    REQUIRE((*row)[7] == rf::Rgba8{0, 1, 252, 255});
  }
}

TEST_CASE("JPEG malformed and truncated inputs never return partial pixels",
          "[decode][jpeg][failure]") {
  const auto valid = fixtures::decode_base64(fixtures::rgb444_base64);
  const auto start_of_scan = find_marker(valid, 0xDA);

  for (const auto end :
       {std::size_t{3}, std::size_t{20}, start_of_scan, valid.size() - 2U}) {
    const std::vector<std::uint8_t> truncated(valid.begin(),
                                              valid.begin() + end);
    const auto result = decode(truncated);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::truncated_data);
  }

  auto corrupt = valid;
  const auto entropy = entropy_start(corrupt);
  REQUIRE(entropy + 1U < corrupt.size());
  corrupt[entropy] = 0xFF;
  corrupt[entropy + 1U] = 0xC0;
  const auto corrupt_result = decode(corrupt);
  REQUIRE_FALSE(corrupt_result);
  REQUIRE(corrupt_result.error().code == rf::ErrorCode::malformed_data);
}

TEST_CASE("unsupported JPEG processes have a stable category",
          "[decode][jpeg][unsupported]") {
  SECTION("CMYK") {
    const auto result = decode(fixtures::decode_base64(fixtures::cmyk_base64));
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::unsupported_feature);
  }

  SECTION("arithmetic-coded process") {
    auto arithmetic = fixtures::decode_base64(fixtures::rgb444_base64);
    const auto start_of_frame = find_marker(arithmetic, 0xC0);
    arithmetic[start_of_frame + 1U] = 0xC9;
    const auto result = decode(arithmetic);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::unsupported_feature);
  }
}

TEST_CASE("JPEG EXIF orientation is exposed and normalized by default",
          "[decode][jpeg][metadata][orientation]") {
  auto encoded = fixtures::decode_base64(fixtures::rgb444_base64);
  constexpr std::array exif_orientation_six{
      std::uint8_t{0xFF}, std::uint8_t{0xE1}, std::uint8_t{0x00},
      std::uint8_t{0x22}, std::uint8_t{'E'},  std::uint8_t{'x'},
      std::uint8_t{'i'},  std::uint8_t{'f'},  std::uint8_t{0x00},
      std::uint8_t{0x00}, std::uint8_t{'I'},  std::uint8_t{'I'},
      std::uint8_t{0x2A}, std::uint8_t{0x00}, std::uint8_t{0x08},
      std::uint8_t{0x00}, std::uint8_t{0x00}, std::uint8_t{0x00},
      std::uint8_t{0x01}, std::uint8_t{0x00}, std::uint8_t{0x12},
      std::uint8_t{0x01}, std::uint8_t{0x03}, std::uint8_t{0x00},
      std::uint8_t{0x01}, std::uint8_t{0x00}, std::uint8_t{0x00},
      std::uint8_t{0x00}, std::uint8_t{0x06}, std::uint8_t{0x00},
      std::uint8_t{0x00}, std::uint8_t{0x00}, std::uint8_t{0x00},
      std::uint8_t{0x00}, std::uint8_t{0x00}, std::uint8_t{0x00},
  };
  encoded.insert(encoded.begin() + 2, exif_orientation_six.begin(),
                 exif_orientation_six.end());

  const auto result = decode(encoded);
  REQUIRE(result);
  REQUIRE(result->encoded_extent() == rf::Extent{2, 1});
  REQUIRE(result->output_extent() == rf::Extent{1, 2});
  REQUIRE(result->source_orientation() == rf::Orientation::rotate_90_clockwise);
  REQUIRE(result->orientation_status() == rf::OrientationStatus::applied);
}

TEST_CASE("JPEG dimensions and every resource class are checked",
          "[decode][jpeg][limits]") {
  const auto encoded = fixtures::decode_base64(fixtures::rgb444_base64);

  SECTION("input exact and one-past") {
    rf::DecodeOptions options{};
    options.limits.max_input_bytes = encoded.size();
    REQUIRE(decode(encoded, options));
    options.limits.max_input_bytes = encoded.size() - 1U;
    const auto result = decode(encoded, options);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::input_too_large);
  }

  SECTION("zero dimension") {
    auto zero_width = encoded;
    const auto sof = find_marker(zero_width, 0xC0);
    zero_width[sof + 7U] = 0;
    zero_width[sof + 8U] = 0;
    const auto result = decode(zero_width);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::invalid_dimensions);
  }

  SECTION("dimension exact and one-past") {
    rf::DecodeOptions options{};
    options.limits.max_dimension = 2;
    REQUIRE(decode(encoded, options));
    options.limits.max_dimension = 1;
    const auto result = decode(encoded, options);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("extreme encoded dimension") {
    auto extreme = encoded;
    const auto sof = find_marker(extreme, 0xC0);
    extreme[sof + 7U] = 0xFF;
    extreme[sof + 8U] = 0xFF;
    const auto result = decode(extreme);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("pixel exact and one-past") {
    rf::DecodeOptions options{};
    options.limits.max_pixels = 2;
    REQUIRE(decode(encoded, options));
    options.limits.max_pixels = 1;
    const auto result = decode(encoded, options);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("output exact and one-past") {
    rf::DecodeOptions options{};
    options.limits.max_output_bytes = 8;
    REQUIRE(decode(encoded, options));
    options.limits.max_output_bytes = 7;
    const auto result = decode(encoded, options);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("temporary exact and one-byte-short") {
    const auto required = detail::checked_jpeg_work_bytes({2, 1});
    REQUIRE(required);
    rf::DecodeOptions options{};
    options.limits.max_temporary_bytes = *required;
    REQUIRE(decode(encoded, options));
    options.limits.max_temporary_bytes = *required - 1U;
    const auto result = decode(encoded, options);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("progressive coefficients cannot spill to backing storage") {
    const auto progressive =
        fixtures::decode_base64(fixtures::progressive_budget_base64);
    const auto scanline_work = detail::checked_jpeg_work_bytes({256, 256});
    REQUIRE(scanline_work);
    rf::DecodeOptions options{};
    options.limits.max_temporary_bytes = *scanline_work;
    const auto result = decode(progressive, options);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }
}
