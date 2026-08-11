#include <rasterforge/rasterforge.hpp>

#include "jpeg_decoder.hpp"
#include "png_decoder.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace rasterforge {
namespace {

constexpr std::array png_signature{
    std::byte{0x89}, std::byte{0x50}, std::byte{0x4E}, std::byte{0x47},
    std::byte{0x0D}, std::byte{0x0A}, std::byte{0x1A}, std::byte{0x0A},
};

constexpr std::array jpeg_signature{
    std::byte{0xFF},
    std::byte{0xD8},
};

static_assert(png_signature.size() == decode_signature_prefix_bytes);
static_assert(jpeg_signature.size() <= decode_signature_prefix_bytes);

constexpr Error empty_input{ErrorCode::empty_input,
                            "encoded input must not be empty"};
constexpr Error input_too_large{
    ErrorCode::input_too_large,
    "encoded input exceeds the configured input-byte limit"};
constexpr Error unsupported_format{
    ErrorCode::unsupported_format,
    "encoded input does not have a recognized byte signature"};
constexpr Error truncated_signature{
    ErrorCode::truncated_data,
    "encoded input ends within a recognized byte signature"};
constexpr Error invalid_orientation{
    ErrorCode::invalid_argument,
    "decode orientation policy is not a recognized value"};
[[nodiscard]] constexpr auto valid(OrientationPolicy policy) noexcept -> bool {
  switch (policy) {
  case OrientationPolicy::apply:
  case OrientationPolicy::ignore:
    return true;
  }
  return false;
}

template <std::size_t Size>
[[nodiscard]] auto signature_matches(std::span<const std::byte> encoded,
                                     const std::array<std::byte, Size> &signature)
    -> bool {
  return encoded.size() >= signature.size() &&
         std::equal(signature.begin(), signature.end(), encoded.begin());
}

template <std::size_t Size>
[[nodiscard]] auto signature_remains_possible(
    std::span<const std::byte> encoded,
    const std::array<std::byte, Size> &signature) -> bool {
  return encoded.size() < signature.size() &&
         std::equal(encoded.begin(), encoded.end(), signature.begin());
}

} // namespace

namespace detail {

struct DecodedImageAccess {
  [[nodiscard]] static auto create(Image image, ImageFormat format,
                                   Extent encoded_extent, bool has_alpha,
                                   OrientationStatus orientation_status)
      -> DecodedImage {
    return DecodedImage{std::move(image), format, encoded_extent, has_alpha,
                        orientation_status};
  }
};

} // namespace detail

auto decode(std::span<const std::byte> encoded, const DecodeOptions &options)
    -> std::expected<DecodedImage, Error> {
  if (encoded.empty()) {
    return std::unexpected{empty_input};
  }
  if (std::cmp_greater(encoded.size(), options.limits.max_input_bytes)) {
    return std::unexpected{input_too_large};
  }
  if (!valid(options.orientation)) {
    return std::unexpected{invalid_orientation};
  }

  if (signature_matches(encoded, jpeg_signature)) {
    auto decoded = detail::decode_jpeg(encoded, options);
    if (!decoded) {
      return std::unexpected{decoded.error()};
    }
    return detail::DecodedImageAccess::create(
        std::move(decoded->image), ImageFormat::jpeg, decoded->encoded_extent,
        false, OrientationStatus::not_present);
  }

  if (!signature_matches(encoded, png_signature)) {
    if (signature_remains_possible(encoded, png_signature) ||
        signature_remains_possible(encoded, jpeg_signature)) {
      return std::unexpected{truncated_signature};
    }
    return std::unexpected{unsupported_format};
  }

  auto decoded = detail::decode_png(encoded, options);
  if (!decoded) {
    return std::unexpected{decoded.error()};
  }

  return detail::DecodedImageAccess::create(
      std::move(decoded->image), ImageFormat::png, decoded->encoded_extent,
      decoded->has_alpha, OrientationStatus::not_present);
}

} // namespace rasterforge
