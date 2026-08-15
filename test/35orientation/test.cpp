#include <catch2/catch_test_macros.hpp>

#include <rasterforge/rasterforge.hpp>

#include "../../src/lib/orientation.hpp"
#include "../26png-decode/fixtures.hpp"
#include "../34jpeg-decode/fixtures.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <utility>
#include <vector>

namespace rf = rasterforge;
namespace detail = rasterforge::detail;

namespace {

[[nodiscard]] auto bytes(const std::vector<std::uint8_t> &encoded)
    -> std::span<const std::byte> {
  return std::as_bytes(std::span{encoded});
}

[[nodiscard]] auto make_labeled_image() -> rf::Image {
  auto result = rf::Image::create({3, 2});
  REQUIRE(result);
  auto image = std::move(*result);
  for (std::uint32_t y = 0; y < 2; ++y) {
    auto row = image.mutable_view().row(y);
    REQUIRE(row);
    for (std::uint32_t x = 0; x < 3; ++x) {
      const auto label = static_cast<std::uint8_t>((y * 3U) + x + 1U);
      (*row)[x] = rf::Rgba8{label, 0, 0, 255};
    }
  }
  return image;
}

[[nodiscard]] auto labels(rf::ImageView view) -> std::vector<std::uint8_t> {
  std::vector<std::uint8_t> result;
  for (std::uint32_t y = 0; y < view.extent().height; ++y) {
    const auto row = view.row(y);
    REQUIRE(row);
    for (const auto pixel : *row) {
      result.push_back(pixel.r);
    }
  }
  return result;
}

void write_u16(std::vector<std::uint8_t> &bytes, std::size_t offset,
               std::uint16_t value, bool little_endian) {
  if (little_endian) {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
  } else {
    bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value);
  }
}

void write_u32(std::vector<std::uint8_t> &bytes, std::size_t offset,
               std::uint32_t value, bool little_endian) {
  for (std::size_t index = 0; index < 4; ++index) {
    const auto shift = little_endian ? index * 8U : (3U - index) * 8U;
    bytes[offset + index] = static_cast<std::uint8_t>(value >> shift);
  }
}

[[nodiscard]] auto make_tiff(bool little_endian, std::uint16_t value,
                             std::uint16_t tag = 0x0112U,
                             std::uint16_t type = 3U, std::uint32_t count = 1U)
    -> std::vector<std::uint8_t> {
  std::vector<std::uint8_t> tiff(26U, 0U);
  tiff[0] = static_cast<std::uint8_t>(little_endian ? 'I' : 'M');
  tiff[1] = tiff[0];
  write_u16(tiff, 2U, 42U, little_endian);
  write_u32(tiff, 4U, 8U, little_endian);
  write_u16(tiff, 8U, 1U, little_endian);
  write_u16(tiff, 10U, tag, little_endian);
  write_u16(tiff, 12U, type, little_endian);
  write_u32(tiff, 14U, count, little_endian);
  write_u16(tiff, 18U, value, little_endian);
  return tiff;
}

[[nodiscard]] auto as_bytes(const std::vector<std::uint8_t> &data)
    -> std::span<const std::byte> {
  return std::as_bytes(std::span{data});
}

[[nodiscard]] auto crc32(std::span<const std::uint8_t> data) -> std::uint32_t {
  std::uint32_t crc{0xFFFFFFFFU};
  for (const auto byte : data) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      const auto mask =
          static_cast<std::uint32_t>(-static_cast<std::int32_t>(crc & 1U));
      crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }
  return ~crc;
}

void append_big_u32(std::vector<std::uint8_t> &bytes, std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value));
}

[[nodiscard]] auto png_with_exif(const std::vector<std::uint8_t> &tiff)
    -> std::vector<std::uint8_t> {
  const auto &fixture = png_fixtures::rgb_png;
  std::vector<std::uint8_t> encoded(fixture.begin(), fixture.end());
  std::vector<std::uint8_t> chunk;
  append_big_u32(chunk, static_cast<std::uint32_t>(tiff.size()));
  constexpr std::array type{std::uint8_t{'e'}, std::uint8_t{'X'},
                            std::uint8_t{'I'}, std::uint8_t{'f'}};
  chunk.insert(chunk.end(), type.begin(), type.end());
  chunk.insert(chunk.end(), tiff.begin(), tiff.end());
  const auto checksum = crc32(
      std::span<const std::uint8_t>{chunk.data(), chunk.size()}.subspan(4U));
  append_big_u32(chunk, checksum);
  encoded.insert(encoded.begin() + 33, chunk.begin(), chunk.end());
  return encoded;
}

