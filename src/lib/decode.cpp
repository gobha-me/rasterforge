#include <rasterforge/rasterforge.hpp>

#include "jpeg_decoder.hpp"
#include "orientation.hpp"
#include "png_decoder.hpp"
#include "webp_decoder.hpp"

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

constexpr std::array webp_riff_signature{
    std::byte{'R'},
    std::byte{'I'},
    std::byte{'F'},
    std::byte{'F'},
};
constexpr std::array webp_format_signature{
    std::byte{'W'},
    std::byte{'E'},
    std::byte{'B'},
    std::byte{'P'},
};

static_assert(png_signature.size() <= decode_signature_prefix_bytes);
static_assert(jpeg_signature.size() <= decode_signature_prefix_bytes);
static_assert(webp_format_signature.size() + 8U ==
              decode_signature_prefix_bytes);

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
[[nodiscard]] auto
signature_matches(std::span<const std::byte> encoded,
                  const std::array<std::byte, Size> &signature) -> bool {
  return encoded.size() >= signature.size() &&
         std::equal(signature.begin(), signature.end(), encoded.begin());
}

[[nodiscard]] auto webp_signature_matches(std::span<const std::byte> encoded)
    -> bool {
  return encoded.size() >= decode_signature_prefix_bytes &&
         std::equal(webp_riff_signature.begin(), webp_riff_signature.end(),
                    encoded.begin()) &&
         std::equal(webp_format_signature.begin(), webp_format_signature.end(),
                    encoded.begin() + 8);
}

[[nodiscard]] auto
webp_signature_remains_possible(std::span<const std::byte> encoded) -> bool {
  if (encoded.size() < webp_riff_signature.size()) {
    return std::equal(encoded.begin(), encoded.end(),
                      webp_riff_signature.begin());
  }
  if (!std::equal(webp_riff_signature.begin(), webp_riff_signature.end(),
                  encoded.begin())) {
    return false;
  }
  if (encoded.size() <= 8U) {
    return true;
  }
  const auto format_bytes = encoded.subspan(8);
  return format_bytes.size() < webp_format_signature.size() &&
         std::equal(format_bytes.begin(), format_bytes.end(),
                    webp_format_signature.begin());
}

template <std::size_t Size>
[[nodiscard]] auto
signature_remains_possible(std::span<const std::byte> encoded,
                           const std::array<std::byte, Size> &signature)
    -> bool {
  return encoded.size() < signature.size() &&
         std::equal(encoded.begin(), encoded.end(), signature.begin());
}

} // namespace

namespace detail {

struct DecodedImageAccess {
  [[nodiscard]] static auto
  create(Image image, ImageFormat format, Extent encoded_extent, bool has_alpha,
         std::optional<Orientation> source_orientation,
         OrientationStatus orientation_status) -> DecodedImage {
    return DecodedImage{std::move(image),   format,
                        encoded_extent,     has_alpha,
                        source_orientation, orientation_status};
  }
};

[[nodiscard]] auto finish_decode(Image image, ImageFormat format,
                                 Extent encoded_extent, bool has_alpha,
                                 const OrientationMetadata &orientation,
                                 const DecodeOptions &options)
    -> std::expected<DecodedImage, Error> {
  auto status = OrientationStatus::not_present;
  std::optional<Orientation> source_orientation;

  if (orientation.invalid) {
    status = OrientationStatus::invalid_ignored;
  } else if (orientation.value) {
    source_orientation = orientation.value;
    if (options.orientation == OrientationPolicy::ignore) {
      status = OrientationStatus::ignored;
    } else {
      status = OrientationStatus::applied;
      auto oriented = apply_orientation(std::move(image), *orientation.value,
                                        options.limits);
      if (!oriented) {
        return std::unexpected{oriented.error()};
      }
      image = std::move(*oriented);
    }
  }

  return DecodedImageAccess::create(std::move(image), format, encoded_extent,
                                    has_alpha, source_orientation, status);
}

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
    return detail::finish_decode(std::move(decoded->image), ImageFormat::jpeg,
                                 decoded->encoded_extent, false,
                                 decoded->orientation, options);
  }

  if (webp_signature_matches(encoded)) {
    auto decoded = detail::decode_webp(encoded, options);
    if (!decoded) {
      return std::unexpected{decoded.error()};
    }
    return detail::finish_decode(std::move(decoded->image), ImageFormat::webp,
                                 decoded->encoded_extent, decoded->has_alpha,
                                 decoded->orientation, options);
  }

  if (!signature_matches(encoded, png_signature)) {
    if (signature_remains_possible(encoded, png_signature) ||
        signature_remains_possible(encoded, jpeg_signature) ||
        webp_signature_remains_possible(encoded)) {
      return std::unexpected{truncated_signature};
    }
    return std::unexpected{unsupported_format};
  }

  auto decoded = detail::decode_png(encoded, options);
  if (!decoded) {
    return std::unexpected{decoded.error()};
  }

  return detail::finish_decode(std::move(decoded->image), ImageFormat::png,
                               decoded->encoded_extent, decoded->has_alpha,
                               decoded->orientation, options);
}

} // namespace rasterforge
