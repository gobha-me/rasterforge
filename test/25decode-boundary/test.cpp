#include <catch2/catch_test_macros.hpp>

#include <rasterforge/rasterforge.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace rf = rasterforge;

namespace {

constexpr std::array png_signature{
    std::byte{0x89}, std::byte{0x50}, std::byte{0x4E}, std::byte{0x47},
    std::byte{0x0D}, std::byte{0x0A}, std::byte{0x1A}, std::byte{0x0A},
};

} // namespace

static_assert(!std::is_default_constructible_v<rf::DecodedImage>);
static_assert(!std::is_copy_constructible_v<rf::DecodedImage>);
static_assert(std::is_nothrow_move_constructible_v<rf::DecodedImage>);
static_assert(std::is_same_v<
              decltype(std::declval<const rf::DecodedImage &>().format()),
              rf::ImageFormat>);
static_assert(std::is_same_v<
              decltype(std::declval<const rf::DecodedImage &>().encoded_extent()),
              rf::Extent>);
static_assert(std::is_same_v<
              decltype(std::declval<const rf::DecodedImage &>().output_extent()),
              rf::Extent>);
static_assert(std::is_same_v<
              decltype(std::declval<const rf::DecodedImage &>().has_alpha()),
              bool>);
static_assert(std::is_same_v<
              decltype(std::declval<const rf::DecodedImage &>()
                           .orientation_status()),
              rf::OrientationStatus>);

TEST_CASE("decode rejects empty and oversized inputs before detection",
          "[decode][failure][limits]") {
  SECTION("empty input") {
    const auto decoded = rf::decode({});
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().code == rf::ErrorCode::empty_input);
  }

  SECTION("the input limit is inclusive") {
    rf::DecodeOptions options{};
    options.limits.max_input_bytes = png_signature.size();

    const auto decoded = rf::decode(png_signature, options);
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().code == rf::ErrorCode::truncated_data);
  }

  SECTION("one byte beyond the input limit") {
    rf::DecodeOptions options{};
    options.limits.max_input_bytes = png_signature.size() - 1;
    options.orientation = static_cast<rf::OrientationPolicy>(0xFF);
    auto over_limit = png_signature;
    over_limit.front() ^= std::byte{0xFF};

    const auto decoded = rf::decode(over_limit, options);
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().code == rf::ErrorCode::input_too_large);
  }
}

TEST_CASE("decode distinguishes truncated PNG signatures from mismatches",
          "[decode][failure][signature]") {
  for (std::size_t size = 1; size < png_signature.size(); ++size) {
    CAPTURE(size);

    const auto prefix = std::span{png_signature}.first(size);
    const auto truncated = rf::decode(prefix);
    REQUIRE_FALSE(truncated);
    REQUIRE(truncated.error().code == rf::ErrorCode::truncated_data);

    auto mismatch = png_signature;
    mismatch[size - 1] ^= std::byte{0xFF};
    const auto unknown = rf::decode(std::span{mismatch}.first(size));
    REQUIRE_FALSE(unknown);
    REQUIRE(unknown.error().code == rf::ErrorCode::unsupported_format);
  }
}

TEST_CASE("decode detection is bounded to the documented signature prefix",
          "[decode][signature]") {
  STATIC_REQUIRE(rf::decode_signature_prefix_bytes == png_signature.size());

  SECTION("a complete recognized signature reaches the codec boundary") {
    const auto decoded = rf::decode(png_signature);
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().code == rf::ErrorCode::truncated_data);
  }

  SECTION("bytes after the recognized prefix do not change detection") {
    constexpr auto suffix_size = std::size_t{4};
    std::array<std::byte, png_signature.size() + suffix_size> encoded{};
    for (std::size_t index = 0; index < png_signature.size(); ++index) {
      encoded[index] = png_signature[index];
    }
    encoded[png_signature.size()] = std::byte{0xFF};

    const auto decoded = rf::decode(encoded);
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().code == rf::ErrorCode::truncated_data);
  }

  SECTION("a definite full-prefix mismatch is unsupported") {
    auto unknown = png_signature;
    unknown.back() ^= std::byte{0xFF};

    const auto decoded = rf::decode(unknown);
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().code == rf::ErrorCode::unsupported_format);
  }
}

TEST_CASE("decode rejects invalid orientation policies",
          "[decode][failure][options]") {
  rf::DecodeOptions options{};
  options.orientation = static_cast<rf::OrientationPolicy>(0xFF);

  const auto decoded = rf::decode(png_signature, options);
  REQUIRE_FALSE(decoded);
  REQUIRE(decoded.error().code == rf::ErrorCode::invalid_argument);
}

TEST_CASE("decode public metadata types have stable defaults",
          "[decode][metadata]") {
  const rf::DecodeOptions options{};
  REQUIRE(options.orientation == rf::OrientationPolicy::apply);
  REQUIRE(options.limits == rf::Limits{});

  STATIC_REQUIRE(static_cast<std::uint8_t>(rf::ImageFormat::png) == 1);
  STATIC_REQUIRE(static_cast<std::uint8_t>(rf::ImageFormat::jpeg) == 2);
  STATIC_REQUIRE(static_cast<std::uint8_t>(rf::OrientationStatus::not_present) ==
                 0);
}