[[nodiscard]] auto jpeg_with_exif(const std::vector<std::uint8_t> &tiff)
    -> std::vector<std::uint8_t> {
  auto encoded = jpeg_fixtures::decode_base64(jpeg_fixtures::rgb444_base64);
  std::vector<std::uint8_t> marker{0xFFU,
                                   0xE1U,
                                   0U,
                                   0U,
                                   static_cast<std::uint8_t>('E'),
                                   static_cast<std::uint8_t>('x'),
                                   static_cast<std::uint8_t>('i'),
                                   static_cast<std::uint8_t>('f'),
                                   0U,
                                   0U};
  marker.insert(marker.end(), tiff.begin(), tiff.end());
  const auto marker_length = static_cast<std::uint16_t>(marker.size() - 2U);
  marker[2] = static_cast<std::uint8_t>(marker_length >> 8U);
  marker[3] = static_cast<std::uint8_t>(marker_length);
  encoded.insert(encoded.begin() + 2, marker.begin(), marker.end());
  return encoded;
}

[[nodiscard]] auto pixels(rf::ImageView view) -> std::vector<rf::Rgba8> {
  std::vector<rf::Rgba8> result;
  for (std::uint32_t y = 0; y < view.extent().height; ++y) {
    const auto row = view.row(y);
    REQUIRE(row);
    result.insert(result.end(), row->begin(), row->end());
  }
  return result;
}

} // namespace

TEST_CASE("all eight EXIF orientations map every labeled pixel exactly",
          "[orientation][pixels]") {
  struct Case {
    rf::Orientation orientation;
    rf::Extent extent;
    std::vector<std::uint8_t> expected;
  };
  const std::array cases{
      Case{rf::Orientation::identity, {3, 2}, {1, 2, 3, 4, 5, 6}},
      Case{rf::Orientation::mirror_horizontal, {3, 2}, {3, 2, 1, 6, 5, 4}},
      Case{rf::Orientation::rotate_180, {3, 2}, {6, 5, 4, 3, 2, 1}},
      Case{rf::Orientation::mirror_vertical, {3, 2}, {4, 5, 6, 1, 2, 3}},
      Case{rf::Orientation::transpose, {2, 3}, {1, 4, 2, 5, 3, 6}},
      Case{rf::Orientation::rotate_90_clockwise, {2, 3}, {4, 1, 5, 2, 6, 3}},
      Case{rf::Orientation::transverse, {2, 3}, {6, 3, 5, 2, 4, 1}},
      Case{rf::Orientation::rotate_270_clockwise, {2, 3}, {3, 6, 2, 5, 1, 4}},
  };

  for (const auto &test : cases) {
    auto oriented =
        detail::apply_orientation(make_labeled_image(), test.orientation, {});
    REQUIRE(oriented);
    REQUIRE(oriented->extent() == test.extent);
    REQUIRE(labels(oriented->view()) == test.expected);
  }
}

TEST_CASE("EXIF parser accepts both byte orders and rejects ambiguous fields",
          "[orientation][metadata][failure]") {
  for (const auto &[little_endian, value] :
       {std::pair{true, std::uint16_t{6}},
        std::pair{false, std::uint16_t{8}}}) {
    const auto tiff = make_tiff(little_endian, value);
    const auto parsed = detail::parse_exif_orientation(as_bytes(tiff));
    REQUIRE_FALSE(parsed.invalid);
    REQUIRE(parsed.value == static_cast<rf::Orientation>(value));
  }

  SECTION("valid TIFF without an orientation tag is not present") {
    const auto tiff = make_tiff(true, 6U, 0x0100U);
    const auto parsed = detail::parse_exif_orientation(as_bytes(tiff));
    REQUIRE_FALSE(parsed.invalid);
    REQUIRE_FALSE(parsed.value);
  }

  SECTION("invalid value, type, and count") {
    for (const auto &tiff : {make_tiff(true, 0U), make_tiff(true, 9U),
                             make_tiff(true, 6U, 0x0112U, 4U),
                             make_tiff(true, 6U, 0x0112U, 3U, 2U)}) {
      REQUIRE(detail::parse_exif_orientation(as_bytes(tiff)).invalid);
    }
  }

  SECTION("truncated directory and overflowing offset") {
    auto truncated = make_tiff(true, 6U);
    truncated.pop_back();
    REQUIRE(detail::parse_exif_orientation(as_bytes(truncated)).invalid);

    auto bad_offset = make_tiff(true, 6U);
    write_u32(bad_offset, 4U, 0xFFFFFFFFU, true);
    REQUIRE(detail::parse_exif_orientation(as_bytes(bad_offset)).invalid);
  }

  SECTION("duplicate records do not acquire a precedence rule") {
    detail::OrientationMetadata aggregate{};
    detail::merge_orientation_metadata(
        aggregate,
        detail::parse_exif_orientation(as_bytes(make_tiff(true, 2U))));
    detail::merge_orientation_metadata(
        aggregate,
        detail::parse_exif_orientation(as_bytes(make_tiff(false, 2U))));
    REQUIRE(aggregate.invalid);
    REQUIRE_FALSE(aggregate.value);
  }
}

