#include "../26png-decode/fixtures.hpp"
#include "../34jpeg-decode/fixtures.hpp"
#include "../36webp-decode/fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <rasterforge/rasterforge.hpp>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace rf = rasterforge;

namespace {

using Bytes = std::vector<std::uint8_t>;

[[nodiscard]] auto encoded_bytes(const Bytes &encoded)
    -> std::span<const std::byte> {
  return std::as_bytes(std::span{encoded});
}

void append_be32(Bytes &output, std::uint32_t value) {
  output.push_back(static_cast<std::uint8_t>(value >> 24U));
  output.push_back(static_cast<std::uint8_t>(value >> 16U));
  output.push_back(static_cast<std::uint8_t>(value >> 8U));
  output.push_back(static_cast<std::uint8_t>(value));
}

void append_le32(Bytes &output, std::uint32_t value) {
  output.push_back(static_cast<std::uint8_t>(value));
  output.push_back(static_cast<std::uint8_t>(value >> 8U));
  output.push_back(static_cast<std::uint8_t>(value >> 16U));
  output.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void write_be16(Bytes &output, std::size_t offset, std::uint16_t value) {
  REQUIRE(offset + 2U <= output.size());
  output[offset] = static_cast<std::uint8_t>(value >> 8U);
  output[offset + 1U] = static_cast<std::uint8_t>(value);
}

void write_be32(Bytes &output, std::size_t offset, std::uint32_t value) {
  REQUIRE(offset + 4U <= output.size());
  output[offset] = static_cast<std::uint8_t>(value >> 24U);
  output[offset + 1U] = static_cast<std::uint8_t>(value >> 16U);
  output[offset + 2U] = static_cast<std::uint8_t>(value >> 8U);
  output[offset + 3U] = static_cast<std::uint8_t>(value);
}

void write_le32(Bytes &output, std::size_t offset, std::uint32_t value) {
  REQUIRE(offset + 4U <= output.size());
  output[offset] = static_cast<std::uint8_t>(value);
  output[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
  output[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
  output[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

[[nodiscard]] auto read_be32(const Bytes &input, std::size_t offset)
    -> std::uint32_t {
  REQUIRE(offset + 4U <= input.size());
  return (static_cast<std::uint32_t>(input[offset]) << 24U) |
         (static_cast<std::uint32_t>(input[offset + 1U]) << 16U) |
         (static_cast<std::uint32_t>(input[offset + 2U]) << 8U) |
         static_cast<std::uint32_t>(input[offset + 3U]);
}

[[nodiscard]] auto crc32(std::span<const std::uint8_t> input) -> std::uint32_t {
  std::uint32_t crc{0xFFFFFFFFU};
  for (const auto byte : input) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      const auto mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }
  return crc ^ 0xFFFFFFFFU;
}

[[nodiscard]] auto adler32(std::span<const std::uint8_t> input)
    -> std::uint32_t {
  constexpr std::uint32_t modulus{65'521U};
  std::uint32_t first{1U};
  std::uint32_t second{};
  for (const auto byte : input) {
    first = (first + byte) % modulus;
    second = (second + first) % modulus;
  }
  return (second << 16U) | first;
}

[[nodiscard]] auto zlib_store(std::span<const std::uint8_t> input) -> Bytes {
  Bytes output{0x78U, 0x01U};
  std::size_t offset{};
  do {
    const auto block_size =
        std::min<std::size_t>(input.size() - offset, 0xFFFFU);
    output.push_back(offset + block_size == input.size() ? 1U : 0U);
    const auto length = static_cast<std::uint16_t>(block_size);
    const auto complement = static_cast<std::uint16_t>(~length);
    output.push_back(static_cast<std::uint8_t>(length));
    output.push_back(static_cast<std::uint8_t>(length >> 8U));
    output.push_back(static_cast<std::uint8_t>(complement));
    output.push_back(static_cast<std::uint8_t>(complement >> 8U));
    output.insert(
        output.end(), input.begin() + static_cast<std::ptrdiff_t>(offset),
        input.begin() + static_cast<std::ptrdiff_t>(offset + block_size));
    offset += block_size;
  } while (offset < input.size());
  append_be32(output, adler32(input));
  return output;
}

[[nodiscard]] auto make_icc_profile(std::size_t size = 132U) -> Bytes {
  REQUIRE(size >= 132U);
  REQUIRE(size <= 0xFFFFFFFFU);
  Bytes profile(size);
  write_be32(profile, 0U, static_cast<std::uint32_t>(size));
  write_be32(profile, 8U, 0x04300000U);
  std::copy_n(std::string_view{"mntr"}.begin(), 4U, profile.begin() + 12);
  std::copy_n(std::string_view{"RGB "}.begin(), 4U, profile.begin() + 16);
  std::copy_n(std::string_view{"XYZ "}.begin(), 4U, profile.begin() + 20);
  write_be16(profile, 24U, 2026U);
  write_be16(profile, 26U, 8U);
  write_be16(profile, 28U, 25U);
  std::copy_n(std::string_view{"acsp"}.begin(), 4U, profile.begin() + 36);
  write_be32(profile, 68U, 0x0000F6D6U);
  write_be32(profile, 72U, 0x00010000U);
  write_be32(profile, 76U, 0x0000D32DU);
  return profile;
}

[[nodiscard]] auto compress_profile(std::span<const std::uint8_t> profile)
    -> Bytes {
  REQUIRE(profile.size() <= std::numeric_limits<uLong>::max());
  const auto source_size = static_cast<uLong>(profile.size());
  auto compressed_size = compressBound(source_size);
  Bytes compressed(static_cast<std::size_t>(compressed_size));
  REQUIRE(compress2(compressed.data(), &compressed_size, profile.data(),
                    source_size, Z_BEST_COMPRESSION) == Z_OK);
  compressed.resize(static_cast<std::size_t>(compressed_size));
  return compressed;
}

[[nodiscard]] auto png_base() -> Bytes {
  return {png_fixtures::rgb_png.begin(), png_fixtures::rgb_png.end()};
}

[[nodiscard]] auto with_png_chunk(Bytes encoded,
                                  const std::array<std::uint8_t, 4> &name,
                                  std::span<const std::uint8_t> payload)
    -> Bytes {
  constexpr std::array<std::uint8_t, 4> idat{'I', 'D', 'A', 'T'};
  std::size_t offset{8U};
  while (offset + 12U <= encoded.size()) {
    const auto length = static_cast<std::size_t>(read_be32(encoded, offset));
    REQUIRE(length <= encoded.size() - offset - 12U);
    if (std::equal(idat.begin(), idat.end(),
                   encoded.begin() +
                       static_cast<std::ptrdiff_t>(offset + 4U))) {
      break;
    }
    offset += length + 12U;
  }
  REQUIRE(offset + 12U <= encoded.size());

  Bytes chunk;
  append_be32(chunk, static_cast<std::uint32_t>(payload.size()));
  chunk.insert(chunk.end(), name.begin(), name.end());
  chunk.insert(chunk.end(), payload.begin(), payload.end());
  append_be32(chunk, crc32(std::span{chunk}.subspan(4U)));
  encoded.insert(encoded.begin() + static_cast<std::ptrdiff_t>(offset),
                 chunk.begin(), chunk.end());
  return encoded;
}

[[nodiscard]] auto with_png_icc(Bytes encoded,
                                std::span<const std::uint8_t> compressed)
    -> Bytes {
  Bytes payload{'r', 'f', 0U, 0U};
  payload.insert(payload.end(), compressed.begin(), compressed.end());
  return with_png_chunk(std::move(encoded), {'i', 'C', 'C', 'P'}, payload);
}

[[nodiscard]] auto jpeg_base() -> Bytes {
  return jpeg_fixtures::decode_base64(jpeg_fixtures::rgb444_base64);
}

[[nodiscard]] auto with_jpeg_icc(Bytes encoded,
                                 std::span<const std::uint8_t> profile)
    -> Bytes {
  constexpr std::size_t marker_payload_max{65'519U};
  const auto marker_count = std::max<std::size_t>(
      1U, (profile.size() + marker_payload_max - 1U) / marker_payload_max);
  REQUIRE(marker_count <= 255U);
  Bytes markers;
  for (std::size_t index = 0; index < marker_count; ++index) {
    const auto offset = index * marker_payload_max;
    const auto payload_size =
        std::min(marker_payload_max, profile.size() - offset);
    const auto length = static_cast<std::uint16_t>(payload_size + 16U);
    markers.insert(markers.end(),
                   {0xFFU, 0xE2U, static_cast<std::uint8_t>(length >> 8U),
                    static_cast<std::uint8_t>(length)});
    constexpr std::array identifier{'I', 'C', 'C', '_', 'P', 'R',
                                    'O', 'F', 'I', 'L', 'E', '\0'};
    markers.insert(markers.end(), identifier.begin(), identifier.end());
    markers.push_back(static_cast<std::uint8_t>(index + 1U));
    markers.push_back(static_cast<std::uint8_t>(marker_count));
    markers.insert(
        markers.end(), profile.begin() + static_cast<std::ptrdiff_t>(offset),
        profile.begin() + static_cast<std::ptrdiff_t>(offset + payload_size));
  }
  encoded.insert(encoded.begin() + 2, markers.begin(), markers.end());
  return encoded;
}

[[nodiscard]] auto webp_base() -> Bytes { return webp_fixtures::rgba_lossless; }

[[nodiscard]] auto with_webp_icc(const Bytes &simple,
                                 std::span<const std::uint8_t> profile)
    -> Bytes {
  REQUIRE(simple.size() >= 12U);
  Bytes result{'R', 'I', 'F', 'F', 0,   0,   0,   0,
               'W', 'E', 'B', 'P', 'V', 'P', '8', 'X'};
  append_le32(result, 10U);
  constexpr std::array<std::uint8_t, 10> vp8x{0x30U, 0U, 0U, 0U, 1U,
                                              0U,    0U, 0U, 0U, 0U};
  result.insert(result.end(), vp8x.begin(), vp8x.end());
  result.insert(result.end(), {'I', 'C', 'C', 'P'});
  append_le32(result, static_cast<std::uint32_t>(profile.size()));
  result.insert(result.end(), profile.begin(), profile.end());
  if ((profile.size() & 1U) != 0U) {
    result.push_back(0U);
  }
  result.insert(result.end(), simple.begin() + 12, simple.end());
  write_le32(result, 4U, static_cast<std::uint32_t>(result.size() - 8U));
  return result;
}

[[nodiscard]] auto pixels(const rf::DecodedImage &decoded)
    -> std::vector<rf::Rgba8> {
  std::vector<rf::Rgba8> output;
  for (std::uint32_t y = 0; y < decoded.output_extent().height; ++y) {
    const auto row = decoded.view().row(y);
    REQUIRE(row);
    output.insert(output.end(), row->begin(), row->end());
  }
  return output;
}

void require_same_decode(const Bytes &plain, const Bytes &tagged) {
  const auto plain_result = rf::decode(encoded_bytes(plain));
  const auto tagged_result = rf::decode(encoded_bytes(tagged));
  REQUIRE(plain_result);
  REQUIRE(tagged_result);
  REQUIRE(tagged_result->format() == plain_result->format());
  REQUIRE(tagged_result->encoded_extent() == plain_result->encoded_extent());
  REQUIRE(tagged_result->output_extent() == plain_result->output_extent());
  REQUIRE(tagged_result->has_alpha() == plain_result->has_alpha());
  REQUIRE(pixels(*tagged_result) == pixels(*plain_result));
}

void require_input_bound(const Bytes &encoded) {
  rf::DecodeOptions options{};
  options.limits.max_input_bytes = encoded.size();
  REQUIRE(rf::decode(encoded_bytes(encoded), options));
  options.limits.max_input_bytes = encoded.size() - 1U;
  const auto rejected = rf::decode(encoded_bytes(encoded), options);
  REQUIRE_FALSE(rejected);
  REQUIRE(rejected.error().code == rf::ErrorCode::input_too_large);
}

} // namespace

TEST_CASE("color metadata does not change decoded sRGBA samples",
          "[decode][metadata][color]") {
  const auto profile = make_icc_profile();

  SECTION("PNG ICC") {
    require_same_decode(png_base(),
                        with_png_icc(png_base(), zlib_store(profile)));
  }
  SECTION("PNG gamma, chromaticity, and sRGB intent") {
    Bytes gamma;
    append_be32(gamma, 45'455U);
    Bytes chromaticity;
    for (const auto value : {31'270U, 32'900U, 64'000U, 33'000U, 30'000U,
                             60'000U, 15'000U, 6'000U}) {
      append_be32(chromaticity, value);
    }
    auto tagged = with_png_chunk(png_base(), {'g', 'A', 'M', 'A'}, gamma);
    tagged =
        with_png_chunk(std::move(tagged), {'c', 'H', 'R', 'M'}, chromaticity);
    tagged = with_png_chunk(std::move(tagged), {'s', 'R', 'G', 'B'}, Bytes{0U});
    require_same_decode(png_base(), tagged);
  }
  SECTION("JPEG ICC") {
    require_same_decode(jpeg_base(), with_jpeg_icc(jpeg_base(), profile));
  }
  SECTION("WebP ICC") {
    require_same_decode(webp_base(), with_webp_icc(webp_base(), profile));
  }
}

TEST_CASE("well-framed invalid color profiles are ignored",
          "[decode][metadata][color][malformed]") {
  const Bytes invalid_profile{1U, 2U, 3U};
  require_same_decode(png_base(),
                      with_png_icc(png_base(), zlib_store(invalid_profile)));
  require_same_decode(jpeg_base(), with_jpeg_icc(jpeg_base(), invalid_profile));
  require_same_decode(webp_base(), with_webp_icc(webp_base(), invalid_profile));
}

TEST_CASE("ignored color metadata remains inside the resource contract",
          "[decode][metadata][color][limits]") {
  const auto large_profile = make_icc_profile(1U << 20U);
  const auto png = with_png_icc(png_base(), compress_profile(large_profile));
  const auto jpeg = with_jpeg_icc(jpeg_base(), large_profile);
  const auto webp = with_webp_icc(webp_base(), large_profile);

  SECTION("oversized profiles do not change decoded samples") {
    require_same_decode(png_base(), png);
    require_same_decode(jpeg_base(), jpeg);
    require_same_decode(webp_base(), webp);
  }

  SECTION("metadata counts toward the encoded input limit") {
    require_input_bound(png);
    require_input_bound(jpeg);
    require_input_bound(webp);
  }

  SECTION("compressed PNG profiles are not inflated") {
    rf::DecodeOptions options{};
    options.limits.max_input_bytes = png.size();
    options.limits.max_temporary_bytes = 256U * 1024U;
    REQUIRE(rf::decode(encoded_bytes(png), options));
  }
}
