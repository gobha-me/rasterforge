#include <rasterforge/rasterforge.hpp>

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

static_assert(png_signature.size() == decode_signature_prefix_bytes);

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

  const auto compared = std::min(encoded.size(), png_signature.size());
  for (std::size_t index = 0; index < compared; ++index) {
    if (encoded[index] != png_signature[index]) {
      return std::unexpected{unsupported_format};
    }
  }
  if (encoded.size() < png_signature.size()) {
    return std::unexpected{truncated_signature};
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