TEST_CASE("orientation transforms enforce output and temporary limits",
          "[orientation][limits][failure]") {
  rf::Limits limits{};
  limits.max_dimension = 3U;
  limits.max_pixels = 6U;
  limits.max_output_bytes = 24U;
  limits.max_temporary_bytes = 24U;
  auto accepted = detail::apply_orientation(
      make_labeled_image(), rf::Orientation::rotate_90_clockwise, limits);
  REQUIRE(accepted);
  REQUIRE(accepted->extent() == rf::Extent{2, 3});

  SECTION("temporary storage one past") {
    limits.max_temporary_bytes = 23U;
    const auto result = detail::apply_orientation(
        make_labeled_image(), rf::Orientation::rotate_90_clockwise, limits);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("pixel count one past") {
    limits.max_pixels = 5U;
    const auto result = detail::apply_orientation(
        make_labeled_image(), rf::Orientation::rotate_90_clockwise, limits);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }

  SECTION("output bytes one past") {
    limits.max_output_bytes = 23U;
    const auto result = detail::apply_orientation(
        make_labeled_image(), rf::Orientation::rotate_90_clockwise, limits);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == rf::ErrorCode::resource_limit);
  }
}

TEST_CASE("PNG and JPEG expose and apply codec-neutral orientation metadata",
          "[decode][orientation][metadata]") {
  const std::array formats{
      std::pair{rf::ImageFormat::png, png_with_exif(make_tiff(true, 2U))},
      std::pair{rf::ImageFormat::jpeg, jpeg_with_exif(make_tiff(false, 2U))},
  };

  for (const auto &[format, encoded] : formats) {
    std::vector<std::uint8_t> plain;
    if (format == rf::ImageFormat::png) {
      plain.assign(png_fixtures::rgb_png.begin(), png_fixtures::rgb_png.end());
    } else {
      plain = jpeg_fixtures::decode_base64(jpeg_fixtures::rgb444_base64);
    }
    const auto source = rf::decode(bytes(plain));
    const auto applied = rf::decode(bytes(encoded));
    REQUIRE(source);
    REQUIRE(applied);
    REQUIRE_FALSE(source->source_orientation());
    REQUIRE(source->orientation_status() ==
            rf::OrientationStatus::not_present);
    REQUIRE(applied->format() == format);
    REQUIRE(applied->encoded_extent() == source->encoded_extent());
    REQUIRE(applied->output_extent() == source->output_extent());
    REQUIRE(applied->source_orientation() ==
            rf::Orientation::mirror_horizontal);
    REQUIRE(applied->orientation_status() == rf::OrientationStatus::applied);

    auto expected = pixels(source->view());
    REQUIRE(expected.size() == 2U);
    std::swap(expected[0], expected[1]);
    REQUIRE(pixels(applied->view()) == expected);
  }
}

TEST_CASE(
    "decode reports ignored, invalid, and identity orientation distinctly",
    "[decode][orientation][policy][failure]") {
  SECTION("valid metadata can be reported without changing pixels") {
    const auto encoded = png_with_exif(make_tiff(true, 6U));
    rf::DecodeOptions options{};
    options.orientation = rf::OrientationPolicy::ignore;
    const auto decoded = rf::decode(bytes(encoded), options);
    REQUIRE(decoded);
    REQUIRE(decoded->encoded_extent() == rf::Extent{2, 1});
    REQUIRE(decoded->output_extent() == rf::Extent{2, 1});
    REQUIRE(decoded->source_orientation() ==
            rf::Orientation::rotate_90_clockwise);
    REQUIRE(decoded->orientation_status() == rf::OrientationStatus::ignored);
  }

  SECTION("invalid metadata is ignored but remains observable") {
    const auto encoded = jpeg_with_exif(make_tiff(true, 9U));
    const auto decoded = rf::decode(bytes(encoded));
    REQUIRE(decoded);
    REQUIRE(decoded->encoded_extent() == decoded->output_extent());
    REQUIRE_FALSE(decoded->source_orientation());
    REQUIRE(decoded->orientation_status() ==
            rf::OrientationStatus::invalid_ignored);
  }

  SECTION("identity is a present and applied no-op") {
    const auto encoded = png_with_exif(make_tiff(false, 1U));
    const auto decoded = rf::decode(bytes(encoded));
    REQUIRE(decoded);
    REQUIRE(decoded->source_orientation() == rf::Orientation::identity);
    REQUIRE(decoded->orientation_status() == rf::OrientationStatus::applied);
  }
}

TEST_CASE("JPEG APP1 framing failures remain structured decode errors",
          "[decode][jpeg][orientation][failure]") {
  SECTION("marker length shorter than its own length field") {
    auto encoded = jpeg_with_exif(make_tiff(true, 6U));
    encoded[4] = 0U;
    encoded[5] = 1U;
    const auto decoded = rf::decode(bytes(encoded));
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().code == rf::ErrorCode::malformed_data);
  }

  SECTION("marker payload extends beyond the in-memory source") {
    auto encoded = jpeg_with_exif(make_tiff(true, 6U));
    encoded[4] = 0xFFU;
    encoded[5] = 0xFFU;
    const auto decoded = rf::decode(bytes(encoded));
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().code == rf::ErrorCode::truncated_data);
  }
}
